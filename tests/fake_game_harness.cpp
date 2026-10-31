// "Fake game" harness for LBN Coop SA (Windows x86, runs under Wine on the build box).
//
// It maps memory at the GTA SA 1.0 US address range, writes the 1.0 US (Hoodlum) signature, puts
// CALL instructions at the hook sites and JMP stubs at every game function the plugin uses, then
// loads LbnCoopSA.asi with LoadLibrary and drives a fake game loop. A tiny fake SCM interpreter
// checks that every script command the plugin emits has the opcode/parameter layout of SA, and
// simulates the few effects the plugin relies on (create/delete ped & car, warp into car).
// A real network client (from lbn_common) plays the "friend" against the plugin's embedded host.
//
// This proves the plugin's plumbing (loading, hooks, ini, log, host session, state capture,
// SCM bytecode, avatar logic, overlay calls). It does NOT prove behaviour inside the real game.
#include "../src/common/net_client.h"
#include "../src/common/net_common.h"
#include "../src/common/mathutil.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace lbn;

static int g_failures = 0;
static std::set<uintptr_t> g_ourBlocks;
#define EXPECT(c) do { if (!(c)) { std::printf("  FALHOU linha %d: %s\n", __LINE__, #c); g_failures++; } else { std::printf("  ok: %s\n", #c); } } while (0)

// ---------------- fake game objects ----------------
struct FakeEntity { uint8_t* mem; uint8_t* matrix; bool alive; bool isVehicle; };
static std::map<int, FakeEntity> g_entities; // handle -> entity
static int g_nextHandle = 0x100;
static std::map<uint16_t, int> g_opCount;
static std::vector<std::string> g_errors;
static int g_rwMatrixCalls = 0, g_removeAndAddCalls = 0, g_headingCalls = 0, g_prints = 0, g_screenCalls = 0;
static std::vector<std::string> g_anims;
static int g_playerHandle = -1;
static int g_areaBlockedReplies = 0;   // IS_AREA_OCCUPIED answers "occupied" this many times
static float g_groundZ = 12.3f;
// stage 2 fake game state
static int g_wanted = 0, g_hour = 14, g_minute = 30, g_weatherForced = -1, g_weatherReleased = 0;
static std::set<int> g_requestedModels;
static int g_lastGiveWeapon = -1, g_removeAllWeapons = 0, g_explodeCalls = 0;
static int g_lastProofs[5] = {-1, -1, -1, -1, -1};
static float g_carDensity = -1.f, g_pedDensity = -1.f;
static uint8_t* g_poolObjects = nullptr; static uint8_t g_poolFlags[3] = {0x80, 0x80, 0x80};

template <class T> static T& F(void* base, uint32_t o) { return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + o); }

static uint8_t* newEntity(bool vehicle, uint16_t model, float x, float y, float z, int* handleOut) {
    size_t sz = vehicle ? 0x600 : 0x800;
    uint8_t* m = static_cast<uint8_t*>(std::calloc(1, sz));
    uint8_t* mat = static_cast<uint8_t*>(std::calloc(1, 0x48));
    F<float>(mat, 0x00) = 1; F<float>(mat, 0x14) = 1; F<float>(mat, 0x28) = 1; // identity
    F<float>(mat, 0x30) = x; F<float>(mat, 0x34) = y; F<float>(mat, 0x38) = z;
    F<void*>(m, 0x14) = mat;
    F<int16_t>(m, 0x22) = int16_t(model);
    if (vehicle) F<uint8_t>(m, 0x488) = 3; // max passengers
    int h = g_nextHandle; g_nextHandle += 0x100;
    g_entities[h] = {m, mat, true, vehicle};
    if (handleOut) *handleOut = h;
    return m;
}

// Puts a vehicle into slot `slot` of the fake CPools::ms_pVehiclePool.
static uint8_t* newPoolVehicle(int slot, uint16_t model, uint8_t createdBy, float x, float y, float z, int* handleOut) {
    uint8_t* m = g_poolObjects + slot * 0xA18;
    uint8_t* mat = static_cast<uint8_t*>(std::calloc(1, 0x48));
    F<float>(mat, 0x00) = 1; F<float>(mat, 0x14) = 1; F<float>(mat, 0x28) = 1;
    F<float>(mat, 0x30) = x; F<float>(mat, 0x34) = y; F<float>(mat, 0x38) = z;
    F<void*>(m, 0x14) = mat; F<int16_t>(m, 0x22) = int16_t(model); F<uint8_t>(m, 0x4A4) = createdBy;
    F<uint8_t>(m, 0x488) = 3; F<float>(m, 0x4C0) = 1000.f;
    int h = g_nextHandle; g_nextHandle += 0x100;
    g_entities[h] = {m, mat, true, true};
    g_poolFlags[slot] = 0;
    if (handleOut) *handleOut = h;
    return m;
}

static void* entityByHandle(int h, bool vehicle) {
    auto it = g_entities.find(h);
    if (it == g_entities.end() || !it->second.alive || it->second.isVehicle != vehicle) return nullptr;
    return it->second.mem;
}
static int handleOf(void* p) {
    for (auto& kv : g_entities) if (kv.second.mem == p && kv.second.alive) return kv.first;
    return -1;
}

// ---------------- fake game functions ----------------
static void* __cdecl fGetPed(int h) { return entityByHandle(h, false); }
static void* __cdecl fGetVehicle(int h) { return entityByHandle(h, true); }
static int __cdecl fGetVehicleRef(void* v) { return handleOf(v); }
static void __thiscall fUpdateRwMatrix(void*) { g_rwMatrixCalls++; }
static void __thiscall fUpdateRwFrame(void*) {}
static void __thiscall fRemoveAndAdd(void*) { g_removeAndAddCalls++; }
static void __thiscall fSetHeading(void* ent, float h) {
    g_headingCalls++;
    uint8_t* mat = F<uint8_t*>(ent, 0x14);
    if (mat) { F<float>(mat, 0x10) = -std::sin(h); F<float>(mat, 0x14) = std::cos(h); }
}
static void __cdecl fPrintString(float, float, const char*) { g_prints++; }
struct V3 { float x, y, z; };
static bool __cdecl fCalcScreen(const V3& in, V3* out, float* w, float* h, bool, bool) {
    g_screenCalls++;
    out->x = 960; out->y = 540; out->z = 10; *w = 1; *h = 1;
    return true;
}
static void __thiscall fScriptInit(void* sc) {
    std::memset(static_cast<uint8_t*>(sc) + 0x18, 0, 0xE0 - 0x18);
}

