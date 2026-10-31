// LBN Coop SA relay server (original code). Usable standalone or embedded in the host's game.
#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "protocol.h"

typedef struct _ENetHost ENetHost;
typedef struct _ENetPeer ENetPeer;

namespace lbn {

struct ServerOptions {
    std::string bindAddress;  // empty = any
    uint16_t port = LBN_DEFAULT_PORT;
    int maxPlayers = LBN_MAX_PLAYERS;
    std::string name = "LBN Coop SA";
    uint32_t timeoutMs = 10000;
};

struct ServerPlayerInfo {
    uint8_t id;
    std::string nick;
    uint16_t skin;
    uint32_t pingMs;
};

class Server {
public:
    Server() = default;
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool start(const ServerOptions& opt);
    void stop();
    bool running() const { return host_ != nullptr; }
    void service(uint32_t waitMs);

    // Runs service() on a background thread.
    bool startThreaded(const ServerOptions& opt);
    void stopThreaded();

    std::vector<ServerPlayerInfo> players() const;
    uint64_t relayedStates() const { return relayed_; }

private:
    struct Slot {
        bool used = false;
        ENetPeer* peer = nullptr;
        std::string nick;
        uint16_t skin = 0;
        uint32_t stateCount = 0;
        uint32_t stateWindowStart = 0;
    };
    void onConnect(ENetPeer* peer);
    void onReceive(ENetPeer* peer, const uint8_t* data, size_t len);
    void onDisconnect(ENetPeer* peer, bool graceful);
    void handleHello(ENetPeer* peer, const uint8_t* data, size_t len);
    void broadcast(const std::vector<uint8_t>& data, bool reliable, int exceptId);
    void broadcastSystem(const std::string& text);
    void sendPings();
    int slotOf(ENetPeer* peer) const;
    std::string uniqueNick(const std::string& base) const;

    ServerOptions opt_;
    ENetHost* host_ = nullptr;
    Slot slots_[LBN_MAX_PLAYERS];
    uint32_t lastPings_ = 0;
    std::atomic<uint64_t> relayed_{0};
    mutable std::mutex mu_;
    std::thread thread_;
    std::atomic<bool> threadRun_{false};
    bool netRef_ = false;
};

} // namespace lbn
