// LBN Coop SA network protocol implementation (original code).
#include "protocol.h"
#include "bytes.h"
#include <cmath>

namespace lbn {

namespace {

void putVec(ByteWriter& w, const Vec3& v) { w.f32(v.x); w.f32(v.y); w.f32(v.z); }
Vec3 getVec(ByteReader& r) { Vec3 v; v.x = r.f32(); v.y = r.f32(); v.z = r.f32(); return v; }

// Quaternion packed as 4 x int16 (normalized * 32767). Plenty for rendering.
void putQuat(ByteWriter& w, const Quat& qin) {
    Quat q = normalize(qin);
    auto pk = [](float f) {
        float c = f < -1 ? -1 : (f > 1 ? 1 : f);
        return int16_t(std::lround(c * 32767.0f));
    };
    w.i16(pk(q.x)); w.i16(pk(q.y)); w.i16(pk(q.z)); w.i16(pk(q.w));
}
Quat getQuat(ByteReader& r) {
    Quat q;
    q.x = r.i16() / 32767.0f; q.y = r.i16() / 32767.0f;
    q.z = r.i16() / 32767.0f; q.w = r.i16() / 32767.0f;
    return normalize(q);
}

void putState(ByteWriter& w, const PlayerState& s) {
    w.u32(s.seq);
    w.u32(s.timeMs);
    w.u8(s.flags);
    putVec(w, s.pos);
    putVec(w, s.vel);
    w.f32(s.heading);
    w.u8(uint8_t(s.anim));
    w.u8(s.health);
    w.u8(s.armour);
    w.u8(s.weapon);
    w.u8(s.wanted);
    if (s.flags & PlayerState::InVehicle) {
        w.u8(s.seat);
        if (s.flags & PlayerState::RemoteVehicle) {
            w.u8(s.vehicleOwnerId);
        } else {
            w.u32(s.veh.key);
            w.u16(s.veh.model);
            putVec(w, s.veh.pos);
            putQuat(w, s.veh.rot);
            putVec(w, s.veh.vel);
            w.u8(s.veh.color1);
            w.u8(s.veh.color2);
            float h = s.veh.health;
            if (!(h >= 0)) h = 0;          // also catches NaN
            if (h > 65535.f) h = 65535.f;
            w.u16(uint16_t(h));
        }
    }
}

bool getState(ByteReader& r, PlayerState& s) {
    s.seq = r.u32();
    s.timeMs = r.u32();
    s.flags = r.u8();
    s.pos = getVec(r);
    s.vel = getVec(r);
    s.heading = r.f32();
    uint8_t a = r.u8();
    if (a > uint8_t(MoveAnim::Sprint)) a = 0;
    s.anim = MoveAnim(a);
    s.health = r.u8();
    s.armour = r.u8();
    s.weapon = r.u8();
    s.wanted = r.u8();
    if (s.flags & PlayerState::InVehicle) {
        s.seat = r.u8();
        if (s.flags & PlayerState::RemoteVehicle) {
            s.vehicleOwnerId = r.u8();
        } else {
            s.veh.key = r.u32();
            s.veh.model = r.u16();
            s.veh.pos = getVec(r);
            s.veh.rot = getQuat(r);
            s.veh.vel = getVec(r);
            s.veh.color1 = r.u8();
            s.veh.color2 = r.u8();
            s.veh.health = float(r.u16());
        }
    }
    return r.ok();
}

bool begin(ByteReader& r, MsgType expected) {
    return r.u8() == uint8_t(expected) && r.ok();
}

} // namespace

std::vector<uint8_t> encode(const HelloMsg& m) {
    ByteWriter w; w.u8(uint8_t(MsgType::Hello));
    w.u32(m.magic); w.u16(m.version); w.str(m.nick, kMaxNickLen); w.u16(m.skin);
    w.str(m.modVersion, 32);
    return w.data();
}
std::vector<uint8_t> encode(const WelcomeMsg& m) {
    ByteWriter w; w.u8(uint8_t(MsgType::Welcome));
    w.u8(m.yourId); w.u8(m.maxPlayers); w.str(m.serverName, 64); w.str(m.yourNick, kMaxNickLen);
    return w.data();
}
std::vector<uint8_t> encode(const RejectMsg& m) {
    ByteWriter w; w.u8(uint8_t(MsgType::Reject));
    w.u8(uint8_t(m.reason)); w.str(m.detail, 128);
    return w.data();
}
std::vector<uint8_t> encode(const PlayerJoinMsg& m) {
    ByteWriter w; w.u8(uint8_t(MsgType::PlayerJoin));
    w.u8(m.id); w.str(m.nick, kMaxNickLen); w.u16(m.skin);
    return w.data();
}
std::vector<uint8_t> encode(const PlayerLeaveMsg& m) {
    ByteWriter w; w.u8(uint8_t(MsgType::PlayerLeave));
    w.u8(m.id); w.u8(uint8_t(m.reason));
    return w.data();
}
std::vector<uint8_t> encodeStateC2S(const PlayerState& s) {
    ByteWriter w; w.u8(uint8_t(MsgType::StateC2S));
    putState(w, s);
    return w.data();
}
std::vector<uint8_t> encodeStateS2C(uint8_t id, const PlayerState& s) {
    ByteWriter w; w.u8(uint8_t(MsgType::StateS2C));
    w.u8(id);
    putState(w, s);
    return w.data();
}
std::vector<uint8_t> encodeChatC2S(const std::string& text) {
    ByteWriter w; w.u8(uint8_t(MsgType::ChatC2S));
    w.str(text, kMaxChatLen);
    return w.data();
}
std::vector<uint8_t> encodeChatS2C(uint8_t from, const std::string& text) {
    ByteWriter w; w.u8(uint8_t(MsgType::ChatS2C));
    w.u8(from); w.str(text, 255);
    return w.data();
}
std::vector<uint8_t> encode(const PingsMsg& m) {
    ByteWriter w; w.u8(uint8_t(MsgType::Pings));
    size_t n = m.entries.size() > 32 ? 32 : m.entries.size();
    w.u8(uint8_t(n));
    for (size_t i = 0; i < n; ++i) { w.u8(m.entries[i].id); w.u16(m.entries[i].pingMs); }
    return w.data();
}

namespace {
void putDamage(ByteWriter& w, const DamageMsg& m) { w.u8(m.peer); w.u16(m.amountX10); w.u8(m.weapon); }
void getDamage(ByteReader& r, DamageMsg& m) { m.peer = r.u8(); m.amountX10 = r.u16(); m.weapon = r.u8(); }
void putWorld(ByteWriter& w, const WorldState& s) {
    w.u8(s.hour); w.u8(s.minute); w.u16(uint16_t(s.weather)); w.u8(s.wanted); w.u8(s.flags);
    w.u8(s.carDensityX100); w.u8(s.pedDensityX100);
}
void getWorld(ByteReader& r, WorldState& s) {
    s.hour = r.u8(); s.minute = r.u8(); s.weather = int16_t(r.u16()); s.wanted = r.u8(); s.flags = r.u8();
    s.carDensityX100 = r.u8(); s.pedDensityX100 = r.u8();
}
}
std::vector<uint8_t> encodeDamageC2S(const DamageMsg& m) { ByteWriter w; w.u8(uint8_t(MsgType::DamageC2S)); putDamage(w, m); return w.data(); }
std::vector<uint8_t> encodeDamageS2C(const DamageMsg& m) { ByteWriter w; w.u8(uint8_t(MsgType::DamageS2C)); putDamage(w, m); return w.data(); }
std::vector<uint8_t> encodeWorldC2S(const WorldState& s) { ByteWriter w; w.u8(uint8_t(MsgType::WorldC2S)); putWorld(w, s); return w.data(); }
std::vector<uint8_t> encode(const WorldS2CMsg& m) { ByteWriter w; w.u8(uint8_t(MsgType::WorldS2C)); w.u8(m.authorityId); putWorld(w, m.w); return w.data(); }
std::vector<uint8_t> encode(const SessionMsg& m) { ByteWriter w; w.u8(uint8_t(MsgType::Session)); w.u8(m.authorityId); return w.data(); }
bool decodeDamageC2S(const uint8_t* p, size_t n, DamageMsg& m) { ByteReader r(p, n); if (!begin(r, MsgType::DamageC2S)) return false; getDamage(r, m); return r.ok(); }
bool decodeDamageS2C(const uint8_t* p, size_t n, DamageMsg& m) { ByteReader r(p, n); if (!begin(r, MsgType::DamageS2C)) return false; getDamage(r, m); return r.ok(); }
bool decodeWorldC2S(const uint8_t* p, size_t n, WorldState& s) { ByteReader r(p, n); if (!begin(r, MsgType::WorldC2S)) return false; getWorld(r, s); return r.ok() && isSaneWorld(s); }
bool decode(const uint8_t* p, size_t n, WorldS2CMsg& m) { ByteReader r(p, n); if (!begin(r, MsgType::WorldS2C)) return false; m.authorityId = r.u8(); getWorld(r, m.w); return r.ok() && isSaneWorld(m.w); }
bool decode(const uint8_t* p, size_t n, SessionMsg& m) { ByteReader r(p, n); if (!begin(r, MsgType::Session)) return false; m.authorityId = r.u8(); return r.ok(); }

bool peekType(const uint8_t* p, size_t n, MsgType& out) {
    if (!p || n < 1) return false;
    out = MsgType(p[0]);
    return true;
}
bool decode(const uint8_t* p, size_t n, HelloMsg& m) {
    ByteReader r(p, n);
    if (!begin(r, MsgType::Hello)) return false;
    m.magic = r.u32(); m.version = r.u16(); m.nick = r.str(); m.skin = r.u16();
    if (!r.ok()) return false;
    // Protocol 1 clients had no mod version; leave it empty so the server reports the mismatch clearly.
    m.modVersion.clear();
    if (m.version >= 2) m.modVersion = r.str();
    return r.ok();
}
bool decode(const uint8_t* p, size_t n, WelcomeMsg& m) {
    ByteReader r(p, n);
    if (!begin(r, MsgType::Welcome)) return false;
    m.yourId = r.u8(); m.maxPlayers = r.u8(); m.serverName = r.str();
    m.yourNick = r.remaining() ? r.str() : std::string();
    return r.ok();
}
bool decode(const uint8_t* p, size_t n, RejectMsg& m) {
    ByteReader r(p, n);
    if (!begin(r, MsgType::Reject)) return false;
    m.reason = RejectReason(r.u8()); m.detail = r.str();
    return r.ok();
}
bool decode(const uint8_t* p, size_t n, PlayerJoinMsg& m) {
    ByteReader r(p, n);
    if (!begin(r, MsgType::PlayerJoin)) return false;
    m.id = r.u8(); m.nick = r.str(); m.skin = r.u16();
    return r.ok();
}
bool decode(const uint8_t* p, size_t n, PlayerLeaveMsg& m) {
    ByteReader r(p, n);
    if (!begin(r, MsgType::PlayerLeave)) return false;
    m.id = r.u8(); m.reason = LeaveReason(r.u8());
    return r.ok();
}
bool decodeStateC2S(const uint8_t* p, size_t n, PlayerState& s) {
    ByteReader r(p, n);
    if (!begin(r, MsgType::StateC2S)) return false;
    return getState(r, s);
}
bool decodeStateS2C(const uint8_t* p, size_t n, uint8_t& id, PlayerState& s) {
    ByteReader r(p, n);
    if (!begin(r, MsgType::StateS2C)) return false;
    id = r.u8();
    return getState(r, s);
}
bool decodeChatC2S(const uint8_t* p, size_t n, std::string& text) {
    ByteReader r(p, n);
    if (!begin(r, MsgType::ChatC2S)) return false;
    text = r.str();
    return r.ok();
}
bool decodeChatS2C(const uint8_t* p, size_t n, ChatMsg& m) {
    ByteReader r(p, n);
    if (!begin(r, MsgType::ChatS2C)) return false;
    m.from = r.u8(); m.text = r.str();
    return r.ok();
}
bool decode(const uint8_t* p, size_t n, PingsMsg& m) {
    ByteReader r(p, n);
    if (!begin(r, MsgType::Pings)) return false;
    uint8_t c = r.u8();
    m.entries.clear();
    for (uint8_t i = 0; i < c && r.ok(); ++i) {
        PingEntry e; e.id = r.u8(); e.pingMs = r.u16();
        if (r.ok()) m.entries.push_back(e);
    }
    return r.ok();
}

bool isFiniteVec(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool isSaneState(const PlayerState& s) {
    // SA map is roughly +-3000 on X/Y; allow a margin (planes can fly far).
    auto inWorld = [](const Vec3& v) {
        return std::fabs(v.x) < 20000.f && std::fabs(v.y) < 20000.f && v.z > -1000.f && v.z < 20000.f;
    };
    if (!isFiniteVec(s.pos) || !isFiniteVec(s.vel) || !std::isfinite(s.heading)) return false;
    if (!inWorld(s.pos)) return false;
    if (s.vel.length() > 500.f) return false;
    if (s.wanted > 6) return false;
    if (s.flags & PlayerState::InVehicle) {
        if (s.seat > 16) return false;
        if (!(s.flags & PlayerState::RemoteVehicle)) {
            if (!isValidVehicleModel(s.veh.model)) return false;
            if (!isFiniteVec(s.veh.pos) || !isFiniteVec(s.veh.vel) || !inWorld(s.veh.pos)) return false;
            if (s.veh.vel.length() > 500.f) return false;
        } else if (s.vehicleOwnerId >= LBN_MAX_PLAYERS) {
            return false;
        }
    }
    return true;
}

bool isValidPedSkin(uint16_t m) {
    // 0 is CJ (needs the player clothes system, not supported for remote peds in stage 1).
    // Exclude ids without a model in vanilla ped.ide and the "special" slots 290-299
    // that need named special-model loading.
    if (m < 1 || m > 311) return false;
    switch (m) {
    case 3: case 4: case 5: case 6: case 8: case 42: case 65: case 74: case 86:
    case 119: case 149: case 208: case 265: case 266: case 267: case 268: case 269:
    case 270: case 271: case 272: case 273: case 289:
        return false;
    default: break;
    }
    if (m >= 290 && m <= 299) return false;
    return true;
}

bool isValidVehicleModel(uint16_t m) { return m >= 400 && m <= 611; }

bool isSpawnableVehicleModel(uint16_t m) {
    if (!isValidVehicleModel(m)) return false;
    switch (m) {
    case 449: // tram
    case 537: case 538: // freight / streak trains
    case 569: case 570: // train carriages
    case 590: // box freight carriage
        return false;
    default:
        return true;
    }
}

bool isValidWeapon(uint8_t w) { return w <= 46 && !(w >= 19 && w <= 21); }

int weaponModel(uint8_t w) {
    // eWeaponType -> eWeaponModel (SA 1.0; cross-checked with plugin-sdk enums).
    static const int16_t kModel[47] = {
        -1, 331, 333, 334, 335, 336, 337, 338, 339, 341,   // 0-9  fists, knuckles, golf, nightstick, knife, bat, shovel, cue, katana, chainsaw
        321, 322, 323, 324, 325, 326, 342, 343, 344, -1,   // 10-19 dildos/vibes, flowers, cane, grenade, teargas, molotov, (rocket proj.)
        -1, -1, 346, 347, 348, 349, 350, 351, 352, 353,    // 20-29 -, -, colt45, silenced, deagle, shotgun, sawnoff, spas, uzi, mp5
        355, 356, 372, 357, 358, 359, 360, 361, 362, 363,  // 30-39 ak47, m4, tec9, rifle, sniper, rpg, heatseek, flame, minigun, satchel
        364, 365, 366, 367, 368, 369, 371};                // 40-46 detonator, spraycan, extinguisher, camera, nv, ir, parachute
    return w <= 46 ? kModel[w] : -1;
}

const char* weaponNamePtBr(uint8_t w) {
    switch (w) {
    case 0: return "maos"; case 1: return "soco ingles"; case 4: return "faca"; case 5: return "taco";
    case 8: return "katana"; case 9: return "motosserra"; case 16: return "granada"; case 18: return "molotov";
    case 22: return "pistola"; case 23: return "pistola c/ silenciador"; case 24: return "desert eagle";
    case 25: return "escopeta"; case 26: return "cano serrado"; case 27: return "escopeta de combate";
    case 28: return "uzi"; case 29: return "mp5"; case 30: return "ak-47"; case 31: return "m4"; case 32: return "tec-9";
    case 33: return "rifle"; case 34: return "sniper"; case 35: return "bazuca"; case 36: return "bazuca teleguiada";
    case 37: return "lanca-chamas"; case 38: return "minigun"; case 46: return "paraquedas";
    default: return isValidWeapon(w) ? "arma" : "?";
    }
}

bool isSaneWorld(const WorldState& w) {
    return w.hour < 24 && w.minute < 60 && w.weather >= 0 && w.weather < 46 && w.wanted <= 6 &&
           w.carDensityX100 <= 200 && w.pedDensityX100 <= 200;
}

const char* rejectReasonPtBr(RejectReason r) {
    switch (r) {
    case RejectReason::BadVersion: return "versao do mod diferente da do host";
    case RejectReason::ServerFull: return "servidor cheio";
    case RejectReason::BadName: return "nome invalido ou ja em uso";
    case RejectReason::BadMessage: return "mensagem invalida";
    default: return "motivo desconhecido";
    }
}

std::string modVersionMismatchPtBr(const std::string& hostVer, const std::string& clientVer) {
    return "host v" + hostVer + ", voce v" + (clientVer.empty() ? std::string("antiga/desconhecida") : clientVer) +
           ". Use a MESMA versao: abra o jogo pelo LbnCoopSA-Iniciar.exe";
}

const char* leaveReasonPtBr(LeaveReason r) {
    switch (r) {
    case LeaveReason::Quit: return "saiu";
    case LeaveReason::Timeout: return "caiu (timeout)";
    case LeaveReason::Kicked: return "foi removido";
    default: return "saiu";
    }
}

} // namespace lbn