// Expected parameter counts of the SA opcodes the plugin uses.
static const std::map<uint16_t, int> kParamCount = {
    {0x009A, 6}, {0x009B, 1}, {0x00A1, 4}, {0x00A5, 5}, {0x00A6, 1}, {0x01B4, 2}, {0x01C3, 1},
    {0x0229, 3}, {0x0247, 1}, {0x0248, 1}, {0x0249, 1}, {0x02AB, 6}, {0x02AC, 6}, {0x0362, 4},
    {0x036A, 2}, {0x038B, 0}, {0x039E, 2}, {0x0430, 3}, {0x0568, 2}, {0x0605, 9}, {0x0687, 1},
    {0x087E, 2}, {0x0918, 2},
    {0x01F5, 2}, {0x0619, 2}, {0x02CE, 4}, {0x0339, 11},
    // v0.1.2 (stage 2), counts checked against Sanny Builder's sa.json
    {0x01B2, 3}, {0x01B9, 2}, {0x048F, 1}, {0x0223, 2}, {0x020B, 1}, {0x010D, 2}, {0x010E, 2},
    {0x0110, 1}, {0x01C0, 2}, {0x00BF, 2}, {0x00C0, 2}, {0x01B6, 1}, {0x01B7, 0}, {0x01EB, 1},
    {0x03DE, 1}, {0x02A9, 2}, {0x0446, 2},
};

struct Param { uint8_t type; int32_t i; float f; std::string s; };

static char __thiscall fProcessOne(void* sc) {
    uint8_t* ip = F<uint8_t*>(sc, 0x14);
    uint16_t op; std::memcpy(&op, ip, 2); ip += 2;
    g_opCount[op]++;
    auto pc = kParamCount.find(op);
    if (pc == kParamCount.end()) { g_errors.push_back("opcode inesperado " + std::to_string(op)); return 0; }
    std::vector<Param> ps;
    for (int k = 0; k < pc->second; ++k) {
        Param p{}; p.type = *ip++;
        switch (p.type) {
        case 0x01: std::memcpy(&p.i, ip, 4); ip += 4; break;
        case 0x04: p.i = int8_t(*ip); ip += 1; break;
        case 0x05: { int16_t v; std::memcpy(&v, ip, 2); p.i = v; ip += 2; break; }
        case 0x06: std::memcpy(&p.f, ip, 4); ip += 4; break;
        case 0x03: { uint16_t v; std::memcpy(&v, ip, 2); p.i = v; ip += 2; break; }
        case 0x0E: { uint8_t n = *ip++; p.s.assign(reinterpret_cast<char*>(ip), n); ip += n; break; }
        default: g_errors.push_back("tipo de parametro invalido no opcode " + std::to_string(op)); return 0;
        }
        ps.push_back(p);
    }
    // Next bytes must be our zero padding (no extra params encoded).
    if (ip[0] != 0 || ip[1] != 0) g_errors.push_back("parametros a mais no opcode " + std::to_string(op));
    F<uint8_t*>(sc, 0x14) = ip;
    auto setLocal = [&](const Param& p, int32_t v) {
        if (p.type != 0x03) { g_errors.push_back("saida nao e variavel local no opcode " + std::to_string(op)); return; }
        F<int32_t>(sc, 0x3C + 4 * p.i) = v;
    };
    auto fl = [&](int k) { return ps[k].type == 0x06 ? ps[k].f : float(ps[k].i); };
    F<uint8_t>(sc, 0xC5) = 0;
    switch (op) {
    case 0x009A: { int h; newEntity(false, uint16_t(ps[1].i), fl(2), fl(3), fl(4), &h); setLocal(ps[5], h); break; }
    case 0x00A5: { int h; newEntity(true, uint16_t(ps[0].i), fl(1), fl(2), fl(3), &h); setLocal(ps[4], h); break; }
    case 0x009B: case 0x00A6: {
        auto it = g_entities.find(ps[0].i);
        if (it != g_entities.end()) {
            it->second.alive = false;
            for (int s = 0; s < 3; ++s) if (g_poolObjects && it->second.mem == g_poolObjects + s * 0xA18) g_poolFlags[s] = 0x80;
        }
        break;
    }
    case 0x0223: { void* ped = fGetPed(ps[0].i); if (ped) F<float>(ped, 0x540) = float(ps[1].i); else g_errors.push_back("SET_CHAR_HEALTH handle invalido"); break; }
    case 0x01C0: setLocal(ps[1], g_wanted); break;
    case 0x010D: g_wanted = ps[1].i; break;
    case 0x010E: g_wanted = std::max(g_wanted, ps[1].i); break;
    case 0x0110: g_wanted = 0; break;
    case 0x00BF: setLocal(ps[0], g_hour); setLocal(ps[1], g_minute); break;
    case 0x00C0: g_hour = ps[0].i; g_minute = ps[1].i; break;
    case 0x01B6: g_weatherForced = ps[0].i; break;
    case 0x01B7: g_weatherReleased++; break;
    case 0x01EB: g_carDensity = fl(0); break;
    case 0x03DE: g_pedDensity = fl(0); break;
    case 0x01B2: { g_lastGiveWeapon = ps[1].i; void* ped = fGetPed(ps[0].i); if (ped) F<int>(ped, 0x5A0) = ps[1].i; break; }
    case 0x048F: g_removeAllWeapons++; break;
    case 0x020B: g_explodeCalls++; break;
    case 0x0247: g_requestedModels.insert(ps[0].i); break;
    case 0x02AB: for (int k = 0; k < 5; ++k) g_lastProofs[k] = ps[1 + k].i; break;
    case 0x0248: F<uint8_t>(sc, 0xC5) = 1; break;
    case 0x036A: case 0x0430: {
        void* ped = fGetPed(ps[0].i); void* car = fGetVehicle(ps[1].i);
        if (!ped || !car) { g_errors.push_back("warp com handle invalido"); break; }
        F<uint32_t>(ped, 0x46C) |= (1u << 8); F<void*>(ped, 0x58C) = car;
        if (op == 0x036A) F<void*>(car, 0x460) = ped; else F<void*>(car, 0x464 + 4 * ps[2].i) = ped;
        break;
    }
    case 0x0362: {
        void* ped = fGetPed(ps[0].i);
        if (ped) {
            void* car = F<void*>(ped, 0x58C);
            if (car) { if (F<void*>(car, 0x460) == ped) F<void*>(car, 0x460) = nullptr;
                       for (int s = 0; s < 8; ++s) if (F<void*>(car, 0x464 + 4 * s) == ped) F<void*>(car, 0x464 + 4 * s) = nullptr; }
            F<uint32_t>(ped, 0x46C) &= ~(1u << 8); F<void*>(ped, 0x58C) = nullptr;
        }
        break;
    }
    case 0x0605: g_anims.push_back(ps[1].s + "/" + ps[2].s); break;
    case 0x0619: { void* ped = fGetPed(ps[0].i); if (ped) { if (ps[1].i) F<uint32_t>(ped, 0x1C) |= 1u; else F<uint32_t>(ped, 0x1C) &= ~1u; } break; }
    case 0x01F5: setLocal(ps[1], g_playerHandle); break;
    case 0x02CE: { int32_t bits; std::memcpy(&bits, &g_groundZ, 4); setLocal(ps[3], bits); break; }
    case 0x0339: if (g_areaBlockedReplies > 0) { g_areaBlockedReplies--; F<uint8_t>(sc, 0xC5) = 1; } break;
    case 0x00A1: {
        void* ped = fGetPed(ps[0].i);
        if (ped) { uint8_t* mat = F<uint8_t*>(ped, 0x14); F<float>(mat, 0x30) = fl(1); F<float>(mat, 0x34) = fl(2); F<float>(mat, 0x38) = fl(3) + 1.f; }
        break;
    }
    default: break;
    }
    return 0;
}

