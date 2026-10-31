// Shared ENet helpers (original code for LBN Coop SA).
#pragma once
#include <cstdint>
#include <vector>

typedef struct _ENetPeer ENetPeer;

namespace lbn {

constexpr uint32_t kQuitCode = 0x4C42; // graceful disconnect marker ("LB")

bool netInit();     // ref-counted enet_initialize
void netShutdown(); // ref-counted enet_deinitialize
uint32_t nowMs();   // monotonic milliseconds

// Sends a payload to a peer. reliable=false => unreliable sequenced on channel 1.
bool sendTo(ENetPeer* peer, const std::vector<uint8_t>& data, bool reliable);

} // namespace lbn
