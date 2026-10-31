// LBN Coop SA relay server implementation (original code).
#include "net_server.h"
#include "config.h"
#include "log.h"
#include "net_common.h"
#include <enet/enet.h>
#include <chrono>
#include <cstdint>

namespace lbn {

static int peerSlot(ENetPeer* p) { return int(reinterpret_cast<intptr_t>(p->data)) - 1; }
static void setPeerSlot(ENetPeer* p, int slot) { p->data = reinterpret_cast<void*>(intptr_t(slot + 1)); }

Server::~Server() {
    stopThreaded();
    stop();
}

bool Server::start(const ServerOptions& opt) {
    std::lock_guard<std::mutex> lk(mu_);
    if (host_) return true;
    opt_ = opt;
    if (opt_.maxPlayers < 1) opt_.maxPlayers = 1;
    if (opt_.maxPlayers > LBN_MAX_PLAYERS) opt_.maxPlayers = LBN_MAX_PLAYERS;
    if (!netInit()) { LOGE("Servidor: falha ao inicializar ENet"); return false; }
    netRef_ = true;

    ENetAddress addr{};
    addr.host = ENET_HOST_ANY;
    if (!opt_.bindAddress.empty() && enet_address_set_host_ip(&addr, opt_.bindAddress.c_str()) != 0) {
        LOGW("Servidor: endereco de bind invalido '%s', usando todos", opt_.bindAddress.c_str());
        addr.host = ENET_HOST_ANY;
    }
    addr.port = opt_.port;
    // A couple of spare peer slots so we can answer "server full" politely.
    host_ = enet_host_create(&addr, size_t(opt_.maxPlayers + 2), kChanCount, 0, 0);
    if (!host_) {
        LOGE("Servidor: nao foi possivel abrir a porta UDP %u (ja em uso?)", unsigned(opt_.port));
        netShutdown(); netRef_ = false;
        return false;
    }
    for (auto& s : slots_) s = Slot();
    lastPings_ = nowMs();
    LOGI("Servidor '%s' ouvindo na porta UDP %u (max %d jogadores)", opt_.name.c_str(), unsigned(opt_.port), opt_.maxPlayers);
    return true;
}

void Server::stop() {
    std::lock_guard<std::mutex> lk(mu_);
    if (!host_) return;
    for (auto& s : slots_)
        if (s.used && s.peer) enet_peer_disconnect(s.peer, kQuitCode);
    // Flush the disconnects briefly.
    ENetEvent ev;
    uint32_t until = nowMs() + 300;
    while (nowMs() < until && enet_host_service(host_, &ev, 10) >= 0) {
        if (ev.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(ev.packet);
    }
    enet_host_destroy(host_);
    host_ = nullptr;
    for (auto& s : slots_) s = Slot();
    authority_ = 0xFF; haveWorld_ = false;
    if (netRef_) { netShutdown(); netRef_ = false; }
    LOGI("Servidor encerrado");
}

bool Server::startThreaded(const ServerOptions& opt) {
    if (!start(opt)) return false;
    threadRun_ = true;
    thread_ = std::thread([this] {
        // Wait on the socket WITHOUT holding the mutex, then process under the lock.
        // (Holding the lock while blocking starved other threads calling players().)
        while (threadRun_) {
            ENetSocket sock = host_->socket; // host_ only changes in start()/stop(), never while this thread runs
            enet_uint32 cond = ENET_SOCKET_WAIT_RECEIVE;
            enet_socket_wait(sock, &cond, 5);
            service(0);
        }
    });
    return true;
}

void Server::stopThreaded() {
    if (threadRun_) {
        threadRun_ = false;
        if (thread_.joinable()) thread_.join();
    }
    stop();
}

int Server::slotOf(ENetPeer* peer) const {
    int s = peerSlot(peer);
    if (s >= 0 && s < LBN_MAX_PLAYERS && slots_[s].used && slots_[s].peer == peer) return s;
    return -1;
}

void Server::service(uint32_t waitMs) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!host_) return;
    ENetEvent ev;
    int r = enet_host_service(host_, &ev, waitMs);
    while (r > 0) {
        switch (ev.type) {
        case ENET_EVENT_TYPE_CONNECT: onConnect(ev.peer); break;
        case ENET_EVENT_TYPE_RECEIVE:
            onReceive(ev.peer, ev.packet->data, ev.packet->dataLength);
            enet_packet_destroy(ev.packet);
            break;
        case ENET_EVENT_TYPE_DISCONNECT: onDisconnect(ev.peer, ev.data == kQuitCode); break;
        default: break;
        }
        r = enet_host_check_events(host_, &ev);
    }
    uint32_t now = nowMs();
    if (now - lastPings_ >= 1000) { lastPings_ = now; sendPings(); }
}

