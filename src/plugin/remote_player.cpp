// Remote player avatar implementation (original code for LBN Coop SA).
#include "remote_player.h"
#include "../common/log.h"
#include "game.h"
#include "script.h"

using lbn::MoveAnim;
using lbn::PlayerState;
using lbn::Vec3;

namespace {
constexpr int kPedTypeMission1 = 24;
constexpr uint32_t kWarpRetryMs = 600;
constexpr uint32_t kCreateRetryMs = 2000;
}

RemoteOptions& remoteOptions() { static RemoteOptions o; return o; }

RemotePlayer::RemotePlayer(uint8_t id, std::string nick, uint16_t skin, const lbn::InterpConfig& cfg)
    : id_(id), nick_(std::move(nick)), skin_(skin), interp_(cfg) {}

RemotePlayer::~RemotePlayer() = default; // entities are released explicitly by the controller

void RemotePlayer::pushState(const PlayerState& s, uint32_t recvMs) {
    interp_.push(s, recvMs);
    lastRecv_ = recvMs;
    health_ = s.health;
    armour_ = s.armour;
    weaponState_ = lbn::isValidWeapon(s.weapon) ? s.weapon : 0;
    wanted_ = s.wanted;
}

void* RemotePlayer::ped() const { return sa::pedFromHandle(pedHandle_); }
void* RemotePlayer::vehicle() const {
    void* v = sa::vehicleFromHandle(vehHandle_);
    return (v && sa::modelOf(v) == int16_t(vehModel_)) ? v : nullptr;
}

bool RemotePlayer::nametagPos(Vec3& out) const {
    void* p = ped();
    if (!p || !sa::entityPos(p, out)) return false;
    out.z += 1.15f;
    return true;
}

bool RemotePlayer::ensurePed(const Vec3& pos) {
    void* p = sa::pedFromHandle(pedHandle_);
    if (p && sa::modelOf(p) == int16_t(skin_)) return true;
    pedHandle_ = -1;
    animValid_ = false;
    uint32_t now = sa::gameTimeMs();
    if (lastCreateFailMs_ && now - lastCreateFailMs_ < kCreateRetryMs) return false;
    if (!scm::loadModelNow(skin_)) {
        LOGW("Nao consegui carregar o modelo de ped %u para %s", unsigned(skin_), nick_.c_str());
        lastCreateFailMs_ = now ? now : 1;
        return false;
    }
    pedHandle_ = scm::createChar(kPedTypeMission1, skin_, pos.x, pos.y, pos.z);
    scm::releaseModel(skin_);
    if (pedHandle_ < 0) {
        LOGW("Falha ao criar o boneco de %s", nick_.c_str());
        lastCreateFailMs_ = now ? now : 1;
        return false;
    }
    lastCreateFailMs_ = 0;
    weaponApplied_ = -1;
    combatMode_ = -1;
    // Proofs/targetting depend on the host's PvP rule (applyCombatMode); health is kept high and every
    // drop is reported to the owner as a hit (the owner's game applies the real damage).
    scm::Call(scm::SET_CHAR_HEALTH).i(pedHandle_).i(int(kAvatarHealth)).run();
    scm::Call(scm::SET_CHAR_ONLY_DAMAGED_BY_PLAYER).i(pedHandle_).i(1).run();
    scm::Call(scm::SET_CHAR_SUFFERS_CRITICAL_HITS).i(pedHandle_).i(0).run();
    scm::Call(scm::SET_CHAR_CANT_BE_DRAGGED_OUT).i(pedHandle_).i(1).run();
    applyCombatMode();
    scm::Call(scm::SET_CHAR_DROPS_WEAPONS_WHEN_DEAD).i(pedHandle_).i(0).run();
    // v0.1.1: the avatar must never block, push or trap the local player (it is moved by the network
    // anyway). SET_CHAR_COLLISION is the game's own script command (CEntity::bUsesCollision).
    if (!remoteOptions().pedCollision) scm::Call(scm::SET_CHAR_COLLISION).i(pedHandle_).i(0).run();
    LOGI("Boneco de %s criado (handle %d, skin %u, colisao %s)", nick_.c_str(), pedHandle_, unsigned(skin_),
         remoteOptions().pedCollision ? "ligada" : "desligada");
    return true;
}

void RemotePlayer::dropVehicle(void* localVehicle) {
    void* v = vehicle();
    if (v) {
        void* p = ped();
        if (p && sa::pedVehicle(p) == v) {
            Vec3 pos; sa::entityPos(v, pos);
            scm::Call(scm::WARP_CHAR_FROM_CAR_TO_COORD).i(pedHandle_).f(pos.x + 2.f).f(pos.y).f(pos.z).run();
        }
        // Never delete a car the local player is sitting in; hand it to the game instead.
        if (v == localVehicle) scm::releaseCar(vehHandle_);
        else scm::deleteCar(vehHandle_);
    }
    vehHandle_ = -1; vehKey_ = 0; vehModel_ = 0; vehColor1_ = vehColor2_ = 0xFF;
}

