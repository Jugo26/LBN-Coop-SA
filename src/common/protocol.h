// LBN Coop SA network protocol (original design).
// All messages: u8 type followed by the payload, little-endian.
// Channel 0 = reliable (control, chat), channel 1 = unreliable sequenced (state).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "mathutil.h"
#include "version.h"

namespace lbn {

enum Channel : uint8_t { kChanReliable = 0, kChanState = 1, kChanCount = 2 };

enum class MsgType : uint8_t {
    Hello        = 1,  // C->S
    Welcome      = 2,  // S->C
    Reject       = 3,  // S->C
    PlayerJoin   = 4,  // S->C
    PlayerLeave  = 5,  // S->C
    StateC2S     = 6,  // C->S
    StateS2C     = 7,  // S->C (relayed, carries player id)
    ChatC2S      = 8,  // C->S
    ChatS2C      = 9,  // S->C
    Pings        = 10, // S->C
    // --- protocol 3 (stage 2: world sync) ---
    DamageC2S    = 11, // C->S  "I hit player X"           (shooter-side hit detection)
    DamageS2C    = 12, // S->C  "player Y hit you"          (forwarded only if the host allows PvP)
    WorldC2S     = 13, // C->S  clock/weather/wanted/rules  (accepted only from the authority = host)
    WorldS2C     = 14, // S->C  relayed world state + who the authority is
    Session      = 15, // S->C  authority id (sent on join and whenever it changes)
};

enum class RejectReason : uint8_t { None = 0, BadVersion = 1, ServerFull = 2, BadName = 3, BadMessage = 4 };
enum class LeaveReason : uint8_t { Quit = 0, Timeout = 1, Kicked = 2 };
enum class MoveAnim : uint8_t { Idle = 0, Walk = 1, Run = 2, Sprint = 3 };

constexpr uint8_t kSystemSender = 0xFF;
constexpr size_t kMaxNickLen = 24;
constexpr size_t kMaxChatLen = 120;

struct HelloMsg {
    uint32_t magic = LBN_PROTOCOL_MAGIC;
    uint16_t version = LBN_PROTOCOL_VERSION;
    std::string nick;
    uint16_t skin = 0;
    std::string modVersion = LBN_MOD_VERSION;  // since protocol 2
};

struct WelcomeMsg {
    uint8_t yourId = 0;
    uint8_t maxPlayers = LBN_MAX_PLAYERS;
    std::string serverName;
    std::string yourNick;   // nickname actually assigned (differs if ours was already taken)
};

struct RejectMsg {
    RejectReason reason = RejectReason::None;
    std::string detail;
};

struct PlayerJoinMsg {
    uint8_t id = 0;
    std::string nick;
    uint16_t skin = 0;
};

struct PlayerLeaveMsg {
    uint8_t id = 0;
    LeaveReason reason = LeaveReason::Quit;
};

// Vehicle part of a player state.
struct VehicleState {
    uint32_t key = 0;        // sender-local unique key of the vehicle (pool ref)
    uint16_t model = 0;      // 400..611
    Vec3 pos;
    Quat rot;
    Vec3 vel;                // metres per second
    uint8_t color1 = 0, color2 = 0;
    float health = 1000.f;
};

struct PlayerState {
    enum Flags : uint8_t {
        InVehicle = 1 << 0,
        RemoteVehicle = 1 << 1,  // sitting in a vehicle that belongs to another player
    };
    uint32_t seq = 0;
    uint32_t timeMs = 0;     // sender clock
    uint8_t flags = 0;
    Vec3 pos;
    Vec3 vel;                // metres per second
    float heading = 0;       // radians
    MoveAnim anim = MoveAnim::Idle;
    uint8_t health = 100;
    uint8_t armour = 0;
    uint8_t weapon = 0;      // eWeaponType held (0 = fists); server replaces invalid ids by 0
    uint8_t wanted = 0;      // local wanted level 0..6 (the authority merges it)
    // valid when InVehicle:
    uint8_t seat = 0;        // 0 = driver, 1.. = passenger seat index + 1
    // valid when InVehicle && !RemoteVehicle:
    VehicleState veh;
    // valid when InVehicle && RemoteVehicle:
    uint8_t vehicleOwnerId = 0;

