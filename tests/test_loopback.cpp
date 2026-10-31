// End-to-end network tests over real UDP sockets on 127.0.0.1 (server + clients in one process).
#include "testfw.h"
#include "../src/common/net_client.h"
#include "../src/common/net_common.h"
#include "../src/common/net_server.h"
#include <enet/enet.h>
#include <functional>
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
    a.disconnect(100); b.disconnect(100);
    srv.stop();
}

TEST(loopback_bad_version_rejected) {
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
                HelloMsg hm; hm.version = 99; hm.nick = "Velho";
                sendTo(p, encode(hm), true); sent = true;
            } else if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
                RejectMsg rm;
                if (decode(ev.packet->data, ev.packet->dataLength, rm) && rm.reason == RejectReason::BadVersion) rejected = true;
                enet_packet_destroy(ev.packet);
            }
        }
    }
    CHECK(rejected);
    enet_host_destroy(h);
    netShutdown();
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
