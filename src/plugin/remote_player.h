// A remote player's in-game avatar (ped + optional vehicle). Original code for LBN Coop SA.
#pragma once
#include <cstdint>
#include <string>
#include "../common/interp.h"

class RemotePlayer;

// Process-wide avatar options (from the ini).
struct RemoteOptions {
    bool pedCollision = false; // false: avatars never collide with (block, push, trap) the local player
    bool pvp = false;          // set from the host's world state: avatars can be hurt by the local player
    // Called when the LOCAL player damaged this avatar (amount in health points).
    void (*onHit)(uint8_t victimId, float amount, uint8_t weapon) = nullptr;
};
// v0.1.2: with PvP on, an avatar keeps collision (needed so bullets hit it) only while it is farther
// than this from the local player; closer than that it is always collision-less (never traps/pushes).
constexpr float kPvpCollisionMinDist = 1.6f;
constexpr float kAvatarHealth = 1000.f;  // avatars keep this much health; drops are reported as hits
RemoteOptions& remoteOptions();

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
    uint8_t armour() const { return armour_; }
    uint8_t weapon() const { return weaponState_; }
    uint8_t wanted() const { return wanted_; }
    bool dead() const { return hasState() && health_ <= 0.f; }

private:
    bool ensurePed(const lbn::Vec3& pos);
    bool ensureVehicle(const lbn::PlayerState& s, void* localVehicle);
    void dropVehicle(void* localVehicle);
    void applyOnFoot(const lbn::PlayerState& s, void* localPed);
    void applyAnim(lbn::MoveAnim a);
    void enforceNoCollision(void* ped, void* localPed);
    void applyCombatMode();
    void applyWeapon();
    void detectHits(void* ped, const WorldLookup& world);
    void syncVehicleHealth(void* veh, const lbn::PlayerState& s);
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
    uint8_t armour_ = 0;
    uint8_t weaponState_ = 0;   // weapon the owner holds (latest state)
    uint8_t wanted_ = 0;
    int weaponApplied_ = -1;    // weapon given to our ped (-1 = none yet)
    int combatMode_ = -1;       // 1 = PvP proofs applied, 0 = invulnerable, -1 = unknown
    uint32_t lastWeaponFailMs_ = 0;
    bool vehExploded_ = false;
};
