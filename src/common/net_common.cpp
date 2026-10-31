// Shared ENet helpers (original code for LBN Coop SA).
#include "net_common.h"
#include "protocol.h"
#include <enet/enet.h>
#include <chrono>
#include <mutex>

namespace lbn {

namespace { std::mutex g_mu; int g_refs = 0; }

bool netInit() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_refs == 0 && enet_initialize() != 0) return false;
    ++g_refs;
    return true;
}

void netShutdown() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_refs > 0 && --g_refs == 0) enet_deinitialize();
}

uint32_t nowMs() {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return uint32_t(duration_cast<milliseconds>(steady_clock::now() - t0).count());
}

bool sendTo(ENetPeer* peer, const std::vector<uint8_t>& data, bool reliable) {
    if (!peer || data.empty()) return false;
    ENetPacket* pk = enet_packet_create(data.data(), data.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    if (!pk) return false;
    if (enet_peer_send(peer, reliable ? kChanReliable : kChanState, pk) != 0) {
        enet_packet_destroy(pk);
        return false;
    }
    return true;
}

} // namespace lbn