bool RemotePlayer::ensureVehicle(const PlayerState& s, void* localVehicle) {
    void* v = vehicle();
    if (v && vehKey_ == s.veh.key && vehModel_ == s.veh.model) return true;
    if (vehHandle_ >= 0) dropVehicle(localVehicle);
    if (!lbn::isSpawnableVehicleModel(s.veh.model)) return false;
    uint32_t now = sa::gameTimeMs();
    if (lastCreateFailMs_ && now - lastCreateFailMs_ < kCreateRetryMs) return false;
    if (!scm::loadModelNow(s.veh.model)) {
        LOGW("Nao consegui carregar o veiculo %u para %s", unsigned(s.veh.model), nick_.c_str());
        lastCreateFailMs_ = now ? now : 1;
        return false;
    }
    vehHandle_ = scm::createCar(s.veh.model, s.veh.pos.x, s.veh.pos.y, s.veh.pos.z);
    scm::releaseModel(s.veh.model);
    if (vehHandle_ < 0) {
        LOGW("Falha ao criar o veiculo %u de %s", unsigned(s.veh.model), nick_.c_str());
        lastCreateFailMs_ = now ? now : 1;
        return false;
    }
    vehKey_ = s.veh.key;
    vehModel_ = s.veh.model;
    vehExploded_ = false;
    scm::Call(scm::SET_CAR_PROOFS).i(vehHandle_).i(1).i(1).i(1).i(1).i(1).run();
    scm::Call(scm::SET_CAR_ENGINE_ON).i(vehHandle_).i(1).run();
    LOGI("Veiculo %u de %s criado (handle %d)", unsigned(vehModel_), nick_.c_str(), vehHandle_);
    return true;
}

void RemotePlayer::warpInto(void* veh, int vehHandle, int seat, uint32_t nowMs) {
    void* p = ped();
    if (!p || !veh) return;
    if (sa::pedVehicle(p) == veh && sa::pedSeat(p, veh) == seat) return;
    if (lastWarpMs_ && nowMs - lastWarpMs_ < kWarpRetryMs) return;
    lastWarpMs_ = nowMs;
    if (sa::pedVehicle(p) && sa::pedVehicle(p) != veh) {
        Vec3 pos; sa::entityPos(p, pos);
        scm::Call(scm::WARP_CHAR_FROM_CAR_TO_COORD).i(pedHandle_).f(pos.x).f(pos.y).f(pos.z).run();
    }
    if (seat <= 0) {
        scm::Call(scm::WARP_CHAR_INTO_CAR).i(pedHandle_).i(vehHandle).run();
    } else {
        int maxPass = sa::at<uint8_t>(veh, sa::off::vehMaxPassengers);
        if (seat - 1 >= maxPass) return;
        void** pass = &sa::at<void*>(veh, sa::off::vehPassengers);
        if (pass[seat - 1] && pass[seat - 1] != p) return; // seat taken locally; don't fight over it
        scm::Call(scm::WARP_CHAR_INTO_CAR_AS_PASSENGER).i(pedHandle_).i(vehHandle).i(seat - 1).run();
    }
    animValid_ = false;
}

void RemotePlayer::warpOut(const Vec3& pos, uint32_t nowMs) {
    void* p = ped();
    if (!p || !sa::pedVehicle(p)) return;
    if (lastWarpMs_ && nowMs - lastWarpMs_ < kWarpRetryMs) return;
    lastWarpMs_ = nowMs;
    scm::Call(scm::WARP_CHAR_FROM_CAR_TO_COORD).i(pedHandle_).f(pos.x).f(pos.y).f(pos.z).run();
    animValid_ = false;
}

void RemotePlayer::applyAnim(MoveAnim a) {
    if (animValid_ && a == anim_) return;
    anim_ = a;
    animValid_ = true;
    const char* name = nullptr;
    switch (a) {
    case MoveAnim::Walk: name = "WALK_player"; break;
    case MoveAnim::Run: name = "run_player"; break;
    case MoveAnim::Sprint: name = "sprint_civi"; break;
    default: break;
    }
    if (!name) {
        scm::Call(scm::CLEAR_CHAR_TASKS).i(pedHandle_).run();
        return;
    }
    // TASK_PLAY_ANIM char anim ifp blend loop lockX lockY lockF time
    scm::Call(scm::TASK_PLAY_ANIM).i(pedHandle_).s(name).s("PED").f(4.0f).i(1).i(0).i(0).i(0).i(-1).run();
}