    bool inVehicle() const { return flags & InVehicle; }
    bool inRemoteVehicle() const { return (flags & InVehicle) && (flags & RemoteVehicle); }
};

struct ChatMsg {
    uint8_t from = kSystemSender; // only meaningful S->C
    std::string text;
};

struct DamageMsg {
    uint8_t peer = 0;        // C2S: target id; S2C: attacker id
    uint16_t amountX10 = 0;  // health points * 10 (max 2000 = 200 HP)
    uint8_t weapon = 0;
};

struct WorldState {
    enum Flags : uint8_t {
        PvP = 1 << 0,            // players can damage each other
        ForceDensity = 1 << 1,   // clients apply the host's traffic/ped density multipliers
        SyncClock = 1 << 2,
        SyncWeather = 1 << 3,
        SyncWanted = 1 << 4,
    };
    uint8_t hour = 12, minute = 0;
    int16_t weather = 0;         // CWeather::NewWeatherType of the host
    uint8_t wanted = 0;          // group wanted level decided by the host
    uint8_t flags = SyncClock | SyncWeather | SyncWanted;
    uint8_t carDensityX100 = 100, pedDensityX100 = 100;
};
struct WorldS2CMsg { uint8_t authorityId = 0xFF; WorldState w; };
struct SessionMsg { uint8_t authorityId = 0xFF; };

struct PingEntry { uint8_t id; uint16_t pingMs; };
struct PingsMsg { std::vector<PingEntry> entries; };

// --- Encoding ---
std::vector<uint8_t> encode(const HelloMsg& m);
std::vector<uint8_t> encode(const WelcomeMsg& m);
std::vector<uint8_t> encode(const RejectMsg& m);
std::vector<uint8_t> encode(const PlayerJoinMsg& m);
std::vector<uint8_t> encode(const PlayerLeaveMsg& m);
std::vector<uint8_t> encodeStateC2S(const PlayerState& s);
std::vector<uint8_t> encodeStateS2C(uint8_t id, const PlayerState& s);
std::vector<uint8_t> encodeChatC2S(const std::string& text);
std::vector<uint8_t> encodeChatS2C(uint8_t from, const std::string& text);
std::vector<uint8_t> encode(const PingsMsg& m);
std::vector<uint8_t> encodeDamageC2S(const DamageMsg& m);
std::vector<uint8_t> encodeDamageS2C(const DamageMsg& m);
std::vector<uint8_t> encodeWorldC2S(const WorldState& w);
std::vector<uint8_t> encode(const WorldS2CMsg& m);
std::vector<uint8_t> encode(const SessionMsg& m);

// --- Decoding --- (return false on malformed input)
bool peekType(const uint8_t* p, size_t n, MsgType& out);
bool decode(const uint8_t* p, size_t n, HelloMsg& m);
bool decode(const uint8_t* p, size_t n, WelcomeMsg& m);
bool decode(const uint8_t* p, size_t n, RejectMsg& m);
bool decode(const uint8_t* p, size_t n, PlayerJoinMsg& m);
bool decode(const uint8_t* p, size_t n, PlayerLeaveMsg& m);
bool decodeStateC2S(const uint8_t* p, size_t n, PlayerState& s);
bool decodeStateS2C(const uint8_t* p, size_t n, uint8_t& id, PlayerState& s);
bool decodeChatC2S(const uint8_t* p, size_t n, std::string& text);
bool decodeChatS2C(const uint8_t* p, size_t n, ChatMsg& m);
bool decode(const uint8_t* p, size_t n, PingsMsg& m);
bool decodeDamageC2S(const uint8_t* p, size_t n, DamageMsg& m);
bool decodeDamageS2C(const uint8_t* p, size_t n, DamageMsg& m);
bool decodeWorldC2S(const uint8_t* p, size_t n, WorldState& w);
bool decode(const uint8_t* p, size_t n, WorldS2CMsg& m);
bool decode(const uint8_t* p, size_t n, SessionMsg& m);

// Validation helpers (used by server and client to reject garbage).
bool isFiniteVec(const Vec3& v);
bool isSaneState(const PlayerState& s);
bool isValidPedSkin(uint16_t model);
bool isValidVehicleModel(uint16_t model);
bool isSpawnableVehicleModel(uint16_t model); // excludes trains/trailers that CREATE_CAR can't make
bool isValidWeapon(uint8_t weaponType);       // 0..46 except the unused 19-21
int weaponModel(uint8_t weaponType);          // model id to load for GIVE_WEAPON_TO_CHAR; -1 = none needed/unknown
const char* weaponNamePtBr(uint8_t weaponType);
bool isSaneWorld(const WorldState& w);

const char* rejectReasonPtBr(RejectReason r);
// pt-BR detail for a mod-version mismatch, e.g. "host v0.1.0, voce v0.0.9 - ...".
std::string modVersionMismatchPtBr(const std::string& hostVer, const std::string& clientVer);
const char* leaveReasonPtBr(LeaveReason r);

} // namespace lbn