// ---------------- memory setup ----------------
static void writeJmp(uintptr_t at, void* to) {
    uint8_t* p = reinterpret_cast<uint8_t*>(at);
    p[0] = 0xE9; int32_t rel = int32_t(reinterpret_cast<uintptr_t>(to) - (at + 5)); std::memcpy(p + 1, &rel, 4);
}
static void writeCallRet(uintptr_t at, void* to) {
    uint8_t* p = reinterpret_cast<uint8_t*>(at);
    p[0] = 0xE8; int32_t rel = int32_t(reinterpret_cast<uintptr_t>(to) - (at + 5)); std::memcpy(p + 1, &rel, 4);
    p[5] = 0xC3;
}
static void writeCall(uintptr_t at, uintptr_t to) {
    uint8_t* p = reinterpret_cast<uint8_t*>(at);
    p[0] = 0xE8; int32_t rel = int32_t(to - (at + 5)); std::memcpy(p + 1, &rel, 4);
}

// ---------------- v0.1.3: video modes, psSelectDevice fragment, game window ----------------
struct FakeVideoMode { int32_t width, height, depth, flags, refRate, format; };
static const FakeVideoMode kModes[3] = {{1920, 1080, 32, 1, 60, 0}, {1280, 720, 32, 1, 60, 0}, {1920, 1080, 32, 0, 0, 0}};
static FakeVideoMode g_vmi{};
static int g_setModeArg = -1, g_menuActivations = 0, g_soundPauses = 0;
static int __cdecl fGetNumModes() { return 3; }
static FakeVideoMode* __cdecl fGetModeInfo(FakeVideoMode* out, int i) { if (i >= 0 && i < 3) *out = kModes[i]; return out; }
static int __cdecl fSetMode(int i) { g_setModeArg = i; return 1; }

// The bytes of psSelectDevice around its two calls (sites 0x7462D1 and 0x7462EA, as in the 1.0 US exe).
static void writePsSelectDeviceFragment() {
    uint8_t* p = reinterpret_cast<uint8_t*>(0x7462C0);
    std::memset(p, 0x90, 0x40);
    const uint8_t a[] = {0x8B, 0x15, 0x20, 0x62, 0x8D, 0x00, 0x52, 0x68}; // mov edx,[GcurSelVM]; push edx; push imm32
    std::memcpy(p, a, sizeof a);
    uint32_t vmi = uint32_t(reinterpret_cast<uintptr_t>(&g_vmi));
    std::memcpy(p + 8, &vmi, 4);
    writeCall(0x7462D1, 0x7F2CF0);                                          // RwEngineGetVideoModeInfo(&vm, GcurSelVM)
    const uint8_t b[] = {0x83, 0xC4, 0x08, 0x8B, 0x0D, 0x20, 0x62, 0x8D, 0x00, 0x51}; // add esp,8; mov ecx,[GcurSelVM]; push ecx
    std::memcpy(p + 0x16, b, sizeof b);
    writeCall(0x7462EA, 0x7F2D50);                                          // RwEngineSetVideoMode(GcurSelVM)
    const uint8_t c[] = {0x83, 0xC4, 0x04, 0xC3};                           // add esp,4; ret
    std::memcpy(p + 0x2F, c, sizeof c);
}

// Mimics what the game's WndProc does with the focus messages (gta-reversed WndProc.cpp).
static LRESULT CALLBACK fakeGameWndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    uint8_t& fg = *reinterpret_cast<uint8_t*>(0x8D621C);
    switch (m) {
    case WM_ACTIVATE: fg = LOWORD(wp) != WA_INACTIVE; if (!fg) g_soundPauses++; return 0;
    case WM_KILLFOCUS: fg = 0; return 0;
    case WM_SETFOCUS: fg = 1; g_menuActivations++; return 0;
    default: return DefWindowProcA(h, m, wp, lp);
    }
}
static HWND makeGameWindow() {
    WNDCLASSA wc{};
    wc.lpfnWndProc = fakeGameWndProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "Grand theft auto San Andreas";
    if (!RegisterClassA(&wc)) return nullptr;
    return CreateWindowExA(0, wc.lpszClassName, "GTA: San Andreas", WS_POPUP, 0, 0, 640, 480, nullptr, nullptr, wc.hInstance, nullptr);
}

static int g_origProcessCalls = 0, g_origDrawCalls = 0, g_origShutdownCalls = 0, g_origInitScriptsCalls = 0;
static void __cdecl origProcess() { g_origProcessCalls++; }
static void __cdecl origDraw() { g_origDrawCalls++; }
static void __cdecl origShutdown() { g_origShutdownCalls++; }
static void __cdecl origInitScripts() { g_origInitScriptsCalls++; }
static void callSite(uintptr_t site) { reinterpret_cast<void(__cdecl*)()>(site)(); }

