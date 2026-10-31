// Runs single GTA SA script (SCM) commands from C++ through the game's own interpreter.
// Original code for LBN Coop SA. Opcode numbers are the public SA opcode ids.
#pragma once
#include <cstdint>
#include <cstddef>

namespace scm {

enum Op : uint16_t {
    CREATE_CHAR = 0x009A,
    DELETE_CHAR = 0x009B,
    SET_CHAR_COORDINATES = 0x00A1,
    CREATE_CAR = 0x00A5,
    DELETE_CAR = 0x00A6,
    SET_PLAYER_CONTROL = 0x01B4,
    MARK_CAR_AS_NO_LONGER_NEEDED = 0x01C3,
    CHANGE_CAR_COLOUR = 0x0229,
    REQUEST_MODEL = 0x0247,
    HAS_MODEL_LOADED = 0x0248,
    MARK_MODEL_AS_NO_LONGER_NEEDED = 0x0249,
    SET_CHAR_PROOFS = 0x02AB,
    SET_CAR_PROOFS = 0x02AC,
    WARP_CHAR_FROM_CAR_TO_COORD = 0x0362,
    WARP_CHAR_INTO_CAR = 0x036A,
    LOAD_ALL_MODELS_NOW = 0x038B,
    SET_CHAR_CANT_BE_DRAGGED_OUT = 0x039E,
    WARP_CHAR_INTO_CAR_AS_PASSENGER = 0x0430,
    SET_CHAR_NEVER_TARGETTED = 0x0568,
    TASK_PLAY_ANIM = 0x0605,
    CLEAR_CHAR_TASKS = 0x0687,
    SET_CHAR_DROPS_WEAPONS_WHEN_DEAD = 0x087E,
    SET_CAR_ENGINE_ON = 0x0918,
    GET_PLAYER_CHAR = 0x01F5,
    SET_CHAR_COLLISION = 0x0619,
    GET_GROUND_Z_FOR_3D_COORD = 0x02CE,
    IS_AREA_OCCUPIED = 0x0339,
    // v0.1.2 (stage 2)
    GIVE_WEAPON_TO_CHAR = 0x01B2,
    SET_CURRENT_CHAR_WEAPON = 0x01B9,
    REMOVE_ALL_CHAR_WEAPONS = 0x048F,
    SET_CHAR_HEALTH = 0x0223,
    EXPLODE_CAR = 0x020B,
    ALTER_WANTED_LEVEL = 0x010D,
    ALTER_WANTED_LEVEL_NO_DROP = 0x010E,
    CLEAR_WANTED_LEVEL = 0x0110,
    STORE_WANTED_LEVEL = 0x01C0,
    GET_TIME_OF_DAY = 0x00BF,
    SET_TIME_OF_DAY = 0x00C0,
    FORCE_WEATHER_NOW = 0x01B6,
    RELEASE_WEATHER = 0x01B7,
    SET_CAR_DENSITY_MULTIPLIER = 0x01EB,
    SET_PED_DENSITY_MULTIPLIER = 0x03DE,
    SET_CHAR_ONLY_DAMAGED_BY_PLAYER = 0x02A9,
    SET_CHAR_SUFFERS_CRITICAL_HITS = 0x0446,
};

// Builds the bytecode for one command and executes it with a private CRunningScript.
class Call {
public:
    explicit Call(uint16_t opcode);
    Call& i(int32_t v);           // immediate int (type 0x01)
    Call& f(float v);             // immediate float (type 0x06)
    Call& s(const char* text);    // variable-length string (type 0x0E)
    Call& out(uint8_t localVar);  // output into local variable N (type 0x03)
    // Executes; returns the condition flag (meaningful only for conditional opcodes).
    bool run();
    int32_t local(int idx) const;
    float localf(int idx) const;   // output local read as float
    bool overflow() const { return overflow_; }
private:
    void put(const void* p, size_t n);
    uint8_t buf_[128];
    size_t len_ = 0;
    bool overflow_ = false;
    int32_t locals_[8] = {};
};

// Convenience wrappers (all return -1 / false on failure).
bool loadModelNow(int model);
void releaseModel(int model);
int createChar(int pedType, int model, float x, float y, float z);
void deleteChar(int handle);
int createCar(int model, float x, float y, float z);
void deleteCar(int handle);
void releaseCar(int handle);
int playerCharHandle();   // handle of the local player's ped (CJ), -1 if none
int wantedLevel();        // local player's wanted level 0..6
void setWantedLevel(int level, bool onlyRaise);
bool timeOfDay(int& h, int& m);

} // namespace scm
