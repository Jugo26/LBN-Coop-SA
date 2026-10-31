// LBN Coop SA in-game controller (original code).
#include "coop.h"
#include "../common/config.h"
#include "../common/interp.h"
#include "../common/log.h"
#include "../common/net_client.h"
#include "../common/net_common.h"
#include "../common/net_server.h"
#include "../common/version.h"
#include "game.h"
#include "hooks.h"
#include "input.h"
#include "overlay.h"
#include "remote_player.h"
#include "script.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <string>

using lbn::MoveAnim;
using lbn::PlayerState;
using lbn::Vec3;

namespace coop {

namespace {

enum class Session { Off, Connecting, Online };

struct ChatLine { std::string text; uint32_t color; uint32_t timeMs; };

struct State {
    std::string gameDir, asiDir, iniPath;
    lbn::CoopConfig cfg;
    lbn::InterpConfig interpCfg;
    lbn::Server server;
    bool serverRunning = false;
    lbn::AsyncClient client;
    Session session = Session::Off;
    uint8_t selfId = 0xFF;
    std::string selfNick;          // nickname the server gave us (may be "LBN_2" if ours was taken)
    uint32_t lastUnstuckMs = 0;
    uint16_t selfPing = 0;
    std::map<uint8_t, std::unique_ptr<RemotePlayer>> remotes;
    std::map<uint8_t, std::string> names;
    std::deque<ChatLine> chat;
    bool menuVisible = false;
    bool controlDisabled = false;
    bool autoStartDone = false;
    bool inputWarned = false;
    uint32_t lastSendMs = 0;
    uint32_t seq = 0;
    uint32_t firstFrameMs = 0;
    void* lastPlayerPed = nullptr;
} *g = nullptr;

uintptr_t g_origProcess = 0, g_origDrawing = 0, g_origShutdown = 0;
uintptr_t g_origInitScripts[3] = {};

constexpr uint32_t kColSystem = overlay::rgba(255, 220, 90);
constexpr uint32_t kColChat = overlay::rgba(255, 255, 255);
constexpr uint32_t kColError = overlay::rgba(255, 110, 110);
constexpr uint32_t kColOk = overlay::rgba(120, 255, 140);

void addLine(const std::string& text, uint32_t color) {
    g->chat.push_back({lbn::foldToAscii(text), color, lbn::nowMs()});
    while (g->chat.size() > 10) g->chat.pop_front();
}

void setPlayerControl(bool enabled) {
    if (!sa::findPlayerPed()) { g->controlDisabled = false; return; }
    if (!enabled && !g->controlDisabled) {
        scm::Call(scm::SET_PLAYER_CONTROL).i(0).i(0).run();
        g->controlDisabled = true;
    } else if (enabled && g->controlDisabled) {
        scm::Call(scm::SET_PLAYER_CONTROL).i(0).i(1).run();
        g->controlDisabled = false;
    }
}

void* localVehicle() { return sa::pedVehicle(sa::findPlayerPed()); }

void destroyAllRemotes(bool worldValid) {
    void* lv = worldValid ? localVehicle() : nullptr;
    for (auto& kv : g->remotes) {
        if (worldValid) kv.second->destroyEntities(lv);
        else kv.second->forgetEntities();
    }
    g->remotes.clear();
}

// touchWorld=false when the game is shutting down: then we must not call into the game anymore.
void stopSession(const std::string& why, uint32_t color, bool touchWorld = true) {
    if (g->session == Session::Off && !g->serverRunning) return;
    g->client.disconnect(200);
    if (g->serverRunning) { g->server.stopThreaded(); g->serverRunning = false; }
    destroyAllRemotes(touchWorld && sa::findPlayerPed() != nullptr);
    g->names.clear();
    g->session = Session::Off;
    g->selfId = 0xFF;
    g->selfNick.clear();
    input::closeChat();
    if (touchWorld) setPlayerControl(true);
    if (!why.empty()) addLine(why, color);
    LOGI("Sessao encerrada: %s", why.empty() ? "(jogo fechando)" : why.c_str());
}

void startSession() {
    if (g->session != Session::Off) { addLine("LBN Coop: ja conectado ou conectando (F10 para sair)", kColSystem); return; }
    std::string host = g->cfg.hostIp;
    if (g->cfg.mode == lbn::Mode::Host) {
        lbn::ServerOptions o;
        o.port = g->cfg.port;
        o.name = g->cfg.serverName;
        o.timeoutMs = uint32_t(g->cfg.timeoutMs);
        if (!g->server.startThreaded(o)) {
            addLine("Erro: nao foi possivel abrir a porta UDP " + std::to_string(g->cfg.port) + " (veja o log)", kColError);
            return;
        }
        g->serverRunning = true;
        host = "127.0.0.1";
        addLine("Servidor iniciado na porta UDP " + std::to_string(g->cfg.port) + ". Aguardando amigos...", kColOk);
    }
    if (!g->client.connect(host, g->cfg.port, g->cfg.nickname, g->cfg.skin, uint32_t(g->cfg.timeoutMs))) {
        addLine("Erro ao iniciar a conexao (veja LbnCoopSA.log)", kColError);
        if (g->serverRunning) { g->server.stopThreaded(); g->serverRunning = false; }
        return;
    }
    g->session = Session::Connecting;
    if (g->cfg.mode == lbn::Mode::Join)
        addLine("Conectando a " + host + ":" + std::to_string(g->cfg.port) + "...", kColSystem);
}

void* vehicleOfPlayerCb(uint8_t id, void*) {
    auto it = g->remotes.find(id);
    return it == g->remotes.end() ? nullptr : it->second->vehicle();
}

int ownerOfVehicle(void* veh) {
    if (!veh) return -1;
    for (auto& kv : g->remotes) if (kv.second->vehicle() == veh) return kv.first;
    return -1;
}

bool captureLocal(PlayerState& s) {
    void* ped = sa::findPlayerPed();
    if (!ped) return false;
    Vec3 pos;
    sa::entityPos(ped, pos);
    Vec3 ms{sa::at<float>(ped, sa::off::moveSpeed), sa::at<float>(ped, sa::off::moveSpeed + 4),
            sa::at<float>(ped, sa::off::moveSpeed + 8)};
    s.pos = pos;
    s.vel = ms * 50.f;
    s.heading = lbn::wrapAngle(sa::at<float>(ped, sa::off::pedHeadingCur));
    float hs = std::sqrt(s.vel.x * s.vel.x + s.vel.y * s.vel.y);
    s.anim = lbn::moveAnimFromPedState(sa::at<int>(ped, sa::off::pedMoveState), hs);
    float hp = sa::at<float>(ped, sa::off::pedHealth), ar = sa::at<float>(ped, sa::off::pedArmour);
    s.health = uint8_t(std::clamp(hp, 0.f, 255.f));
    s.armour = uint8_t(std::clamp(ar, 0.f, 255.f));
    s.flags = 0;
    void* veh = sa::pedVehicle(ped);
    if (veh) {
        int seat = sa::pedSeat(ped, veh);
        s.seat = uint8_t(seat < 0 ? 0 : seat);
        int owner = ownerOfVehicle(veh);
        if (owner >= 0 && s.seat > 0) {
            s.flags = PlayerState::InVehicle | PlayerState::RemoteVehicle;
            s.vehicleOwnerId = uint8_t(owner);
        } else {
            s.flags = PlayerState::InVehicle;
            void* m = sa::at<void*>(veh, sa::off::matrix);
            if (!m) return false;
            auto rd = [&](uint32_t o) { return Vec3{sa::at<float>(m, o), sa::at<float>(m, o + 4), sa::at<float>(m, o + 8)}; };
            s.veh.key = uint32_t(sa::vehicleRef(veh));
            s.veh.model = uint16_t(sa::modelOf(veh));
            s.veh.pos = rd(sa::off::mPos);
            s.veh.rot = lbn::quatFromAxes(rd(sa::off::mRight), rd(sa::off::mFwd), rd(sa::off::mUp));
            s.veh.vel = Vec3{sa::at<float>(veh, sa::off::moveSpeed), sa::at<float>(veh, sa::off::moveSpeed + 4),
                             sa::at<float>(veh, sa::off::moveSpeed + 8)} * 50.f;
            s.veh.color1 = sa::at<uint8_t>(veh, sa::off::vehColor1);
            s.veh.color2 = sa::at<uint8_t>(veh, sa::off::vehColor2);
            s.veh.health = sa::at<float>(veh, sa::off::vehHealth);
        }
    }
    return lbn::isSaneState(s);
}

// /soltar: moves the local player ~3 m to a free spot (ground found, no wall/car/object, away from avatars).
void unstuckLocal() {
    void* ped = sa::findPlayerPed();
    if (!ped) return;
    uint32_t now = lbn::nowMs();
    if (g->lastUnstuckMs && now - g->lastUnstuckMs < 2000) { addLine("Aguarde um pouco para usar /soltar de novo.", kColSystem); return; }
    g->lastUnstuckMs = now;
    if (sa::pedVehicle(ped)) { addLine("/soltar: saia do veiculo primeiro.", kColSystem); return; }
    int h = scm::playerCharHandle();
    if (h < 0) return;
    Vec3 p; sa::entityPos(ped, p);
    std::vector<Vec3> avoid;
    float nearest = 1e9f; Vec3 away{1, 0, 0};
    for (auto& kv : g->remotes) {
        Vec3 q;
        if (!kv.second->ped() || !sa::entityPos(kv.second->ped(), q)) continue;
        avoid.push_back(q);
        Vec3 d{p.x - q.x, p.y - q.y, 0};
        float l = d.length();
        if (l < nearest) { nearest = l; away = l > 0.05f ? d * (1.f / l) : Vec3{1, 0, 0}; }
    }
    float base = std::atan2(away.y, away.x);
    for (float r : {3.0f, 4.0f, 2.5f, 5.0f}) {
        for (int k = 0; k < 8; ++k) {
            float a = base + (k % 2 ? 1.f : -1.f) * float((k + 1) / 2) * (lbn::kPi / 4.f);
            float x = p.x + r * std::cos(a), y = p.y + r * std::sin(a);
            scm::Call gz(scm::GET_GROUND_Z_FOR_3D_COORD);
            gz.f(x).f(y).f(p.z + 2.0f).out(0);
            gz.run();
            float ground = gz.localf(0);
            // p.z is the ped's centre (~1 m above its feet).
            if (!(ground > -100.f) || ground == 0.f || std::fabs(ground - (p.z - 1.0f)) > 2.5f) continue;
            bool blocked = scm::Call(scm::IS_AREA_OCCUPIED).f(x - 0.45f).f(y - 0.45f).f(ground + 0.3f)
                               .f(x + 0.45f).f(y + 0.45f).f(ground + 1.9f).i(1).i(1).i(0).i(1).i(0).run();
            if (blocked) continue;
            bool nearAvatar = false;
            for (auto& q : avoid) if ((Vec3{q.x - x, q.y - y, 0}).length() < 1.5f) nearAvatar = true;
            if (nearAvatar) continue;
            scm::Call(scm::SET_CHAR_COORDINATES).i(h).f(x).f(y).f(ground).run();
            LOGI("/soltar: jogador movido de (%.1f, %.1f, %.1f) para (%.1f, %.1f, %.1f)", p.x, p.y, p.z, x, y, ground);
            addLine("Voce foi movido " + std::to_string(int(r + 0.5f)) + " m para um lugar livre.", kColOk);
            return;
        }
    }
    LOGW("/soltar: nenhum lugar livre perto de (%.1f, %.1f, %.1f)", p.x, p.y, p.z);
    addLine("/soltar: nao achei lugar livre por perto. Ande um pouco ou tente de novo.", kColError);
}

void handleCommand(const std::string& cmd) {
    if (cmd == "/soltar" || cmd == "/unstuck") { unstuckLocal(); return; }
    if (cmd == "/sair" || cmd == "/quit") { stopSession("Voce saiu da sessao.", kColSystem); return; }
    if (cmd == "/ping") {
        addLine("Seu ping: " + std::to_string(g->client.pingMs()) + " ms", kColSystem);
        return;
    }
    if (cmd == "/jogadores" || cmd == "/players") {
        std::string s = "Jogadores: " + (g->selfNick.empty() ? g->cfg.nickname : g->selfNick) + " (voce)";
        for (auto& kv : g->remotes) s += ", " + kv.second->nick();
        addLine(s, kColSystem);
        return;
    }
    addLine("Comandos: /ping /jogadores /soltar /sair", kColSystem);
}

void handleActions() {
    for (input::Action a : input::drainActions()) {
        switch (a) {
        case input::Action::ToggleMenu: g->menuVisible = !g->menuVisible; break;
        case input::Action::Start: startSession(); break;
        case input::Action::ToggleMode:
            if (g->session != Session::Off) { addLine("Saia da sessao (F10) antes de trocar o modo.", kColSystem); break; }
            g->cfg.mode = g->cfg.mode == lbn::Mode::Host ? lbn::Mode::Join : lbn::Mode::Host;
            addLine(std::string("Modo alterado para: ") + (g->cfg.mode == lbn::Mode::Host ? "HOST" : "CLIENTE (" + g->cfg.hostIp + ")"), kColSystem);
            break;
        case input::Action::Disconnect:
            if (g->session == Session::Off) addLine("Voce nao esta em uma sessao.", kColSystem);
            else stopSession("Voce saiu da sessao.", kColSystem);
            break;
        case input::Action::Unstuck:
            if (g->menuVisible) unstuckLocal();
            break;
        case input::Action::ChatOpened: setPlayerControl(false); break;
        case input::Action::ChatCancel: setPlayerControl(true); break;
        case input::Action::ChatSubmit: {
            setPlayerControl(true);
            std::string t = lbn::sanitizeChat(input::takeSubmitted());
            if (t.empty()) break;
            if (t[0] == '/') handleCommand(t);
            else g->client.sendChat(t);
            break;
        }
        default: break;
        }
    }
}

void handleNetwork() {
    lbn::ClientEvent e;
    while (g->client.pollEvent(e)) {
        switch (e.type) {
        case lbn::ClientEvent::Connected:
            g->session = Session::Online;
            g->selfId = e.id;
            g->selfNick = e.nick.empty() ? g->cfg.nickname : e.nick;
            g->names[e.id] = g->selfNick;
            addLine("Conectado a '" + e.text + "' como " + g->selfNick + ". Aperte T para o chat.", kColOk);
            if (g->selfNick != g->cfg.nickname) {
                LOGW("Apelido duplicado: '%s' ja estava em uso no servidor; voce entrou como '%s'",
                     g->cfg.nickname.c_str(), g->selfNick.c_str());
                addLine("Seu apelido '" + g->cfg.nickname + "' ja estava em uso: voce aparece como '" + g->selfNick +
                        "'. Mude Nickname no LbnCoopSA.ini.", kColSystem);
            }
            break;
        case lbn::ClientEvent::ConnectFailed:
            stopSession("Falha ao conectar: " + e.text + ". Confira IP, porta e firewall.", kColError);
            break;
        case lbn::ClientEvent::Rejected:
            stopSession("Conexao recusada: " + e.text, kColError);
            break;
        case lbn::ClientEvent::Disconnected:
            stopSession("Desconectado: " + e.text, kColError);
            break;
        case lbn::ClientEvent::PlayerJoined:
            g->names[e.id] = e.nick;

            if (e.id != g->selfId && !g->remotes.count(e.id))
                g->remotes[e.id] = std::make_unique<RemotePlayer>(e.id, e.nick, e.skin, g->interpCfg);
            LOGI("Jogador %s (id %u) na sessao", e.nick.c_str(), unsigned(e.id));
            break;
        case lbn::ClientEvent::PlayerLeft: {
            auto it = g->remotes.find(e.id);
            if (it != g->remotes.end()) {
                it->second->destroyEntities(localVehicle());
                g->remotes.erase(it);
            }
            g->names.erase(e.id);
            break;
        }
        case lbn::ClientEvent::State: {
            auto it = g->remotes.find(e.id);
            if (it != g->remotes.end()) it->second->pushState(e.state, e.recvMs);
            break;
        }
        case lbn::ClientEvent::Chat:
            if (e.id == lbn::kSystemSender) addLine(e.text, kColSystem);
            else {
                auto n = g->names.find(e.id);
                std::string who = (n != g->names.end()) ? n->second : (e.id == g->selfId ? g->cfg.nickname : "?");
                addLine(who + ": " + e.text, kColChat);
            }
            break;
        case lbn::ClientEvent::Pings:
            for (auto& p : e.pings.entries) {
                if (p.id == g->selfId) g->selfPing = p.pingMs;
                auto it = g->remotes.find(p.id);
                if (it != g->remotes.end()) it->second->setPing(p.pingMs);
            }
            break;
        }
    }
}

void onProcess() {
    uint32_t now = lbn::nowMs();
    if (!g->firstFrameMs) g->firstFrameMs = now;

    if (!input::installed()) {
        input::Keys k{g->cfg.keyMenu, g->cfg.keyStart, g->cfg.keyToggleMode, g->cfg.keyDisconnect, g->cfg.keyChat,
                      g->cfg.keyUnstuck};
        if (input::install(k)) LOGI("Teclado: janela do jogo encontrada, atalhos ativos");
        else if (!g->inputWarned && now - g->firstFrameMs > 10000) {
            g->inputWarned = true;
            LOGW("Teclado: nao encontrei a janela do jogo; atalhos indisponiveis");
        }
    }

    void* ped = sa::findPlayerPed();
    if (ped != g->lastPlayerPed) {
        // New player ped (new game / load): any old entity handles are meaningless now.
        if (g->lastPlayerPed) for (auto& kv : g->remotes) kv.second->forgetEntities();
        g->lastPlayerPed = ped;
        g->controlDisabled = false;
    }

    handleActions();
    if (g->cfg.autoStart && !g->autoStartDone && ped) {
        g->autoStartDone = true;
        startSession();
    }
    handleNetwork();

    bool online = g->session == Session::Online;
    input::setChatAllowed(online && ped != nullptr);
    if (!online || !ped) return;

    uint32_t interval = uint32_t(1000 / std::max(1, g->cfg.sendRateHz));
    if (now - g->lastSendMs >= interval) {
        PlayerState s;
        if (captureLocal(s)) {
            s.seq = ++g->seq;
            s.timeMs = now;
            g->client.sendState(s);
        }
        g->lastSendMs = now;
    }

    WorldLookup w;
    w.selfId = g->selfId;
    w.localPed = ped;
    w.localVehicle = sa::pedVehicle(ped);
    w.vehicleOfPlayer = vehicleOfPlayerCb;
    for (auto& kv : g->remotes) if (kv.second->hasState()) kv.second->update(now, w);
}

void drawNametags(uint32_t now) {
    if (!g->cfg.showNametags) return;
    Vec3 me;
    if (!sa::entityPos(sa::findPlayerPed(), me)) return;
    for (auto& kv : g->remotes) {
        Vec3 p;
        if (!kv.second->nametagPos(p)) continue;
        float dist = (p - me).length();
        if (dist > g->cfg.nametagDistance) continue;
        float sx, sy, depth;
        if (!sa::worldToScreen(p, sx, sy, depth)) continue;
        std::string label = lbn::foldToAscii(kv.second->nick());
        if (now - kv.second->lastStateMs() > 2500) label += " (pausado)";
        float scale = std::clamp(1.1f - dist / (g->cfg.nametagDistance * 1.5f), 0.55f, 1.0f);
        overlay::text(sx, sy - overlay::lineHeight(scale), label.c_str(), overlay::rgba(255, 255, 255, 230), scale, overlay::Center);
    }
}

void drawChat(uint32_t now) {
    float x = sa::screenWidth() * 0.02f;
    float y = sa::screenHeight() * 0.30f;
    float lh = overlay::lineHeight(0.9f);
    bool typing = input::chatOpen();
    for (auto& l : g->chat) {
        uint32_t age = now - l.timeMs;
        if (!typing && age > 20000) continue;
        uint8_t a = (!typing && age > 17000) ? uint8_t(255 * (20000 - age) / 3000) : 255;
        overlay::text(x, y, l.text.c_str(), (l.color & 0x00FFFFFFu) | (uint32_t(a) << 24), 0.9f);
        y += lh;
    }
    if (typing) {
        std::string line = "Chat: " + lbn::foldToAscii(input::chatBuffer()) + ((now / 400) % 2 ? "_" : " ");
        overlay::text(x, y + lh * 0.3f, line.c_str(), overlay::rgba(150, 220, 255), 0.9f, overlay::Left, true);
    }
}

void drawMenu(uint32_t now) {
    if (!g->menuVisible) {
        if (g->session == Session::Off && g->firstFrameMs && now - g->firstFrameMs < 20000) {
            overlay::text(sa::screenWidth() * 0.98f, sa::screenHeight() * 0.92f,
                          "LBN Coop SA: F7 iniciar | F8 menu | preso? /soltar ou F8+F6", overlay::rgba(255, 255, 255, 200), 0.8f, overlay::Right);
        }
        return;
    }
    float x = sa::screenWidth() * 0.62f;
    float y = sa::screenHeight() * 0.18f;
    float lh = overlay::lineHeight(0.85f);
    auto line = [&](const std::string& s, uint32_t c) { overlay::text(x, y, lbn::foldToAscii(s).c_str(), c, 0.85f, overlay::Left, true); y += lh; };
    line(std::string(LBN_COOP_NAME) + " " + LBN_COOP_VERSION, overlay::rgba(255, 200, 60));
    const char* st = g->session == Session::Online ? "ONLINE" : g->session == Session::Connecting ? "CONECTANDO..." : "DESLIGADO";
    line(std::string("Estado: ") + st, g->session == Session::Online ? kColOk : kColSystem);
    if (g->cfg.mode == lbn::Mode::Host) line("Modo: HOST (porta UDP " + std::to_string(g->cfg.port) + ")", kColChat);
    else line("Modo: CLIENTE -> " + g->cfg.hostIp + ":" + std::to_string(g->cfg.port), kColChat);
    line("Apelido: " + (g->selfNick.empty() ? g->cfg.nickname : g->selfNick) + "  skin " + std::to_string(g->cfg.skin), kColChat);
    if (g->session == Session::Online) {
        line("Voce: " + std::to_string(g->selfPing) + " ms", kColChat);
        for (auto& kv : g->remotes) {
            std::string s = "  " + kv.second->nick() + "  " + std::to_string(kv.second->pingMs()) + " ms";
            if (!kv.second->hasState()) s += " (carregando)";
            line(s, kColChat);
        }
    }
    line("F6 soltar boneco (preso? = /soltar)", overlay::rgba(150, 220, 255));
    line("F7 iniciar  F9 trocar modo  F10 sair  T chat", overlay::rgba(200, 200, 200));
    line("Chat: /ping /jogadores /soltar /sair", overlay::rgba(200, 200, 200));
}

void onDraw() {
    if (!sa::findPlayerPed()) return;
    uint32_t now = lbn::nowMs();
    drawNametags(now);
    drawChat(now);
    drawMenu(now);
}

// ---- hook trampolines ----
void __cdecl hkProcess() {
    reinterpret_cast<void(__cdecl*)()>(g_origProcess)();
    onProcess();
}
void __cdecl hkDrawing() {
    reinterpret_cast<void(__cdecl*)()>(g_origDrawing)();
    onDraw();
}
void __cdecl hkShutdown() {
    LOGI("Jogo fechando: encerrando a sessao");
    stopSession("", 0, false);
    input::uninstall();
    reinterpret_cast<void(__cdecl*)()>(g_origShutdown)();
}
template <int N> void __cdecl hkInitScripts() {
    reinterpret_cast<void(__cdecl*)()>(g_origInitScripts[N])();
    // CTheScripts::Init runs on new game / load game: the old world (and our entities) is gone.
    for (auto& kv : g->remotes) kv.second->forgetEntities();
    g->controlDisabled = false;
}

std::string dirOf(const char* path) {
    std::string s(path);
    size_t p = s.find_last_of("\\/");
    return p == std::string::npos ? std::string(".") : s.substr(0, p);
}

} // namespace

bool init(HMODULE self) {
    g = new State();
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    g->gameDir = dirOf(buf);
    GetModuleFileNameA(self, buf, MAX_PATH);
    g->asiDir = dirOf(buf);

    lbn::logOpen(g->gameDir + "\\LbnCoopSA.log", false);
    LOGI("%s %s carregado (protocolo %d)", LBN_COOP_NAME, LBN_COOP_VERSION, LBN_PROTOCOL_VERSION);
    LOGI("Pasta do jogo: %s", g->gameDir.c_str());

    const char* ver = sa::detectGameVersion();
    if (!ver) {
        LOGE("Versao do gta_sa.exe NAO suportada. Use o GTA SA 1.0 US (14.383.616 bytes). Mod desativado.");
        if (!GetEnvironmentVariableA("LBN_COOP_NO_DIALOG", nullptr, 0))
            MessageBoxA(nullptr, "LBN Coop SA: versao do gta_sa.exe nao suportada.\nUse o executavel 1.0 US (14.383.616 bytes).\nO mod foi desativado.",
                    "LBN Coop SA", MB_ICONWARNING | MB_OK);
        return false;
    }
    LOGI("Versao do jogo detectada: %s", ver);

    g->iniPath = g->asiDir + "\\LbnCoopSA.ini";
    lbn::IniFile ini;
    if (!ini.loadFromFile(g->iniPath)) {
        LOGW("LbnCoopSA.ini nao encontrado em %s; criando um padrao", g->asiDir.c_str());
        if (FILE* f = std::fopen(g->iniPath.c_str(), "wb")) {
            std::string t = lbn::defaultIniText();
            std::fwrite(t.data(), 1, t.size(), f);
            std::fclose(f);
        }
        ini.loadFromString(lbn::defaultIniText());
    }
    std::string warn;
    g->cfg = lbn::configFromIni(ini, &warn);
    if (!warn.empty()) LOGW("Config: %s", warn.c_str());
    g->interpCfg.delayMs = uint32_t(g->cfg.interpDelayMs);
    remoteOptions().pedCollision = g->cfg.remotePedCollision;
    LOGI("Config: apelido=%s modo=%s hostIP=%s porta=%u skin=%u autoStart=%d",
         g->cfg.nickname.c_str(), lbn::modeName(g->cfg.mode), g->cfg.hostIp.c_str(), unsigned(g->cfg.port),
         unsigned(g->cfg.skin), int(g->cfg.autoStart));
    LOGI("Config: SendRate=%d InterpDelayMs=%d RemotePedCollision=%d", g->cfg.sendRateHz, g->cfg.interpDelayMs,
         int(g->cfg.remotePedCollision));

    bool ok = hooks::redirectCall(sa::hookGameProcess, reinterpret_cast<void*>(&hkProcess), &g_origProcess) &&
              hooks::redirectCall(sa::hookDrawing, reinterpret_cast<void*>(&hkDrawing), &g_origDrawing);
    if (!ok) {
        LOGE("Falha ao instalar os hooks principais (bytes inesperados). Mod desativado.");
        hooks::restoreCall(sa::hookGameProcess, g_origProcess);
        g_origProcess = 0;
        return false;
    }
    if (!hooks::redirectCall(sa::hookShutdownRw, reinterpret_cast<void*>(&hkShutdown), &g_origShutdown))
        LOGW("Hook de encerramento nao instalado (nao e critico)");
    void* initFns[3] = {reinterpret_cast<void*>(&hkInitScripts<0>), reinterpret_cast<void*>(&hkInitScripts<1>),
                        reinterpret_cast<void*>(&hkInitScripts<2>)};
    for (int i = 0; i < 3; ++i)
        if (!hooks::redirectCall(sa::hookInitScripts[i], initFns[i], &g_origInitScripts[i]))
            LOGW("Hook CTheScripts::Init #%d nao instalado (nao e critico)", i);
    LOGI("Hooks instalados. Aperte F7 no jogo para %s.", g->cfg.mode == lbn::Mode::Host ? "hospedar" : "conectar");
    return true;
}

} // namespace coop
