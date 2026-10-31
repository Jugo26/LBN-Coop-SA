// End-to-end network tests over real UDP sockets on 127.0.0.1 (server + clients in one process).
#include "testfw.h"
#include "../src/common/net_client.h"
#include "../src/common/net_common.h"
#include "../src/common/net_server.h"
#include <enet/enet.h>
#include <functional>
#include <chrono>
#include <thread>
#include <vector>

using namespace lbn;

namespace {

uint16_t g_port = 47810;

// Pump server + clients until pred() or timeout.
bool pumpUntil(Server& s, std::vector<Client*> cs, std::function<bool()> pred, uint32_t timeoutMs,
               std::vector<std::vector<ClientEvent>>* sink) {
    uint32_t start = nowMs();
    while (nowMs() - start < timeoutMs) {
        s.service(1);
        for (size_t i = 0; i < cs.size(); ++i) {
            cs[i]->service(1);
            ClientEvent e;
            while (cs[i]->pollEvent(e)) if (sink) (*sink)[i].push_back(e);
        }
        if (pred()) return true;
    }
    return false;
}

size_t countType(const std::vector<ClientEvent>& v, ClientEvent::Type t) {
    size_t n = 0; for (auto& e : v) if (e.type == t) ++n; return n;
}

} // namespace

TEST(loopback_join_state_chat_leave) {
    Server srv; ServerOptions o; o.port = ++g_port; o.name = "Teste";
    CHECK(srv.start(o));
    Client a, b;
    std::vector<std::vector<ClientEvent>> ev(2);
    CHECK(a.connect("127.0.0.1", o.port, "Host", 106, 3000));
    CHECK(pumpUntil(srv, {&a}, [&] { return a.connected(); }, 3000, &ev));
    CHECK(b.connect("127.0.0.1", o.port, "Amigo", 107, 3000));
    CHECK(pumpUntil(srv, {&a, &b}, [&] { return b.connected() && countType(ev[0], ClientEvent::PlayerJoined) >= 1; }, 3000, &ev));
    CHECK(a.selfId() != b.selfId());
    // b learned about a
    bool bKnowsA = false;
    for (auto& e : ev[1]) if (e.type == ClientEvent::PlayerJoined && e.id == a.selfId() && e.nick == "Host") bKnowsA = true;
    CHECK(bKnowsA);

    PlayerState s; s.timeMs = nowMs(); s.pos = {1, 2, 3}; s.heading = 0.5f;
    s.flags = PlayerState::InVehicle; s.veh.model = 411; s.veh.key = 9; s.veh.pos = {1, 2, 3};
    CHECK(a.sendState(s));
    CHECK(pumpUntil(srv, {&a, &b}, [&] { return countType(ev[1], ClientEvent::State) >= 1; }, 2000, &ev));
    for (auto& e : ev[1]) if (e.type == ClientEvent::State) {
        CHECK(e.id == a.selfId()); CHECK(e.state.veh.model == 411); CHECK(e.state.pos.y == 2.f);
    }
    CHECK(countType(ev[0], ClientEvent::State) == 0); // no echo to sender

    CHECK(b.sendChat("oi host"));
    CHECK(pumpUntil(srv, {&a, &b}, [&] {
        for (auto& e : ev[0]) if (e.type == ClientEvent::Chat && e.text == "oi host" && e.id == b.selfId()) return true;
        return false; }, 2000, &ev));

    // Pings broadcast within ~1s
    CHECK(pumpUntil(srv, {&a, &b}, [&] { return countType(ev[1], ClientEvent::Pings) >= 1; }, 2500, &ev));

    uint8_t bid = b.selfId();
    b.disconnect(300);
    CHECK(pumpUntil(srv, {&a}, [&] {
        for (auto& e : ev[0]) if (e.type == ClientEvent::PlayerLeft && e.id == bid && e.leaveReason == LeaveReason::Quit) return true;
        return false; }, 3000, &ev));
    a.disconnect(200);
    srv.stop();
}

TEST(loopback_server_full_rejects) {
    Server srv; ServerOptions o; o.port = ++g_port; o.maxPlayers = 1;
    CHECK(srv.start(o));
    Client a, b;
    std::vector<std::vector<ClientEvent>> ev(2);
    CHECK(a.connect("127.0.0.1", o.port, "A", 106, 3000));
    CHECK(pumpUntil(srv, {&a}, [&] { return a.connected(); }, 3000, &ev));
    CHECK(b.connect("127.0.0.1", o.port, "B", 106, 3000));
    CHECK(pumpUntil(srv, {&a, &b}, [&] { return countType(ev[1], ClientEvent::Rejected) >= 1; }, 3000, &ev));
    CHECK(!b.connected());
    a.disconnect(100);
    srv.stop();
}