void Server::onConnect(ENetPeer* peer) {
    peer->data = nullptr; // not joined until Hello
    uint32_t t = opt_.timeoutMs;
    enet_peer_timeout(peer, 32, t / 2, t);
    char ip[64] = "?";
    enet_address_get_host_ip(&peer->address, ip, sizeof ip);
    LOGI("Servidor: conexao de %s:%u (aguardando identificacao)", ip, unsigned(peer->address.port));
}

std::string Server::uniqueNick(const std::string& base) const {
    auto taken = [&](const std::string& n) {
        for (auto& s : slots_) if (s.used && s.nick == n) return true;
        return false;
    };
    if (!taken(base)) return base;
    for (int i = 2; i < 100; ++i) {
        std::string n = base.substr(0, kMaxNickLen - 3) + "_" + std::to_string(i);
        if (!taken(n)) return n;
    }
    return base;
}

void Server::handleHello(ENetPeer* peer, const uint8_t* data, size_t len) {
    HelloMsg h;
    auto reject = [&](RejectReason why, const std::string& detail) {
        RejectMsg rm; rm.reason = why; rm.detail = detail;
        sendTo(peer, encode(rm), true);
        enet_peer_disconnect_later(peer, 0);
        LOGW("Servidor: conexao recusada (%s) %s", rejectReasonPtBr(why), detail.c_str());
    };
    if (!decode(data, len, h) || h.magic != LBN_PROTOCOL_MAGIC) { reject(RejectReason::BadMessage, ""); return; }
    if (h.version != LBN_PROTOCOL_VERSION || h.modVersion != LBN_MOD_VERSION) {
        // Old protocol-1 clients send no mod version: report it as "antiga/desconhecida".
        reject(RejectReason::BadVersion, modVersionMismatchPtBr(LBN_MOD_VERSION, h.modVersion));
        // Tell the host (and anyone already in) why the friend could not join.
        broadcastSystem("Recusado '" + sanitizeNick(h.nick) + "': versao do mod v" +
                        (h.modVersion.empty() ? std::string("antiga") : h.modVersion.substr(0, 32)) +
                        ", host v" LBN_MOD_VERSION ". Atualizem pelo LbnCoopSA-Iniciar.exe");
        return;
    }
    std::string nick = sanitizeNick(h.nick);
    if (nick.empty()) { reject(RejectReason::BadName, ""); return; }
    int slot = -1;
    for (int i = 0; i < opt_.maxPlayers; ++i) if (!slots_[i].used) { slot = i; break; }
    if (slot < 0) { reject(RejectReason::ServerFull, ""); return; }

    std::string wanted = nick;
    nick = uniqueNick(nick);
    if (nick != wanted) {
        char ip[64] = "?";
        enet_address_get_host_ip(&peer->address, ip, sizeof ip);
        LOGW("Servidor: apelido duplicado '%s' (de %s): ja em uso, entrou como '%s'", wanted.c_str(), ip, nick.c_str());
        duplicateNicks_++;
    }
    Slot& s = slots_[slot];
    s = Slot();
    s.used = true; s.peer = peer; s.nick = nick;
    s.skin = isValidPedSkin(h.skin) ? h.skin : 106;
    setPeerSlot(peer, slot);

    WelcomeMsg w; w.yourId = uint8_t(slot); w.maxPlayers = uint8_t(opt_.maxPlayers); w.serverName = opt_.name; w.yourNick = nick;
    sendTo(peer, encode(w), true);
    // Tell the newcomer about everyone already here.
    for (int i = 0; i < LBN_MAX_PLAYERS; ++i) {
        if (i == slot || !slots_[i].used) continue;
        PlayerJoinMsg j; j.id = uint8_t(i); j.nick = slots_[i].nick; j.skin = slots_[i].skin;
        sendTo(peer, encode(j), true);
    }
    updateAuthority();
    { SessionMsg sm; sm.authorityId = authority_; sendTo(peer, encode(sm), true); }
    if (haveWorld_ && authority_ != slot) { WorldS2CMsg wm; wm.authorityId = authority_; wm.w = world_; sendTo(peer, encode(wm), true); }
    PlayerJoinMsg me; me.id = uint8_t(slot); me.nick = nick; me.skin = s.skin;
    broadcast(encode(me), true, slot);
    LOGI("Servidor: '%s' entrou (id %d, skin %u)", nick.c_str(), slot, unsigned(s.skin));
    broadcastSystem(nick + " entrou no jogo");
    if (nick != wanted)
        broadcastSystem("Apelido '" + wanted + "' ja estava em uso: renomeado para '" + nick + "' (mude Nickname no LbnCoopSA.ini)");
}

