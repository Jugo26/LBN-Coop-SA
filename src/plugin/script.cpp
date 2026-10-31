// SCM single-command runner (original code for LBN Coop SA).
#include "script.h"
#include "game.h"
#include <cstring>

namespace scm {

namespace {
// CRunningScript is 0xE0 bytes in SA 1.0 US; layout offsets used below:
constexpr uint32_t kBaseIP = 0x10, kCurIP = 0x14, kLocals = 0x3C, kCondResult = 0xC5,
                   kUseMissionCleanup = 0xC6, kIsMission = 0xDC, kName = 0x08;
alignas(16) uint8_t g_script[0x100];
using ThisVoid = void(__thiscall*)(void*);
using ThisChar = char(__thiscall*)(void*);
}

Call::Call(uint16_t opcode) { put(&opcode, 2); }

void Call::put(const void* p, size_t n) {
    if (len_ + n > sizeof buf_) { overflow_ = true; return; }
    std::memcpy(buf_ + len_, p, n);
    len_ += n;
}

Call& Call::i(int32_t v) { uint8_t t = 0x01; put(&t, 1); put(&v, 4); return *this; }
Call& Call::f(float v) { uint8_t t = 0x06; put(&t, 1); put(&v, 4); return *this; }
Call& Call::s(const char* text) {
    size_t n = std::strlen(text);
    if (n > 60) n = 60;
    uint8_t t = 0x0E, l = uint8_t(n);
    put(&t, 1); put(&l, 1); put(text, n);
    return *this;
}
Call& Call::out(uint8_t localVar) {
    uint8_t t = 0x03; uint16_t idx = localVar;
    put(&t, 1); put(&idx, 2);
    return *this;
}

bool Call::run() {
    if (overflow_) return false;
    // Pad so a mis-sized command can't run off the end into garbage: 0x0000 = NOP.
    uint8_t code[sizeof buf_ + 8] = {};
    std::memcpy(code, buf_, len_);
    void* sc = g_script;
    std::memset(g_script, 0, sizeof g_script);
    reinterpret_cast<ThisVoid>(sa::fnScriptInit)(sc);
    std::memcpy(g_script + kName, "lbncoop", 8);
    sa::at<uint8_t*>(sc, kBaseIP) = code;
    sa::at<uint8_t*>(sc, kCurIP) = code;
    sa::at<uint8_t>(sc, kUseMissionCleanup) = 0;
    sa::at<uint8_t>(sc, kIsMission) = 0;
    for (int k = 0; k < 8; ++k) sa::at<int32_t>(sc, kLocals + 4 * k) = -1; // "no result" marker
    reinterpret_cast<ThisChar>(sa::fnScriptProcessOne)(sc);
    std::memcpy(locals_, g_script + kLocals, sizeof locals_);
    return sa::at<uint8_t>(sc, kCondResult) != 0;
}

int32_t Call::local(int idx) const { return (idx >= 0 && idx < 8) ? locals_[idx] : 0; }

bool loadModelNow(int model) {
    Call(REQUEST_MODEL).i(model).run();
    Call(LOAD_ALL_MODELS_NOW).run();
    return Call(HAS_MODEL_LOADED).i(model).run();
}

void releaseModel(int model) { Call(MARK_MODEL_AS_NO_LONGER_NEEDED).i(model).run(); }

int createChar(int pedType, int model, float x, float y, float z) {
    Call c(CREATE_CHAR);
    c.i(pedType).i(model).f(x).f(y).f(z).out(0);
    c.run();
    int h = c.local(0);
    return sa::pedFromHandle(h) ? h : -1;
}

void deleteChar(int handle) { if (sa::pedFromHandle(handle)) Call(DELETE_CHAR).i(handle).run(); }

int createCar(int model, float x, float y, float z) {
    Call c(CREATE_CAR);
    c.i(model).f(x).f(y).f(z).out(0);
    c.run();
    int h = c.local(0);
    return sa::vehicleFromHandle(h) ? h : -1;
}

void deleteCar(int handle) { if (sa::vehicleFromHandle(handle)) Call(DELETE_CAR).i(handle).run(); }
void releaseCar(int handle) { if (sa::vehicleFromHandle(handle)) Call(MARK_CAR_AS_NO_LONGER_NEEDED).i(handle).run(); }

} // namespace scm