void RemotePlayer::enforceNoCollision(void* p, void* localPed) {
    if (remoteOptions().pedCollision || !p) return;
    // PvP needs collision for bullets to hit the avatar (CWorld::ProcessLineOfSight skips peds without it),
    // but it is only enabled while the avatar is clearly away from the local player, so it still can't trap us.
    bool want = false;
    if (remoteOptions().pvp && localPed) {
        Vec3 a, b;
        if (sa::entityPos(p, a) && sa::entityPos(localPed, b)) {
            float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
            want = dx * dx + dy * dy > kPvpCollisionMinDist * kPvpCollisionMinDist || dz * dz > 4.f;
        }
    }
    bool has = (sa::at<uint32_t>(p, sa::off::entityFlags) & 1u) != 0;
    // The game turns collision back on when a ped leaves a vehicle; re-apply through the script command.
    if (has != want) scm::Call(scm::SET_CHAR_COLLISION).i(pedHandle_).i(want ? 1 : 0).run();
    // Without collision the ped never "touches" the ground; we place it on the sender's (grounded) position
    // every frame, so keep it flagged as standing and not in the air to avoid falling animations/gravity.
    uint32_t& pf = sa::at<uint32_t>(p, sa::off::pedFlags);
    pf = (pf | 1u) & ~(1u << 9);
    sa::at<uint32_t>(p, sa::off::physicalFlags) &= ~(1u << 1);
}

void RemotePlayer::applyCombatMode() {
    int want = remoteOptions().pvp ? 1 : 0;
    if (pedHandle_ < 0 || want == combatMode_) return;
    combatMode_ = want;
    // SET_CHAR_PROOFS char bullet fire explosion collision melee. Fire stays proof (a burning copy would
    // desync from the owner); everything else can hurt the avatar when PvP is on.
    if (want) scm::Call(scm::SET_CHAR_PROOFS).i(pedHandle_).i(0).i(1).i(0).i(0).i(0).run();
    else scm::Call(scm::SET_CHAR_PROOFS).i(pedHandle_).i(1).i(1).i(1).i(1).i(1).run();
    scm::Call(scm::SET_CHAR_NEVER_TARGETTED).i(pedHandle_).i(want ? 0 : 1).run();
    LOGI("Boneco de %s: %s", nick_.c_str(), want ? "PvP ligado (pode ser ferido)" : "invulneravel (PvP desligado)");
}

void RemotePlayer::applyWeapon() {
    int w = weaponState_;
    if (pedHandle_ < 0 || w == weaponApplied_) return;
    uint32_t now = sa::gameTimeMs();
    if (lastWeaponFailMs_ && now - lastWeaponFailMs_ < kCreateRetryMs) return;
    int model = lbn::weaponModel(uint8_t(w));
    if (w != 0 && model > 0 && !scm::loadModelNow(model)) {
        LOGW("Nao consegui carregar o modelo da arma %d (%s) para %s", w, lbn::weaponNamePtBr(uint8_t(w)), nick_.c_str());
        lastWeaponFailMs_ = now ? now : 1;
        return;
    }
    lastWeaponFailMs_ = 0;
    scm::Call(scm::REMOVE_ALL_CHAR_WEAPONS).i(pedHandle_).run();
    if (w != 0) {
        scm::Call(scm::GIVE_WEAPON_TO_CHAR).i(pedHandle_).i(w).i(9999).run();
        scm::Call(scm::SET_CURRENT_CHAR_WEAPON).i(pedHandle_).i(w).run();
        if (model > 0) scm::releaseModel(model);
    }
    weaponApplied_ = w;
    LOGI("Boneco de %s agora segura: %s", nick_.c_str(), lbn::weaponNamePtBr(uint8_t(w)));
}

void RemotePlayer::detectHits(void* p, const WorldLookup& world) {
    if (!p) return;
    float hp = sa::at<float>(p, sa::off::pedHealth);
    if (hp >= kAvatarHealth - 0.5f) return;
    float lost = kAvatarHealth - hp;
    void* by = sa::at<void*>(p, sa::off::pedLastDamEntity);
    bool mine = by && (by == world.localPed || (world.localVehicle && by == world.localVehicle));
    if (remoteOptions().pvp && mine && remoteOptions().onHit) {
        uint8_t wep = uint8_t(sa::at<uint32_t>(p, sa::off::pedLastDamWeapon) & 0xFF);
        remoteOptions().onHit(id_, lost > 200.f ? 200.f : lost, lbn::isValidWeapon(wep) ? wep : 0);
    }
    // The real damage is applied by the owner's game; our copy always goes back to full health.
    if (!sa::pedIsDead(p)) sa::at<float>(p, sa::off::pedHealth) = kAvatarHealth;
}