void Server::onReceive(ENetPeer* peer, const uint8_t* data, size_t len) {
    MsgType t;
    if (!peekType(data, len, t)) return;
    int slot = slotOf(peer);
    if (slot < 0) {
        if (t == MsgType::Hello) handleHello(peer, data, len);
        return; // ignore anything else before Hello
    }
    switch (t) {
    case MsgType::StateC2S: {
        PlayerState st;
        if (!decodeStateC2S(data, len, st) || !isSaneState(st)) return;
        // Simple flood guard: at most 90 states per second per player.
        Slot& s = slots_[slot];
        uint32_t now = nowMs();
        if (now - s.stateWindowStart >= 1000) { s.stateWindowStart = now; s.stateCount = 0; }
        if (++s.stateCount > 90) return;
        // Host-authoritative weapons: only real SA weapon ids are relayed.
        if (!isValidWeapon(st.weapon)) st.weapon = 0;
        s.hasPos = true; s.lastPos = st.pos;
        broadcast(encodeStateS2C(uint8_t(slot), st), false, slot);
        relayed_++;
        break;
    }
    case MsgType::ChatC2S: {
        std::string text;
        if (!decodeChatC2S(data, len, text)) return;
        text = sanitizeChat(text);
        if (text.empty()) return;
        LOGI("Chat <%s> %s", slots_[slot].nick.c_str(), text.c_str());
        broadcast(encodeChatS2C(uint8_t(slot), text), true, -1);
        break;
    }
    case MsgType::DamageC2S: handleDamage(slot, data, len); break;
    case MsgType::WorldC2S: handleWorld(slot, data, len); break;
    default: break;
    }
}

void Server::onDisconnect(ENetPeer* peer, bool graceful) {
    int slot = slotOf(peer);
    peer->data = nullptr;
    if (slot < 0) return;
    std::string nick = slots_[slot].nick;
    slots_[slot] = Slot();
    PlayerLeaveMsg lm; lm.id = uint8_t(slot); lm.reason = graceful ? LeaveReason::Quit : LeaveReason::Timeout;
    broadcast(encode(lm), true, -1);
    LOGI("Servidor: '%s' %s", nick.c_str(), leaveReasonPtBr(lm.reason));
    broadcastSystem(nick + " " + leaveReasonPtBr(lm.reason));
    updateAuthority();
}

