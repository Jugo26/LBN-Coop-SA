// Headless test client for LBN Coop SA (original code). Simulates a player walking in a circle,
// then driving, and sends a chat line. Prints what it receives. Exit code 0 when it saw at least one
// other player's state and chat; 1 otherwise.
// Usage: lbn_testclient <host> <port> <nick> <seconds> [phase-offset]
#include "../common/interp.h"
#include "../common/log.h"
#include "../common/net_client.h"
#include "../common/net_common.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "uso: %s <host> <porta> <nick> <segundos> [fase]\n", argv[0]);
        return 2;
    }
    std::string host = argv[1];
    uint16_t port = uint16_t(std::atoi(argv[2]));
    std::string nick = argv[3];
    double seconds = std::atof(argv[4]);
    float phase = argc > 5 ? float(std::atof(argv[5])) : 0.f;

    lbn::logOpen("lbn_testclient_" + nick + ".log", true);
    lbn::Client c;
    if (!c.connect(host, port, nick, 106, 5000)) return 1;

    std::map<int, lbn::Interpolator> interps;
    std::map<int, std::string> names;
    int statesSeen = 0, chatsSeen = 0, interpSamples = 0;
    bool chatSent = false;
    uint32_t start = lbn::nowMs(), lastSend = 0, seq = 0;
    bool failed = false;

    while (lbn::nowMs() - start < uint32_t(seconds * 1000)) {
        c.service(5);
        lbn::ClientEvent e;
        while (c.pollEvent(e)) {
            switch (e.type) {
            case lbn::ClientEvent::Connected: std::printf("[%s] CONECTADO id=%u\n", nick.c_str(), e.id); break;
            case lbn::ClientEvent::ConnectFailed: std::printf("[%s] FALHA: %s\n", nick.c_str(), e.text.c_str()); failed = true; break;
            case lbn::ClientEvent::Rejected: std::printf("[%s] RECUSADO: %s\n", nick.c_str(), e.text.c_str()); failed = true; break;
            case lbn::ClientEvent::Disconnected: std::printf("[%s] DESCONECTADO: %s\n", nick.c_str(), e.text.c_str()); failed = true; break;
            case lbn::ClientEvent::PlayerJoined: names[e.id] = e.nick; std::printf("[%s] JOIN id=%u nick=%s skin=%u\n", nick.c_str(), e.id, e.nick.c_str(), e.skin); break;
            case lbn::ClientEvent::PlayerLeft: std::printf("[%s] LEAVE id=%u\n", nick.c_str(), e.id); interps.erase(e.id); break;
            case lbn::ClientEvent::State:
                statesSeen++;
                interps[e.id].push(e.state, e.recvMs);
                break;
            case lbn::ClientEvent::Chat:
                chatsSeen++;
                std::printf("[%s] CHAT de %u: %s\n", nick.c_str(), e.id, e.text.c_str());
                break;
            case lbn::ClientEvent::Pings:
                for (auto& p : e.pings.entries) std::printf("[%s] PING id=%u %ums\n", nick.c_str(), p.id, p.pingMs);
                break;
            case lbn::ClientEvent::Damage: std::printf("[%s] DANO de %u: %.1f\n", nick.c_str(), e.id, e.damage.amountX10 / 10.0); break;
            case lbn::ClientEvent::World: std::printf("[%s] MUNDO %02u:%02u clima %d\n", nick.c_str(), e.world.hour, e.world.minute, e.world.weather); break;
            case lbn::ClientEvent::Session: std::printf("[%s] AUTORIDADE id=%u\n", nick.c_str(), e.id); break;
            }
        }
        if (failed) break;
        uint32_t now = lbn::nowMs();
        if (c.connected() && now - lastSend >= 50) {
            lastSend = now;
            float t = (now - start) / 1000.f;
            lbn::PlayerState s;
            s.seq = ++seq;
            s.timeMs = now;
            float w = 0.5f, r = 10.f;
            s.pos = {2495.f + r * std::cos(w * t + phase), -1665.f + r * std::sin(w * t + phase), 13.3f};
            s.vel = {-r * w * std::sin(w * t + phase), r * w * std::cos(w * t + phase), 0};
            s.heading = lbn::forwardToHeading(s.vel);
            s.anim = lbn::MoveAnim::Run;
            if (std::fmod(t, 6.f) > 3.f) { // alternate: in a vehicle
                s.flags = lbn::PlayerState::InVehicle;
                s.seat = 0;
                s.veh.key = 1234; s.veh.model = 560; s.veh.pos = s.pos; s.veh.vel = s.vel;
                s.veh.rot = lbn::quatFromAxes({std::cos(s.heading), std::sin(s.heading), 0},
                                              lbn::headingToForward(s.heading), {0, 0, 1});
            }
            c.sendState(s);
            if (!chatSent && t > 1.0f) { c.sendChat("Ola de " + nick + "! acentuação ok?"); chatSent = true; }
        }
        for (auto& kv : interps) {
            auto r = kv.second.sample(now);
            if (r.valid) interpSamples++;
        }
    }
    for (auto& kv : interps) {
        auto r = kv.second.sample(lbn::nowMs());
        std::printf("[%s] remoto %d (%s): pos=(%.2f, %.2f, %.2f) veiculo=%d snapshots=%zu offset=%d\n", nick.c_str(),
                    kv.first, names[kv.first].c_str(), r.state.pos.x, r.state.pos.y, r.state.pos.z,
                    r.state.inVehicle() ? r.state.veh.model : 0, kv.second.size(), kv.second.clock().offset());
    }
    std::printf("[%s] RESUMO estados=%d chats=%d amostras=%d ping=%ums\n", nick.c_str(), statesSeen, chatsSeen,
                interpSamples, c.pingMs());
    c.disconnect();
    lbn::logClose();
    return (!failed && statesSeen > 0 && chatsSeen > 0) ? 0 : 1;
}