static int childMain(int argc, char** argv) {
    if (argc < 2) { std::printf("uso: fake_game_harness <caminho\\LbnCoopSA.asi> [porta]\n"); return 2; }
    int port = argc > 2 ? std::atoi(argv[2]) : 7899;
    // The parent maps the game address range into us before we started (see runParent()).
    {
        MEMORY_BASIC_INFORMATION mbi{};
        VirtualQuery(reinterpret_cast<void*>(0x401000), &mbi, sizeof mbi);
        if (!(mbi.Protect & PAGE_EXECUTE_READWRITE)) { std::printf("memoria do jogo nao mapeada\n"); return 3; }
    }
    *reinterpret_cast<uint32_t*>(0x401000) = 0x16197BE9u; // 1.0 US Hoodlum signature

    // Hook sites (CALL + RET so we can call them like functions).
    writeCallRet(0x53E981, reinterpret_cast<void*>(&origProcess));
    writeCallRet(0x53E293, reinterpret_cast<void*>(&origDraw));
    writeCallRet(0x53D910, reinterpret_cast<void*>(&origShutdown));
    writeCallRet(0x53BDD7, reinterpret_cast<void*>(&origInitScripts));
    writeCallRet(0x5BA340, reinterpret_cast<void*>(&origInitScripts));
    writeCallRet(0x5D4FD7, reinterpret_cast<void*>(&origInitScripts));
    // Game functions.
    writeJmp(0x54FF90, reinterpret_cast<void*>(&fGetPed));
    writeJmp(0x54FFF0, reinterpret_cast<void*>(&fGetVehicle));
    writeJmp(0x54FFC0, reinterpret_cast<void*>(&fGetVehicleRef));
    writeJmp(0x446F90, reinterpret_cast<void*>(&fUpdateRwMatrix));
    writeJmp(0x532B00, reinterpret_cast<void*>(&fUpdateRwFrame));
    writeJmp(0x542560, reinterpret_cast<void*>(&fRemoveAndAdd));
    writeJmp(0x43E0C0, reinterpret_cast<void*>(&fSetHeading));
    writeJmp(0x4648E0, reinterpret_cast<void*>(&fScriptInit));
    writeJmp(0x469EB0, reinterpret_cast<void*>(&fProcessOne));
    writeJmp(0x70CE30, reinterpret_cast<void*>(&fCalcScreen));
    writeJmp(0x71A700, reinterpret_cast<void*>(&fPrintString));
    const uintptr_t fontFns[] = {0x719380, 0x719420, 0x719430, 0x719490, 0x7194D0, 0x7194E0, 0x7194F0, 0x719500,
                                 0x719510, 0x719570, 0x719590, 0x7195B0, 0x7195C0, 0x7195E0, 0x719600, 0x719610};
    for (uintptr_t a : fontFns) *reinterpret_cast<uint8_t*>(a) = 0xC3; // cdecl: plain RET is fine
    *reinterpret_cast<int*>(0xC17044) = 1920;
    *reinterpret_cast<int*>(0xC17048) = 1080;
    *reinterpret_cast<int16_t*>(0xC8131C) = 8; // CWeather::NewWeatherType = rainy
    // Fake CPools::ms_pVehiclePool with 3 slots (filled in step [4c]).
    g_poolObjects = static_cast<uint8_t*>(std::calloc(3, 0xA18));
    {
        static uint8_t pool[16];
        F<uint8_t*>(pool, 0) = g_poolObjects; F<uint8_t*>(pool, 4) = g_poolFlags; F<int>(pool, 8) = 3; F<int>(pool, 12) = 0;
        *reinterpret_cast<uint8_t**>(0xB74494) = pool;
    }
    // v0.1.3: IsAlreadyRunning ("mov eax,[0x8D6224]" ... "mov al,1; ret" = another copy is running),
    // RenderWare video mode functions, the psSelectDevice fragment and the game window.
    const uint8_t alreadyRunning[] = {0xA1, 0x24, 0x62, 0x8D, 0x00, 0xB0, 0x01, 0xC3};
    std::memcpy(reinterpret_cast<void*>(0x7468E0), alreadyRunning, sizeof alreadyRunning);
    writeJmp(0x7F2CC0, reinterpret_cast<void*>(&fGetNumModes));
    writeJmp(0x7F2CF0, reinterpret_cast<void*>(&fGetModeInfo));
    writeJmp(0x7F2D50, reinterpret_cast<void*>(&fSetMode));
    writePsSelectDeviceFragment();
    *reinterpret_cast<int32_t*>(0x8D6220) = 1;   // GcurSelVM: the menu picked the 1280x720 fullscreen mode
    *reinterpret_cast<uint8_t*>(0x8D621C) = 1;   // ForegroundApp
    HWND gameWnd = makeGameWindow();
    *reinterpret_cast<HWND*>(0xC8CF88) = gameWnd; // PsGlobal.window
    if (!gameWnd) std::printf("  (nao consegui criar a janela falsa: testes de foco e de janela pulados)\n");

    // Write the ini next to the ASI: host mode, auto start.
    std::string asi = argv[1];
    std::string dir = asi.substr(0, asi.find_last_of("\\/"));
    std::string ini = dir + "\\LbnCoopSA.ini";
    if (FILE* f = std::fopen(ini.c_str(), "wb")) {
        std::fprintf(f, "[LbnCoopSA]\nNickname=HostLBN\nMode=host\nPort=%d\nSkin=107\nAutoStart=1\nSendRate=30\nInterpDelayMs=120\n"
                        "[Janela]\nJanela=1\nLarguraJanela=800\nAlturaJanela=600\nDuasCopias=1\nSegundoPlano=1\n", port);
        std::fclose(f);
    }
    SetEnvironmentVariableA("LBN_COOP_NO_DIALOG", "1");
    SetEnvironmentVariableA("LBN_COOP_TEST", "1");

    std::printf("[1] Carregando o ASI...\n");
    HMODULE h = LoadLibraryA(asi.c_str());
    EXPECT(h != nullptr);
    if (!h) return 1;
    auto hookedAway = [](uintptr_t site, void* orig) {
        int32_t rel; std::memcpy(&rel, reinterpret_cast<void*>(site + 1), 4);
        return site + 5 + uintptr_t(intptr_t(rel)) != reinterpret_cast<uintptr_t>(orig);
    };
    EXPECT(hookedAway(0x53E981, reinterpret_cast<void*>(&origProcess)));
    EXPECT(hookedAway(0x53E293, reinterpret_cast<void*>(&origDraw)));
    EXPECT(hookedAway(0x53D910, reinterpret_cast<void*>(&origShutdown)));
    EXPECT(hookedAway(0x5D4FD7, reinterpret_cast<void*>(&origInitScripts)));

    std::printf("[1b] v0.1.3: duas copias, mouse da janela de tras, modo janela...\n");
    {
        const uint8_t never[] = {0x33, 0xC0, 0xC3};
        EXPECT(std::memcmp(reinterpret_cast<void*>(0x7468E0), never, sizeof never) == 0);
        EXPECT(reinterpret_cast<bool(__cdecl*)()>(0x7468E0)() == false);   // the second copy may open
        // The (hidden) game window is not in front: the game must not move or confine the mouse.
        POINT before{};
        if (gameWnd && GetCursorPos(&before)) {
            SetCursorPos(before.x + 37, before.y + 11);
            POINT after{}; GetCursorPos(&after);
            EXPECT(after.x == before.x && after.y == before.y);
            RECT clipBefore{}, clipAfter{}, tiny{0, 0, 10, 10};
            GetClipCursor(&clipBefore);
            ClipCursor(&tiny);
            GetClipCursor(&clipAfter);
            ClipCursor(nullptr);                                            // releasing always goes through
            EXPECT(std::memcmp(&clipBefore, &clipAfter, sizeof clipBefore) == 0);
        } else std::printf("  (sem cursor nesta sessao: teste do mouse pulado)\n");
        // psSelectDevice with the 1280x720 fullscreen mode selected: the windowed mode (2) is used instead.
        callSite(0x7462C0);
        std::printf("  modo escolhido=%d GcurSelVM=%d flags=%d tela=%dx%d\n", g_setModeArg, *reinterpret_cast<int32_t*>(0x8D6220),
                    g_vmi.flags, *reinterpret_cast<int*>(0xC17044), *reinterpret_cast<int*>(0xC17048));
        EXPECT(g_setModeArg == 2 && *reinterpret_cast<int32_t*>(0x8D6220) == 2);
        EXPECT(g_vmi.flags == 0);                                          // the rest of psSelectDevice goes the windowed way
        EXPECT(*reinterpret_cast<uint8_t*>(0xC920CC) == 1);                // Windowed
        EXPECT(*reinterpret_cast<int*>(0xC17044) == 800 && *reinterpret_cast<int*>(0xC17048) == 600);
        if (gameWnd) {
            RECT cr{}; GetClientRect(gameWnd, &cr);
            EXPECT((GetWindowLongA(gameWnd, GWL_STYLE) & WS_CAPTION) == WS_CAPTION);
            EXPECT(cr.right - cr.left == 800 && cr.bottom - cr.top == 600);
        }
        *reinterpret_cast<int*>(0xC17044) = 1920;                         // back to the size the overlay tests use
        *reinterpret_cast<int*>(0xC17048) = 1080;
    }

    std::printf("[2] Frames sem jogador (carregando)...\n");
    for (int i = 0; i < 30; ++i) { callSite(0x53E981); callSite(0x53E293); }
    EXPECT(g_origProcessCalls == 30 && g_origDrawCalls == 30);
    if (gameWnd) {
        // No session yet: losing the focus still reaches the game (normal pause).
        *reinterpret_cast<uint8_t*>(0x8D621C) = 1;
        SendMessageA(gameWnd, WM_KILLFOCUS, 0, 0);
        EXPECT(*reinterpret_cast<uint8_t*>(0x8D621C) == 0);
        *reinterpret_cast<uint8_t*>(0x8D621C) = 1;
    }

    std::printf("[3] Criando o CJ falso e deixando o AutoStart hospedar...\n");
    int playerHandle;
    uint8_t* player = newEntity(false, 0, 2495.f, -1665.f, 13.3f, &playerHandle);
    g_playerHandle = playerHandle;
    F<uint32_t>(player, 0x1C) |= 1u; // the real player uses collision
    F<float>(player, 0x540) = 100.f; F<float>(player, 0x558) = 0.5f; F<int>(player, 0x534) = 6; // running
    F<float>(player, 0x44) = 0.1f; // 5 m/s along X
    *reinterpret_cast<void**>(0xB7CD98) = player;

    Client friendC;
    bool friendConnected = false;
    std::vector<ClientEvent> got;
    auto frame = [&]() {
        callSite(0x53E981); callSite(0x53E293);
        friendC.service(0);
        ClientEvent e; while (friendC.pollEvent(e)) got.push_back(e);
        Sleep(16);
    };
    for (int i = 0; i < 10; ++i) frame(); // AutoStart -> embedded server + local client
    // Online but alone: the ESC menu still pauses the game as usual.
    *reinterpret_cast<uint8_t*>(0xBA67A4) = 1; *reinterpret_cast<uint8_t*>(0xB7CB49) = 1;
    frame();
    EXPECT(*reinterpret_cast<uint8_t*>(0xB7CB49) == 1);
    *reinterpret_cast<uint8_t*>(0xBA67A4) = 0; *reinterpret_cast<uint8_t*>(0xB7CB49) = 0;
    std::printf("[4] Amigo (cliente de rede real) entrando no host do ASI...\n");
    EXPECT(friendC.connect("127.0.0.1", uint16_t(port), "Amigo", 106, 4000));
    uint32_t t0 = nowMs();
    while (nowMs() - t0 < 4000 && !friendC.connected()) frame();
    friendConnected = friendC.connected();
    EXPECT(friendConnected);

    // Friend: walk for 1.5 s, then drive a Sultan (560) for 1.5 s; also chat.
    uint32_t seq = 0;
    uint8_t fWeapon = 24, fWanted = 0; float fCarHealth = 600.f;
    auto sendFriend = [&](bool inCar, float t) {
        PlayerState s; s.seq = ++seq; s.timeMs = nowMs();
        s.pos = {2500.f + 3.f * t, -1660.f, 13.4f}; s.vel = {3.f, 0, 0}; s.heading = -kPi / 2;
        s.anim = MoveAnim::Walk; s.health = 100; s.armour = 40; s.weapon = fWeapon; s.wanted = fWanted;
        s.veh.health = fCarHealth;
        if (inCar) {
            s.flags = PlayerState::InVehicle; s.seat = 0;
            s.veh.key = 4242; s.veh.model = 560; s.veh.pos = s.pos; s.veh.vel = s.vel; s.veh.color1 = 1; s.veh.color2 = 3;
            s.veh.rot = quatFromAxes({0, -1, 0}, {1, 0, 0}, {0, 0, 1});
        }
        friendC.sendState(s);
    };
    friendC.sendChat("oi host, aqui e o amigo");
    t0 = nowMs();
    uint32_t lastSend = 0;
    while (nowMs() - t0 < 1500) { if (nowMs() - lastSend >= 50) { lastSend = nowMs(); sendFriend(false, (nowMs() - t0) / 1000.f); } frame(); }
    int pedCreates = g_opCount[0x009A];
    EXPECT(pedCreates == 1);
    EXPECT(!g_anims.empty() && g_anims.back() == "WALK_player/PED");
    // Find the avatar and check it follows the friend's path.
    void* avatar = nullptr;
    for (auto& kv : g_entities) if (kv.second.alive && !kv.second.isVehicle && kv.second.mem != player) avatar = kv.second.mem;
    EXPECT(avatar != nullptr);
    if (avatar) {
        float ax = F<float>(F<uint8_t*>(avatar, 0x14), 0x30);
        float expected = 2500.f + 3.f * 1.4f; // ~100 ms interpolation delay
        std::printf("  avatar x=%.2f (esperado ~%.2f)\n", ax, expected);
        EXPECT(std::fabs(ax - expected) < 1.0f);
        EXPECT(F<int16_t>(avatar, 0x22) == 106);
        // v0.1.1: the avatar must not collide with the local player. v0.1.2 (PvP on, host default): it keeps
        // collision only while it is away (> 1.6 m) from the local player, so bullets can hit it.
        EXPECT(g_opCount[0x0619] >= 1);
        EXPECT((F<uint32_t>(avatar, 0x1C) & 1u) == 1u);      // far away + PvP: collision on (shootable)
        EXPECT((F<uint32_t>(avatar, 0x46C) & 1u) == 1u);     // kept "standing" (no fall anim)
        EXPECT((F<uint32_t>(player, 0x1C) & 1u) == 1u);      // local player untouched
        // The local player walks into the avatar: its collision must go off at once.
        uint8_t* pm0 = F<uint8_t*>(player, 0x14); uint8_t* am0 = F<uint8_t*>(avatar, 0x14);
        uint32_t tc = nowMs();
        while (nowMs() - tc < 200) {
            if (nowMs() - lastSend >= 33) { lastSend = nowMs(); sendFriend(false, 1.5f); }
            F<float>(pm0, 0x30) = F<float>(am0, 0x30) + 0.5f; F<float>(pm0, 0x34) = F<float>(am0, 0x34);
            frame();
        }
        EXPECT((F<uint32_t>(avatar, 0x1C) & 1u) == 0);       // CEntity::bUsesCollision off while close
        // The game re-enables collision e.g. after leaving a car: the plugin must turn it off again.
        F<uint32_t>(avatar, 0x1C) |= 1u;
        tc = nowMs();
        while (nowMs() - tc < 200) {
            if (nowMs() - lastSend >= 33) { lastSend = nowMs(); sendFriend(false, 1.5f); }
            F<float>(pm0, 0x30) = F<float>(am0, 0x30) + 0.5f; F<float>(pm0, 0x34) = F<float>(am0, 0x34);
            frame();
        }
        EXPECT((F<uint32_t>(avatar, 0x1C) & 1u) == 0);
    }

    std::printf("[4b] /soltar e F6 (menu aberto)...\n");
    {
        using InjectFn = void(__cdecl*)(int, const char*);
        auto inject = reinterpret_cast<InjectFn>(GetProcAddress(h, "LbnCoopSA_TestInject"));
        EXPECT(inject != nullptr);
        uint8_t* pm = F<uint8_t*>(player, 0x14);
        auto ppos = [&]() { return Vec3{F<float>(pm, 0x30), F<float>(pm, 0x34), F<float>(pm, 0x38)}; };
        // Put the local player right on top of the avatar (the reported bug) and ask to get free.
        if (avatar) { uint8_t* am = F<uint8_t*>(avatar, 0x14); F<float>(pm, 0x30) = F<float>(am, 0x30); F<float>(pm, 0x34) = F<float>(am, 0x34); }
        Vec3 before = ppos();
        g_areaBlockedReplies = 2; // first two candidate spots are "occupied" (wall/car)
        int setCoords = g_opCount[0x00A1];
        if (inject) inject(6, "/soltar");
        for (int i = 0; i < 3; ++i) frame();
        Vec3 after = ppos();
        float moved = (Vec3{after.x - before.x, after.y - before.y, 0}).length();
        std::printf("  /soltar: movido %.2f m, z=%.2f\n", moved, after.z);
        EXPECT(g_opCount[0x00A1] == setCoords + 1);
        EXPECT(moved > 2.4f && moved < 5.1f);
        EXPECT(std::fabs(after.z - (g_groundZ + 1.f)) < 0.01f);
        EXPECT(g_opCount[0x0339] >= 3);
        // F6 only acts while the F8 menu is open.
        Sleep(2100);
        if (inject) inject(8, nullptr);       // F6 with the menu closed: ignored
        for (int i = 0; i < 3; ++i) frame();
        EXPECT(g_opCount[0x00A1] == setCoords + 1);
        if (inject) { inject(1, nullptr); inject(8, nullptr); } // F8 then F6
        for (int i = 0; i < 3; ++i) frame();
        EXPECT(g_opCount[0x00A1] == setCoords + 2);
        if (inject) inject(1, nullptr);       // close menu
        frame();
        F<float>(pm, 0x30) = 2495.f; F<float>(pm, 0x34) = -1665.f; F<float>(pm, 0x38) = 13.3f; // back for step [5]
    }

    std::printf("[4c] Etapa 2: arma, procurado, hora/clima, PvP...\n");
    uint8_t hostId = 0xFF;
    auto pumpFriend = [&](uint32_t ms) {
        uint32_t t = nowMs();
        while (nowMs() - t < ms) { if (nowMs() - lastSend >= 33) { lastSend = nowMs(); sendFriend(false, 1.5f); } frame(); }
    };
    {
        pumpFriend(300);
        for (auto& e : got) if (e.type == ClientEvent::Session) hostId = e.id;
        EXPECT(hostId == 0);                                  // the ASI's own client is the world authority
        // Weapon in hand: Desert Eagle (24) -> model 348 loaded, given and selected.
        EXPECT(g_lastGiveWeapon == 24);
        EXPECT(g_requestedModels.count(348) == 1);
        EXPECT(g_opCount[0x01B9] >= 1);
        int removes = g_removeAllWeapons;
        fWeapon = 0; pumpFriend(200);
        EXPECT(g_removeAllWeapons == removes + 1);            // fists: weapons taken away
        fWeapon = 31; pumpFriend(200);
        EXPECT(g_lastGiveWeapon == 31 && g_requestedModels.count(356) == 1);
        fWeapon = 20; pumpFriend(200);                         // invalid id: server relays 0 (fists)
        EXPECT(g_lastGiveWeapon == 31 && g_removeAllWeapons == removes + 3); // +1 before giving 31, +1 for fists
        // PvP proofs on the avatar (bullet 0, fire 1, explosion 0, collision 0, melee 0).
        EXPECT(g_lastProofs[0] == 0 && g_lastProofs[1] == 1 && g_lastProofs[2] == 0 && g_lastProofs[4] == 0);
        // Wanted level: the friend gets 3 stars -> the host (authority) is raised to 3.
        fWanted = 3; pumpFriend(300);
        EXPECT(g_wanted == 3);
        // World broadcast to the friend once per second.
        pumpFriend(1200);
        lbn::WorldState last; int worlds = 0;
        for (auto& e : got) if (e.type == ClientEvent::World) { last = e.world; worlds++; }
        std::printf("  mundos=%d hora=%02u:%02u clima=%d procurado=%u flags=%02X\n", worlds, last.hour, last.minute, last.weather, last.wanted, last.flags);
        EXPECT(worlds >= 1);
        EXPECT(last.hour == 14 && last.minute == 30 && last.weather == 8 && last.wanted == 3);
        EXPECT((last.flags & WorldState::PvP) && (last.flags & WorldState::SyncClock) && !(last.flags & WorldState::ForceDensity));
        EXPECT(g_carDensity < 0.f && g_pedDensity < 0.f);     // density 100% = never touched
        // The local player shoots the avatar (health 1000 -> 950 by the player with a pistol).
        if (avatar) {
            F<float>(avatar, 0x540) = 950.f; F<void*>(avatar, 0x764) = player; F<uint32_t>(avatar, 0x760) = 22;
            pumpFriend(300);
            bool hit = false;
            for (auto& e : got) if (e.type == ClientEvent::Damage && e.id == hostId && e.damage.amountX10 == 500 && e.damage.weapon == 22) hit = true;
            EXPECT(hit);
            EXPECT(std::fabs(F<float>(avatar, 0x540) - 1000.f) < 0.01f);  // copy back to full health
            // Damage not caused by the local player is not reported.
            size_t dmg = 0; for (auto& e : got) if (e.type == ClientEvent::Damage) dmg++;
            F<float>(avatar, 0x540) = 900.f; F<void*>(avatar, 0x764) = nullptr;
            pumpFriend(200);
            size_t dmg2 = 0; for (auto& e : got) if (e.type == ClientEvent::Damage) dmg2++;
            EXPECT(dmg2 == dmg);
        }
        // The friend shoots the host for 30 HP: 20 armour absorbs first, health 100 -> 90.
        F<float>(player, 0x548) = 20.f;
        lbn::DamageMsg d; d.peer = hostId; d.amountX10 = 300; d.weapon = 24;
        EXPECT(friendC.sendDamage(d));
        pumpFriend(300);
        std::printf("  host: vida=%.1f colete=%.1f\n", F<float>(player, 0x540), F<float>(player, 0x548));
        EXPECT(std::fabs(F<float>(player, 0x548)) < 0.01f);
        EXPECT(std::fabs(F<float>(player, 0x540) - 90.f) < 0.01f);
        F<float>(player, 0x540) = 100.f;
        fWanted = 0;
    }

    std::printf("[4d] Carro do amigo: vida, explosao e remocao de duplicata do transito...\n");
    int parkedH = -1, missionH = -1, otherH = -1;
    newPoolVehicle(0, 560, 3, 2505.f, -1660.f, 13.4f, &parkedH);   // parked Sultan where the friend's car is
    newPoolVehicle(1, 560, 2, 2505.f, -1660.f, 13.4f, &missionH);  // mission car: never touched
    newPoolVehicle(2, 400, 3, 2505.f, -1660.f, 13.4f, &otherH);    // other model: kept
    t0 = nowMs();
    while (nowMs() - t0 < 1000) { if (nowMs() - lastSend >= 50) { lastSend = nowMs(); sendFriend(true, 1.5f + (nowMs() - t0) / 1000.f); } frame(); }
    EXPECT(g_poolFlags[0] == 0x80 && !g_entities[parkedH].alive);   // duplicate parked copy removed
    EXPECT(g_poolFlags[1] == 0 && g_entities[missionH].alive);
    EXPECT(g_poolFlags[2] == 0 && g_entities[otherH].alive);
    {
        void* copy = nullptr;
        for (auto& kv : g_entities) if (kv.second.alive && kv.second.isVehicle && F<int16_t>(kv.second.mem, 0x22) == 560 && kv.first != missionH) copy = kv.second.mem;
        EXPECT(copy && std::fabs(F<float>(copy, 0x4C0) - 600.f) < 0.01f);
    }
    fCarHealth = 0.f;
    t0 = nowMs();
    while (nowMs() - t0 < 500) { if (nowMs() - lastSend >= 50) { lastSend = nowMs(); sendFriend(true, 2.5f + (nowMs() - t0) / 1000.f); } frame(); }
    EXPECT(g_explodeCalls == 1);
    EXPECT(g_opCount[0x00A5] == 1);  // CREATE_CAR
    EXPECT(g_opCount[0x036A] >= 1);  // WARP_CHAR_INTO_CAR
    EXPECT(g_opCount[0x0229] == 1);  // CHANGE_CAR_COLOUR
    void* car = nullptr;
    for (auto& kv : g_entities) if (kv.second.alive && kv.second.isVehicle && kv.first != missionH && kv.first != otherH) car = kv.second.mem;
    EXPECT(car != nullptr);
    if (car && avatar) {
        EXPECT(F<int16_t>(car, 0x22) == 560);
        EXPECT(F<void*>(car, 0x460) == avatar);
        uint8_t* m = F<uint8_t*>(car, 0x14);
        std::printf("  carro x=%.2f fwd=(%.2f, %.2f)\n", F<float>(m, 0x30), F<float>(m, 0x10), F<float>(m, 0x14));
        EXPECT(std::fabs(F<float>(m, 0x10) - 1.f) < 0.01f); // forward axis = +X as sent
    }

    std::printf("[4e] v0.1.3: segundo plano e pausa sem congelar com o amigo conectado...\n");
    {
        // A friend is in: the ESC menu must not freeze the world.
        *reinterpret_cast<uint8_t*>(0xBA67A4) = 1; *reinterpret_cast<uint8_t*>(0xB7CB49) = 1;
        frame();
        EXPECT(*reinterpret_cast<uint8_t*>(0xB7CB49) == 0);
        *reinterpret_cast<uint8_t*>(0xBA67A4) = 0;
        if (gameWnd) {
            // In a session, switching to another window keeps the game running and opens no pause menu.
            int pauses = g_soundPauses, menus = g_menuActivations;
            *reinterpret_cast<uint8_t*>(0x8D621C) = 1;
            SendMessageA(gameWnd, WM_ACTIVATE, WA_INACTIVE, 0);
            SendMessageA(gameWnd, WM_KILLFOCUS, 0, 0);
            EXPECT(*reinterpret_cast<uint8_t*>(0x8D621C) == 1);
            EXPECT(g_soundPauses == pauses);
            SendMessageA(gameWnd, WM_SETFOCUS, 0, 0);
            EXPECT(g_menuActivations == menus && *reinterpret_cast<uint8_t*>(0x8D621C) == 1);
            SendMessageA(gameWnd, WM_ACTIVATE, WA_ACTIVE, 0);                 // coming back still reaches the game
            EXPECT(*reinterpret_cast<uint8_t*>(0x8D621C) == 1);
        }
    }

    std::printf("[5] Estados do host recebidos pelo amigo...\n");
    int hostStates = 0; bool chatEcho = false; float lastHostX = 0;
    for (auto& e : got) {
        if (e.type == ClientEvent::State) { hostStates++; lastHostX = e.state.pos.x; }
        if (e.type == ClientEvent::Chat && e.text == "oi host, aqui e o amigo") chatEcho = true;
    }
    std::printf("  estados do host=%d ultimo x=%.2f\n", hostStates, lastHostX);
    EXPECT(hostStates > 20);
    EXPECT(std::fabs(lastHostX - 2495.f) < 0.01f);
    EXPECT(chatEcho);
    EXPECT(g_prints > 0);       // overlay drew something (chat lines / nametag)
    EXPECT(g_screenCalls > 0);  // nametag projection
    EXPECT(g_removeAndAddCalls > 0 && g_rwMatrixCalls > 0 && g_headingCalls > 0);

    std::printf("[6] Novo jogo (CTheScripts::Init) -> o mod esquece handles antigos...\n");
    callSite(0x5D4FD7);
    EXPECT(g_origInitScriptsCalls == 1);

    std::printf("[7] Fechando o jogo (shutdown hook)...\n");
    callSite(0x53D910);
    EXPECT(g_origShutdownCalls == 1);
    t0 = nowMs();
    bool sawDisconnect = false;
    while (nowMs() - t0 < 3000 && !sawDisconnect) {
        friendC.service(10);
        ClientEvent e; while (friendC.pollEvent(e)) if (e.type == ClientEvent::Disconnected) sawDisconnect = true;
    }
    EXPECT(sawDisconnect);

    for (auto& err : g_errors) std::printf("  ERRO SCM: %s\n", err.c_str());
    EXPECT(g_errors.empty());
    std::printf("\nopcodes executados:");
    for (auto& kv : g_opCount) std::printf(" %04X x%d", kv.first, kv.second);
    std::printf("\n%s (%d falhas)\n", g_failures ? "HARNESS FALHOU" : "HARNESS OK", g_failures);
    friendC.disconnect(50);
    return g_failures ? 1 : 0;
}