void Server::updateAuthority() {
    uint8_t a = 0xFF;
    for (int i = 0; i < LBN_MAX_PLAYERS; ++i) if (slots_[i].used) { a = uint8_t(i); break; }
    if (a == authority_) return;
    authority_ = a;
    if (a == 0xFF) { haveWorld_ = false; return; }
    LOGI("Servidor: '%s' (id %u) decide hora, clima, procurado e regras (autoridade)", slots_[a].nick.c_str(), unsigned(a));
    SessionMsg sm; sm.authorityId = a;
    broadcast(encode(sm), true, -1);
}

void Server::handleDamage(int slot, const uint8_t* data, size_t len) {
    DamageMsg d;
    if (!decodeDamageC2S(data, len, d)) return;
    Slot& s = slots_[slot];
    auto drop = [&](const char* why) {
        damageDropped_++;
        LOGI("Servidor: dano de '%s' descartado (%s)", s.nick.c_str(), why);
    };
    if (d.peer >= LBN_MAX_PLAYERS || !slots_[d.peer].used || d.peer == slot) { drop("alvo invalido"); return; }
    if (!(haveWorld_ && (world_.flags & WorldState::PvP))) { drop("PvP desligado pelo host"); return; }
    if (d.amountX10 == 0 || d.amountX10 > 2000) { drop("valor invalido"); return; }
    uint32_t now = nowMs();
    if (now - s.damageWindowStart >= 1000) { s.damageWindowStart = now; s.damageCount = 0; }
    if (++s.damageCount > 30) { drop("excesso de mensagens"); return; }
    const Slot& t = slots_[d.peer];
    if (s.hasPos && t.hasPos && (s.lastPos - t.lastPos).length() > 350.f) { drop("longe demais"); return; }
    DamageMsg out; out.peer = uint8_t(slot); out.amountX10 = d.amountX10; out.weapon = isValidWeapon(d.weapon) ? d.weapon : 0;
    sendTo(t.peer, encodeDamageS2C(out), true);
    damageRelayed_++;
    LOGI("Servidor: '%s' acertou '%s': %.1f de dano (%s)", s.nick.c_str(), t.nick.c_str(), d.amountX10 / 10.0,
         weaponNamePtBr(out.weapon));
}

void Server::handleWorld(int slot, const uint8_t* data, size_t len) {
    WorldState w;
    if (!decodeWorldC2S(data, len, w)) return;
    if (slot != authority_) return; // only the host decides the world
    bool rulesChanged = !haveWorld_ || (w.flags & WorldState::PvP) != (world_.flags & WorldState::PvP);
    world_ = w; haveWorld_ = true;
    if (rulesChanged) LOGI("Servidor: PvP %s (definido pelo host)", (w.flags & WorldState::PvP) ? "LIGADO" : "desligado");
    WorldS2CMsg m; m.authorityId = uint8_t(slot); m.w = w;
    broadcast(encode(m), true, slot);
}

void Server::broadcast(const std::vector<uint8_t>& data, bool reliable, int exceptId) {
    for (int i = 0; i < LBN_MAX_PLAYERS; ++i)
        if (slots_[i].used && i != exceptId) sendTo(slots_[i].peer, data, reliable);
}

void Server::broadcastSystem(const std::string& text) {
    broadcast(encodeChatS2C(kSystemSender, text), true, -1);
}

void Server::sendPings() {
    PingsMsg pm;
    for (int i = 0; i < LBN_MAX_PLAYERS; ++i)
        if (slots_[i].used) pm.entries.push_back({uint8_t(i), uint16_t(slots_[i].peer->roundTripTime > 65535 ? 65535 : slots_[i].peer->roundTripTime)});
    if (!pm.entries.empty()) broadcast(encode(pm), true, -1);
}

std::vector<ServerPlayerInfo> Server::players() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<ServerPlayerInfo> out;
    for (int i = 0; i < LBN_MAX_PLAYERS; ++i)
        if (slots_[i].used) out.push_back({uint8_t(i), slots_[i].nick, slots_[i].skin, slots_[i].peer->roundTripTime});
    return out;
}

} // namespace lbn