TEST(loopback_duplicate_nick_renamed) {
    Server srv; ServerOptions o; o.port = ++g_port;
    CHECK(srv.start(o));
    Client a, b;
    std::vector<std::vector<ClientEvent>> ev(2);
    a.connect("127.0.0.1", o.port, "LBN", 106, 3000);
    CHECK(pumpUntil(srv, {&a}, [&] { return a.connected(); }, 3000, &ev));
    b.connect("127.0.0.1", o.port, "LBN", 106, 3000);
    CHECK(pumpUntil(srv, {&a, &b}, [&] { return countType(ev[0], ClientEvent::PlayerJoined) >= 1; }, 3000, &ev));
    for (auto& e : ev[0]) if (e.type == ClientEvent::PlayerJoined) CHECK(e.nick == "LBN_2");
    CHECK(srv.duplicateNicks() == 1);
    bool gotAssigned = false;
    for (auto& e : ev[1]) if (e.type == ClientEvent::Connected) gotAssigned = (e.nick == "LBN_2");
    CHECK(gotAssigned);
    CHECK(pumpUntil(srv, {&a, &b}, [&] {
        for (auto& e : ev[1]) if (e.type == ClientEvent::Chat && e.text.find("ja estava em uso") != std::string::npos) return true;
        return false; }, 3000, &ev));
    a.disconnect(100); b.disconnect(100);
    srv.stop();
}

static bool helloRejectedAsBadVersion(HelloMsg hm, std::string* detailOut) {
    Server srv; ServerOptions o; o.port = ++g_port;
    CHECK(srv.start(o));
    // Raw ENet client sending a Hello with a wrong protocol version.
    CHECK(netInit());
    ENetHost* h = enet_host_create(nullptr, 1, kChanCount, 0, 0);
    ENetAddress addr{}; enet_address_set_host(&addr, "127.0.0.1"); addr.port = o.port;
    ENetPeer* p = enet_host_connect(h, &addr, kChanCount, 0);
    bool rejected = false, sent = false;
    uint32_t start = nowMs();
    while (nowMs() - start < 3000 && !rejected) {
        srv.service(1);
        ENetEvent ev;
        while (enet_host_service(h, &ev, 1) > 0) {
            if (ev.type == ENET_EVENT_TYPE_CONNECT && !sent) {
                sendTo(p, encode(hm), true); sent = true;
            } else if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
                RejectMsg rm;
                if (decode(ev.packet->data, ev.packet->dataLength, rm) && rm.reason == RejectReason::BadVersion) {
                    rejected = true; if (detailOut) *detailOut = rm.detail;
                }
                enet_packet_destroy(ev.packet);
            }
        }
    }
    enet_host_destroy(h);
    netShutdown();
    srv.stop();
    return rejected;
}

TEST(loopback_bad_version_rejected) {
    HelloMsg hm; hm.version = 99; hm.nick = "Velho";
    CHECK(helloRejectedAsBadVersion(hm, nullptr));
}

TEST(loopback_mod_version_mismatch_rejected) {
    // Same protocol, different mod version (e.g. friend did not update) -> must be refused.
    HelloMsg hm; hm.nick = "Amigo"; hm.modVersion = "0.0.9";
    std::string detail;
    CHECK(helloRejectedAsBadVersion(hm, &detail));
    CHECK(detail.find("host v" LBN_MOD_VERSION) != std::string::npos);
    CHECK(detail.find("voce v0.0.9") != std::string::npos);
}

TEST(loopback_old_protocol1_client_rejected) {
    HelloMsg hm; hm.version = 1; hm.nick = "Antigo";
    std::string detail;
    CHECK(helloRejectedAsBadVersion(hm, &detail));
    CHECK(detail.find("antiga/desconhecida") != std::string::npos);
}



TEST(loopback_v011_protocol2_client_rejected) {
    // A friend still on v0.1.1 (protocol 2) must get the clear "update" message.
    HelloMsg hm; hm.version = 2; hm.nick = "Amigo"; hm.modVersion = "0.1.1";
    std::string detail;
    CHECK(helloRejectedAsBadVersion(hm, &detail));
    CHECK(detail.find("host v" LBN_MOD_VERSION ", voce v0.1.1") != std::string::npos);
}

TEST(loopback_v012_client_rejected) {
    // v0.1.3 keeps protocol 3, so only the mod version tells a v0.1.2 friend to update.
    HelloMsg hm; hm.nick = "Amigo"; hm.modVersion = "0.1.2";
    std::string detail;
    CHECK(helloRejectedAsBadVersion(hm, &detail));
    CHECK(detail.find("host v" LBN_MOD_VERSION ", voce v0.1.2") != std::string::npos);
}

