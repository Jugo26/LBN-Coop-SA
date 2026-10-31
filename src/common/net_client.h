// LBN Coop SA network client session (original code). Game-agnostic so it can be unit tested.
#pragma once
#include <cstdint>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <string>
#include "protocol.h"

typedef struct _ENetHost ENetHost;
typedef struct _ENetPeer ENetPeer;

namespace lbn {

struct ClientEvent {
    enum Type {
        Connected,      // welcome received; selfId valid
        ConnectFailed,  // could not reach host
        Rejected,       // server refused; text = reason
        Disconnected,   // lost connection or server closed; text = reason
        PlayerJoined,   // id, nick, skin
        PlayerLeft,     // id, leaveReason
        State,          // id, state, recvMs
        Chat,           // id (0xFF = system), text
        Pings,          // pings
        Damage,         // id = attacker, damage
        World,          // world (from the authority), id = authority id
        Session,        // id = authority id (the "host" deciding world state)
    };
    ClientEvent() = default;
    explicit ClientEvent(Type t) : type(t) {}
    Type type = Chat;
    uint8_t id = 0;
    std::string nick;
    std::string text;
    uint16_t skin = 0;
    LeaveReason leaveReason = LeaveReason::Quit;
    PlayerState state;
    uint32_t recvMs = 0;
    PingsMsg pings;
    DamageMsg damage;
    WorldState world;
};

class Client {
public:
    enum class Status { Idle, Connecting, Handshake, Connected };

    Client() = default;
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    bool connect(const std::string& host, uint16_t port, const std::string& nick, uint16_t skin,
                 uint32_t timeoutMs = 10000);
    // Graceful disconnect; blocks at most `flushMs`.
    void disconnect(uint32_t flushMs = 200);
    void service(uint32_t waitMs = 0);
    bool pollEvent(ClientEvent& out);

    bool sendState(const PlayerState& s);
    bool sendChat(const std::string& text);
    bool sendDamage(const DamageMsg& d);
    bool sendWorld(const WorldState& w);

    Status status() const { return status_; }
    bool connected() const { return status_ == Status::Connected; }
    uint8_t selfId() const { return selfId_; }
    uint32_t pingMs() const;
    const std::string& serverName() const { return serverName_; }

private:
    void push(ClientEvent e) { events_.push_back(std::move(e)); }
    void handlePacket(const uint8_t* data, size_t len);
    void teardown();

    ENetHost* host_ = nullptr;
    ENetPeer* peer_ = nullptr;
    Status status_ = Status::Idle;
    uint8_t selfId_ = 0xFF;
    std::string nick_;
    uint16_t skin_ = 0;
    std::string serverName_;
    uint32_t connectStart_ = 0;
    uint32_t timeoutMs_ = 10000;
    std::deque<ClientEvent> events_;
    bool netRef_ = false;
    bool rejected_ = false;
};

// Thread-safe wrapper that services the ENet client on a background thread, so the
// connection keeps alive even when the game loop stops calling us (pause menu, loading).
class AsyncClient {
public:
    AsyncClient() = default;
    ~AsyncClient() { stop(); }
    bool connect(const std::string& host, uint16_t port, const std::string& nick, uint16_t skin, uint32_t timeoutMs);
    void disconnect(uint32_t flushMs = 200);
    bool pollEvent(ClientEvent& out);
    bool sendState(const PlayerState& s);
    bool sendChat(const std::string& text);
    bool sendDamage(const DamageMsg& d);
    bool sendWorld(const WorldState& w);
    Client::Status status();
    bool connected() { return status() == Client::Status::Connected; }
    uint8_t selfId();
    uint32_t pingMs();
private:
    void stop();
    std::mutex mu_;
    Client client_;
    std::thread thread_;
    std::atomic<bool> run_{false};
};

} // namespace lbn
