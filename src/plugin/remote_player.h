// A remote player's in-game avatar (ped + optional vehicle). Original code for LBN Coop SA.
#pragma once
#include <cstdint>
#include <string>
#include "../common/interp.h"

class RemotePlayer;

// Lets an avatar look up vehicles owned by other avatars or by the local player.
struct WorldLookup {
    uint8_t selfId = 0xFF;
    void* localPed = nullptr;
    void* localVehicle = nullptr;
    void* (*vehicleOfPlayer)(uint8_t id, void* ctx) = nullptr; // remote copy owned by player id
    void* ctx = nullptr;
};

class RemotePlayer {
public:
    RemotePlayer(uint8_t id, std::string nick, uint16_t skin, const lbn::InterpConfig& cfg);
    ~RemotePlayer();

    void pushState(const lbn::PlayerState& s, uint32_t recvMs);
    // Main-thread, once per frame while the world is ready.
    void update(uint32_t nowMs, const WorldLookup& world);
    // Delete our entities (call while the world is valid).
    void destroyEntities(void* localVehicle);
    // Forget handles without touching the game (after the world was reset/reloaded).
    void forgetEntities();

    uint8_t id() const { return id_; }
    const std::string& nick() const { return nick_; }
    uint16_t pingMs() const { return ping_; }
    void setPing(uint16_t p) { ping_ = p; }
    uint32_t lastStateMs() const { return lastRecv_; }
    bool hasState() const { return lastRecv_ != 0; }
    void* ped() const;
    void* vehicle() const;   // our remote copy of this player's vehicle (may be null)
    bool nametagPos(lbn::Vec3& out) const;
    float health() const { return health_; }

private:
    bool ensurePed(const lbn::Vec3& pos);
    bool ensureVehicle(const lbn::PlayerState& s, void* localVehicle);
    void dropVehicle(void* localVehicle);
    void applyOnFoot(const lbn::PlayerState& s);
    void applyAnim(lbn::MoveAnim a);
    void warpInto(void* veh, int vehHandle, int seat, uint32_t nowMs);
    void warpOut(const lbn::Vec3& pos, uint32_t nowMs);

    uint8_t id_;
    std::string nick_;
    uint16_t skin_;
    lbn::Interpolator interp_;
    int pedHandle_ = -1;
    int vehHandle_ = -1;
    uint32_t vehKey_ = 0;
    uint16_t vehModel_ = 0;
    uint8_t vehColor1_ = 0xFF, vehColor2_ = 0xFF;
    lbn::MoveAnim anim_ = lbn::MoveAnim::Idle;
    bool animValid_ = false;
    uint32_t lastRecv_ = 0;
    uint32_t lastWarpMs_ = 0;
    uint32_t lastCreateFailMs_ = 0;
    uint16_t ping_ = 0;
    float health_ = 100.f;
};