// Parent: start ourselves suspended, map the GTA address blocks into the child before its heap
// exists (otherwise the process heap lands on 0x400000), then let it run.
static const uintptr_t kNeeded[] = {0x401000, 0x43E0C0, 0x446F90, 0x4648E0, 0x469EB0, 0x532B00, 0x53BDD7, 0x53D910,
                                    0x53E293, 0x53E981, 0x542560, 0x54FF90, 0x5BA340, 0x5D4FD7, 0x70CE30, 0x719380,
                                    0x71A700, 0xB7CB48, 0xB7CD98, 0xBA67A4, 0xC17044, 0xB74494, 0xC8131C,
                                    // v0.1.3: IsAlreadyRunning + psSelectDevice, RenderWare video modes,
                                    // ForegroundApp/GcurSelVM, Windowed, PsGlobal
                                    0x7468E0, 0x7F2CC0, 0x8D621C, 0xC920CC, 0xC8CF88};
static int runParent(int argc, char** argv) {
    char self[MAX_PATH];
    GetModuleFileNameA(nullptr, self, MAX_PATH);
    std::string cmd = std::string("\"") + self + "\" --child";
    for (int i = 1; i < argc; ++i) cmd += std::string(" \"") + argv[i] + "\"";
    STARTUPINFOA si{}; si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    std::vector<char> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED, nullptr, nullptr, &si, &pi)) {
        std::printf("CreateProcess falhou (%lu)\n", GetLastError()); return 4;
    }
    std::set<uintptr_t> done;
    for (uintptr_t a : kNeeded) {
        uintptr_t base = a & ~uintptr_t(0xFFFF);
        if (done.count(base)) continue;
        void* m = VirtualAllocEx(pi.hProcess, reinterpret_cast<void*>(base), 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (m != reinterpret_cast<void*>(base)) {
            std::printf("nao consegui mapear %08X no processo filho (%lu)\n", unsigned(base), GetLastError());
            TerminateProcess(pi.hProcess, 5); return 5;
        }
        done.insert(base);
    }
    ResumeThread(pi.hThread);
    WaitForSingleObject(pi.hProcess, 180000);
    DWORD code = 6;
    GetExitCodeProcess(pi.hProcess, &code);
    return int(code);
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc > 1 && std::strcmp(argv[1], "--child") == 0) return childMain(argc - 1, argv + 1);
    return runParent(argc, argv);
}
