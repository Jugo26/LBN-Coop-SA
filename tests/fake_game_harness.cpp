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
    case 0x009B: case 0x00A6: { auto it = g_entities.find(ps[0].i); if (it != g_entities.end()) it->second.alive = false; break; }
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

    // Write the ini next to the ASI: host mode, auto start.
    std::string asi = argv[1];
    std::string dir = asi.substr(0, asi.find_last_of("\\/"));
    std::string ini = dir + "\\LbnCoopSA.ini";
    if (FILE* f = std::fopen(ini.c_str(), "wb")) {
        std::fprintf(f, "[LbnCoopSA]\nNickname=HostLBN\nMode=host\nPort=%d\nSkin=107\nAutoStart=1\nSendRate=30\nInterpDelayMs=120\n", port);
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

    std::printf("[2] Frames sem jogador (carregando)...\n");
    for (int i = 0; i < 30; ++i) { callSite(0x53E981); callSite(0x53E293); }
    EXPECT(g_origProcessCalls == 30 && g_origDrawCalls == 30);

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
    std::printf("[4] Amigo (cliente de rede real) entrando no host do ASI...\n");
    EXPECT(friendC.connect("127.0.0.1", uint16_t(port), "Amigo", 106, 4000));
    uint32_t t0 = nowMs();
    while (nowMs() - t0 < 4000 && !friendC.connected()) frame();
    friendConnected = friendC.connected();
    EXPECT(friendConnected);

    // Friend: walk for 1.5 s, then drive a Sultan (560) for 1.5 s; also chat.
    uint32_t seq = 0;
    auto sendFriend = [&](bool inCar, float t) {
        PlayerState s; s.seq = ++seq; s.timeMs = nowMs();
        s.pos = {2500.f + 3.f * t, -1660.f, 13.4f}; s.vel = {3.f, 0, 0}; s.heading = -kPi / 2;
        s.anim = MoveAnim::Walk; s.health = 100;
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
        // v0.1.1: the avatar must not collide with the local player.
        EXPECT(g_opCount[0x0619] >= 1);
        EXPECT((F<uint32_t>(avatar, 0x1C) & 1u) == 0);       // CEntity::bUsesCollision off
        EXPECT((F<uint32_t>(avatar, 0x46C) & 1u) == 1u);     // kept "standing" (no fall anim)
        EXPECT((F<uint32_t>(player, 0x1C) & 1u) == 1u);      // local player untouched
        // The game re-enables collision e.g. after leaving a car: the plugin must turn it off again.
        F<uint32_t>(avatar, 0x1C) |= 1u;
        uint32_t tc = nowMs();
        while (nowMs() - tc < 200) { if (nowMs() - lastSend >= 33) { lastSend = nowMs(); sendFriend(false, 1.5f); } frame(); }
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
    t0 = nowMs();
    while (nowMs() - t0 < 1500) { if (nowMs() - lastSend >= 50) { lastSend = nowMs(); sendFriend(true, 1.5f + (nowMs() - t0) / 1000.f); } frame(); }
    EXPECT(g_opCount[0x00A5] == 1);  // CREATE_CAR
    EXPECT(g_opCount[0x036A] >= 1);  // WARP_CHAR_INTO_CAR
    EXPECT(g_opCount[0x0229] == 1);  // CHANGE_CAR_COLOUR
    void* car = nullptr;
    for (auto& kv : g_entities) if (kv.second.alive && kv.second.isVehicle) car = kv.second.mem;
    EXPECT(car != nullptr);
    if (car && avatar) {
        EXPECT(F<int16_t>(car, 0x22) == 560);
        EXPECT(F<void*>(car, 0x460) == avatar);
        uint8_t* m = F<uint8_t*>(car, 0x14);
        std::printf("  carro x=%.2f fwd=(%.2f, %.2f)\n", F<float>(m, 0x30), F<float>(m, 0x10), F<float>(m, 0x14));
        EXPECT(std::fabs(F<float>(m, 0x10) - 1.f) < 0.01f); // forward axis = +X as sent
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
                                    0x71A700, 0xB7CB48, 0xB7CD98, 0xBA67A4, 0xC17044};
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