namespace {
bool hasDamageFrom(const std::vector<ClientEvent>& v, uint8_t from, uint16_t amt) {
    for (auto& e : v) if (e.type == ClientEvent::Damage && e.id == from && e.damage.amountX10 == amt) return true;
    return false;
}
}

TEST(loopback_world_authority_damage) {
    Server srv; ServerOptions o; o.port = ++g_port;
    CHECK(srv.start(o));
    Client a, b, c;
    std::vector<std::vector<ClientEvent>> ev(3);
    CHECK(a.connect("127.0.0.1", o.port, "Host", 0, 3000));
    CHECK(pumpUntil(srv, {&a}, [&] { return a.connected() && countType(ev[0], ClientEvent::Session) >= 1; }, 3000, &ev));
    CHECK(srv.authorityId() == a.selfId());
    CHECK(b.connect("127.0.0.1", o.port, "Amigo", 0, 3000));
    CHECK(c.connect("127.0.0.1", o.port, "Amigo2", 0, 3000));
    CHECK(pumpUntil(srv, {&a, &b, &c}, [&] { return b.connected() && c.connected() &&
        countType(ev[1], ClientEvent::Session) >= 1 && countType(ev[2], ClientEvent::Session) >= 1; }, 3000, &ev));
    for (auto& e : ev[1]) if (e.type == ClientEvent::Session) CHECK(e.id == a.selfId());

    // Weapon sanitizing: invalid weapon id 20 is relayed as 0, valid 24 kept, wanted kept.
    PlayerState s; s.timeMs = nowMs(); s.pos = {1, 2, 3}; s.weapon = 20; s.wanted = 2;
    CHECK(b.sendState(s));
    s.seq = 1; s.weapon = 24; s.pos = {1.5f, 2, 3};
    CHECK(pumpUntil(srv, {&a, &b, &c}, [&] { return countType(ev[0], ClientEvent::State) >= 1; }, 2000, &ev));
    CHECK(b.sendState(s));
    CHECK(pumpUntil(srv, {&a, &b, &c}, [&] { return countType(ev[0], ClientEvent::State) >= 2; }, 2000, &ev));
    { int n = 0; for (auto& e : ev[0]) if (e.type == ClientEvent::State) { CHECK(e.state.weapon == (n == 0 ? 0 : 24)); CHECK(e.state.wanted == 2); ++n; } }

    // Damage before the host enabled PvP is dropped.
    DamageMsg d; d.peer = c.selfId(); d.amountX10 = 250; d.weapon = 24;
    CHECK(b.sendDamage(d));
    CHECK(pumpUntil(srv, {&a, &b, &c}, [&] { return srv.damageDropped() >= 1; }, 2000, &ev));

    // A non-authority cannot change the world.
    WorldState w; w.hour = 3; w.weather = 9; w.flags |= WorldState::PvP;
    CHECK(b.sendWorld(w));
    pumpUntil(srv, {&a, &b, &c}, [] { return false; }, 300, &ev);
    CHECK(countType(ev[2], ClientEvent::World) == 0);

    // Host world goes to everybody else (not echoed to the host).
    w.hour = 21; w.minute = 30; w.weather = 16; w.wanted = 2;
    CHECK(a.sendWorld(w));
    CHECK(pumpUntil(srv, {&a, &b, &c}, [&] { return countType(ev[1], ClientEvent::World) >= 1 && countType(ev[2], ClientEvent::World) >= 1; }, 2000, &ev));
    for (auto& e : ev[2]) if (e.type == ClientEvent::World) { CHECK(e.world.hour == 21 && e.world.weather == 16 && (e.world.flags & WorldState::PvP)); CHECK(e.id == a.selfId()); }
    CHECK(countType(ev[0], ClientEvent::World) == 0);

    // With PvP on, damage reaches only the target, tagged with the attacker id.
    CHECK(b.sendDamage(d));
    CHECK(pumpUntil(srv, {&a, &b, &c}, [&] { return hasDamageFrom(ev[2], b.selfId(), 250); }, 2000, &ev));
    CHECK(countType(ev[0], ClientEvent::Damage) == 0 && countType(ev[1], ClientEvent::Damage) == 0);
    CHECK(srv.damageRelayed() == 1);
    // Self damage, bogus target and absurd values are dropped.
    uint32_t dropped = srv.damageDropped();
    DamageMsg bad = d; bad.peer = b.selfId(); CHECK(b.sendDamage(bad));
    bad.peer = 4; CHECK(b.sendDamage(bad));
    bad.peer = c.selfId(); bad.amountX10 = 5000; CHECK(b.sendDamage(bad));
    CHECK(pumpUntil(srv, {&a, &b, &c}, [&] { return srv.damageDropped() >= dropped + 3; }, 2000, &ev));
    CHECK(srv.damageRelayed() == 1);

    // A late joiner receives the last host world immediately.
    Client late; std::vector<std::vector<ClientEvent>> ev2(1);
    c.disconnect(200);
    CHECK(late.connect("127.0.0.1", o.port, "Atrasado", 0, 3000));
    CHECK(pumpUntil(srv, {&late}, [&] { return countType(ev2[0], ClientEvent::World) >= 1; }, 3000, &ev2));
    for (auto& e : ev2[0]) if (e.type == ClientEvent::World) CHECK(e.world.hour == 21);

    // Host leaves -> the lowest remaining id becomes the authority and everybody is told.
    uint8_t bid = b.selfId();
    a.disconnect(300);
    CHECK(pumpUntil(srv, {&b, &late}, [&] { return srv.authorityId() == bid; }, 3000, nullptr));
    b.disconnect(200); late.disconnect(200);
    srv.stop();
}