void RemotePlayer::syncVehicleHealth(void* v, const PlayerState& s) {
    if (!v || vehExploded_) return;
    if (s.veh.health <= 0.f) {
        // The owner's car blew up: blow up our copy once (EXPLODE_CAR ignores proofs).
        scm::Call(scm::EXPLODE_CAR).i(vehHandle_).run();
        vehExploded_ = true;
        LOGI("Veiculo de %s explodiu (dono)", nick_.c_str());
        return;
    }
    // Below 250 a car catches fire and explodes on its own; keep our copy just above that so only the
    // owner's game decides when it explodes (smoke/damage level still follow the owner).
    float want = s.veh.health < 255.f ? 255.f : s.veh.health;
    float& cur = sa::at<float>(v, sa::off::vehHealth);
    if (cur - want > 25.f || want - cur > 25.f) cur = want;
}

void RemotePlayer::applyOnFoot(const PlayerState& s, void* localPed) {
    void* p = ped();
    if (!p || sa::pedVehicle(p)) return;
    enforceNoCollision(p, localPed);
    sa::setPedHeading(p, s.heading);
    sa::setPhysicalTransform(p, s.pos, nullptr, nullptr, nullptr, s.vel);
    applyAnim(s.anim);
}

void RemotePlayer::update(uint32_t nowMs, const WorldLookup& world) {
    lbn::InterpResult r = interp_.sample(nowMs);
    if (!r.valid) return;
    const PlayerState& s = r.state;

    Vec3 spawnPos = (s.inVehicle() && !s.inRemoteVehicle()) ? s.veh.pos : s.pos;
    if (!ensurePed(spawnPos)) return;
    uint32_t gt = sa::gameTimeMs();
    void* lp = world.localPed;

    // Stage 2: damage, weapon in hand.
    detectHits(ped(), world);
    if (sa::pedIsDead(ped())) {
        // Our copy died (e.g. huge explosion): recreate it next frame at the owner's position.
        LOGI("Boneco de %s morreu localmente; recriando", nick_.c_str());
        destroyEntities(world.localVehicle);
        return;
    }
    applyCombatMode();
    applyWeapon();

    if (!s.inVehicle()) {
        warpOut(s.pos, gt);
        applyOnFoot(s, lp);
        return;
    }

    if (!s.inRemoteVehicle()) {
        if (!ensureVehicle(s, world.localVehicle)) { applyOnFoot(s, lp); return; }
        void* v = vehicle();
        // The remote owns this car: drive its transform unless the local player took the wheel.
        bool localDrives = (v == world.localVehicle) && sa::pedSeat(world.localPed, v) == 0;
        if (!localDrives) {
            Vec3 rt, fw, up;
            lbn::axesFromQuat(s.veh.rot, rt, fw, up);
            sa::setPhysicalTransform(v, s.veh.pos, &rt, &fw, &up, s.veh.vel);
            syncVehicleHealth(v, s);
        }
        if (s.veh.color1 != vehColor1_ || s.veh.color2 != vehColor2_) {
            scm::Call(scm::CHANGE_CAR_COLOUR).i(vehHandle_).i(s.veh.color1).i(s.veh.color2).run();
            vehColor1_ = s.veh.color1; vehColor2_ = s.veh.color2;
        }
        warpInto(v, vehHandle_, s.seat, gt);
        return;
    }

    // Passenger in a vehicle owned by someone else (the local player or another remote player).
    void* target = nullptr;
    if (s.vehicleOwnerId == world.selfId) target = world.localVehicle;
    else if (world.vehicleOfPlayer) target = world.vehicleOfPlayer(s.vehicleOwnerId, world.ctx);
    if (!target) { warpOut(s.pos, gt); applyOnFoot(s, lp); return; }
    int h = sa::vehicleRef(target);
    if (h >= 0) warpInto(target, h, s.seat, gt);
}

void RemotePlayer::destroyEntities(void* localVehicle) {
    void* p = ped();
    if (p) {
        if (sa::pedVehicle(p)) {
            Vec3 pos; sa::entityPos(p, pos);
            scm::Call(scm::WARP_CHAR_FROM_CAR_TO_COORD).i(pedHandle_).f(pos.x).f(pos.y).f(pos.z + 1.f).run();
        }
        scm::deleteChar(pedHandle_);
    }
    pedHandle_ = -1;
    if (vehHandle_ >= 0) dropVehicle(localVehicle);
    animValid_ = false;
}

void RemotePlayer::forgetEntities() {
    pedHandle_ = -1;
    vehHandle_ = -1; vehKey_ = 0; vehModel_ = 0; vehColor1_ = vehColor2_ = 0xFF;
    animValid_ = false;
}
