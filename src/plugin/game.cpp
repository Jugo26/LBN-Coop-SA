// GTA SA access helpers (original code for LBN Coop SA).
#include "game.h"
#include <windows.h>

namespace sa {

namespace {
struct RwV3d { float x, y, z; };
using GetByHandleFn = void*(__cdecl*)(int);
using GetRefFn = int(__cdecl*)(void*);
using ThisVoidFn = void(__thiscall*)(void*);
using ThisFloatFn = void(__thiscall*)(void*, float);
using CalcScreenFn = bool(__cdecl*)(const RwV3d&, RwV3d*, float*, float*, bool, bool);
}

static bool readable(uintptr_t a, size_t n) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<void*>(a), &mbi, sizeof mbi)) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return a + n <= reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
}

const char* detectGameVersion() {
    // Same signatures plugin-sdk uses to tell the 1.0 US variants apart.
    if (readable(0x401000, 4)) {
        if (global<uint32_t>(0x401000) == 0x16197BE9u) return "1.0 US (Hoodlum)";
        if (global<uint32_t>(0x401000) == 0x53EC8B55u) return "1.0 US (compact)";
    }
    if (readable(0x82457C, 4) && global<uint32_t>(0x82457C) == 0x94BFu) return "1.0 US";
    return nullptr;
}

void* findPlayerPed() { return global<void*>(kPlayers); }

bool gamePaused() {
    return global<uint8_t>(kMenuActive) != 0 || global<uint8_t>(kUserPause) != 0 || global<uint8_t>(kCodePause) != 0;
}

int screenWidth() { int w = global<int>(kScreenWidth); return w > 0 ? w : 640; }
int screenHeight() { int h = global<int>(kScreenHeight); return h > 0 ? h : 480; }
uint32_t gameTimeMs() { return global<uint32_t>(kTimerMs); }

void* pedFromHandle(int h) { return h < 0 ? nullptr : reinterpret_cast<GetByHandleFn>(fnPoolsGetPed)(h); }
void* vehicleFromHandle(int h) { return h < 0 ? nullptr : reinterpret_cast<GetByHandleFn>(fnPoolsGetVehicle)(h); }
int vehicleRef(void* veh) { return veh ? reinterpret_cast<GetRefFn>(fnPoolsGetVehicleRef)(veh) : -1; }

bool entityPos(void* ent, lbn::Vec3& out) {
    if (!ent) return false;
    void* m = at<void*>(ent, off::matrix);
    const float* p = m ? &at<float>(m, off::mPos) : &at<float>(ent, off::placementPos);
    out = {p[0], p[1], p[2]};
    return true;
}

bool pedInVehicle(void* ped) {
    return ped && (at<uint32_t>(ped, off::pedFlags) & (1u << 8)) && at<void*>(ped, off::pedVehicle);
}
void* pedVehicle(void* ped) { return pedInVehicle(ped) ? at<void*>(ped, off::pedVehicle) : nullptr; }

int pedSeat(void* ped, void* veh) {
    if (!ped || !veh) return -1;
    if (at<void*>(veh, off::vehDriver) == ped) return 0;
    void** pass = &at<void*>(veh, off::vehPassengers);
    for (int i = 0; i < 8; ++i) if (pass[i] == ped) return i + 1;
    return -1;
}

int16_t modelOf(void* ent) { return ent ? at<int16_t>(ent, off::modelIndex) : int16_t(-1); }

void setPhysicalTransform(void* ent, const lbn::Vec3& pos, const lbn::Vec3* right, const lbn::Vec3* fwd,
                          const lbn::Vec3* up, const lbn::Vec3& velMps) {
    if (!ent) return;
    void* m = at<void*>(ent, off::matrix);
    if (m) {
        auto put = [&](uint32_t o, const lbn::Vec3& v) {
            at<float>(m, o) = v.x; at<float>(m, o + 4) = v.y; at<float>(m, o + 8) = v.z;
        };
        if (right && fwd && up) { put(off::mRight, *right); put(off::mFwd, *fwd); put(off::mUp, *up); }
        put(off::mPos, pos);
    } else {
        at<float>(ent, off::placementPos) = pos.x;
        at<float>(ent, off::placementPos + 4) = pos.y;
        at<float>(ent, off::placementPos + 8) = pos.z;
    }
    // GTA physics speeds are expressed per 1/50 s step.
    at<float>(ent, off::moveSpeed) = velMps.x / 50.f;
    at<float>(ent, off::moveSpeed + 4) = velMps.y / 50.f;
    at<float>(ent, off::moveSpeed + 8) = velMps.z / 50.f;
    if (right) {
        at<float>(ent, off::turnSpeed) = 0; at<float>(ent, off::turnSpeed + 4) = 0; at<float>(ent, off::turnSpeed + 8) = 0;
    }
    reinterpret_cast<ThisVoidFn>(fnPhysicalRemoveAndAdd)(ent);
    reinterpret_cast<ThisVoidFn>(fnEntityUpdateRwMatrix)(ent);
    reinterpret_cast<ThisVoidFn>(fnEntityUpdateRwFrame)(ent);
}

void setPedHeading(void* ped, float heading) {
    if (!ped) return;
    at<float>(ped, off::pedHeadingCur) = heading;
    at<float>(ped, off::pedHeadingGoal) = heading;
    reinterpret_cast<ThisFloatFn>(fnPlaceableSetHeading)(ped, heading);
}

bool worldToScreen(const lbn::Vec3& world, float& sx, float& sy, float& depth) {
    RwV3d in{world.x, world.y, world.z}, out{};
    float w = 0, h = 0;
    if (!reinterpret_cast<CalcScreenFn>(fnCalcScreenCoors)(in, &out, &w, &h, true, true)) return false;
    sx = out.x; sy = out.y; depth = out.z;
    return depth > 0.f;
}

} // namespace sa