TEST(loopback_timeout_detected) {
    Server srv; ServerOptions o; o.port = ++g_port; o.timeoutMs = 1500;
    CHECK(srv.start(o));
    Client a, b;
    std::vector<std::vector<ClientEvent>> ev(2);
    a.connect("127.0.0.1", o.port, "A", 106, 3000);
    b.connect("127.0.0.1", o.port, "B", 106, 3000);
    CHECK(pumpUntil(srv, {&a, &b}, [&] { return a.connected() && b.connected(); }, 3000, &ev));
    uint8_t bid = b.selfId();
    // Simulate a crash/freeze of b: stop servicing it entirely.
    CHECK(pumpUntil(srv, {&a}, [&] {
        for (auto& e : ev[0]) if (e.type == ClientEvent::PlayerLeft && e.id == bid && e.leaveReason == LeaveReason::Timeout) return true;
        return false; }, 8000, &ev));
    a.disconnect(100);
    srv.stop();
}

TEST(loopback_threaded_server_and_connect_failure) {
    Server srv; ServerOptions o; o.port = ++g_port;
    CHECK(srv.startThreaded(o));
    Client a;
    CHECK(a.connect("127.0.0.1", o.port, "A", 106, 3000));
    uint32_t start = nowMs();
    while (!a.connected() && nowMs() - start < 3000) a.service(5);
    CHECK(a.connected());
    auto pl = srv.players();
    if (pl.size() != 1) std::printf("  players=%zu\n", pl.size());
    CHECK(pl.size() == 1);
    a.disconnect(200);
    srv.stopThreaded();

    // Nobody listening on this port => ConnectFailed eventually.
    Client c;
    CHECK(c.connect("127.0.0.1", uint16_t(o.port + 500), "C", 106, 1500));
    bool failed = false;
    start = nowMs();
    while (!failed && nowMs() - start < 8000) {
        c.service(10);
        ClientEvent e;
        while (c.pollEvent(e)) if (e.type == ClientEvent::ConnectFailed) failed = true;
    }
    CHECK(failed);
}

TEST(loopback_async_client_survives_main_thread_stall) {
    // The game's main loop stops calling us while paused; the async client must keep the link alive.
    Server srv; ServerOptions o; o.port = ++g_port; o.timeoutMs = 1500;
    CHECK(srv.startThreaded(o));
    AsyncClient a, b;
    CHECK(a.connect("127.0.0.1", o.port, "A", 106, 3000));
    CHECK(b.connect("127.0.0.1", o.port, "B", 106, 3000));
    uint32_t start = nowMs();
    while ((!a.connected() || !b.connected()) && nowMs() - start < 3000) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(a.connected() && b.connected());
    // "Pause" for 3x the timeout without touching the clients from this thread.
    std::this_thread::sleep_for(std::chrono::milliseconds(4500));
    CHECK(a.connected() && b.connected());
    PlayerState s; s.timeMs = nowMs(); s.pos = {7, 8, 9};
    CHECK(a.sendState(s));
    bool got = false;
    start = nowMs();
    while (!got && nowMs() - start < 2000) {
        ClientEvent e;
        while (b.pollEvent(e)) if (e.type == ClientEvent::State && e.state.pos.z == 9.f) got = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(got);
    a.disconnect(); b.disconnect();
    srv.stopThreaded();
}
