// GTA San Andreas 1.0 US (incl. the 14,383,616-byte "Hoodlum" exe) memory layout used by LBN Coop SA.
// Addresses/offsets were cross-checked against plugin-sdk (DK22Pac et al., zlib license) headers;
// no plugin-sdk code is compiled into this project.
#pragma once
#include <cstdint>
#include "../common/mathutil.h"

namespace sa {

// ---------------- Globals ----------------
constexpr uintptr_t kPlayers        = 0xB7CD98; // CPlayerInfo CWorld::Players[]; +0x0 = CPed* m_pPed
constexpr uintptr_t kTimerMs        = 0xB7CB84; // CTimer::m_snTimeInMilliseconds
constexpr uintptr_t kCodePause      = 0xB7CB48; // CTimer::m_CodePause
constexpr uintptr_t kUserPause      = 0xB7CB49; // CTimer::m_UserPause
constexpr uintptr_t kMenuActive     = 0xBA6748 + 0x5C; // FrontEndMenuManager.m_bMenuActive
constexpr uintptr_t kScreenWidth    = 0xC17044; // RsGlobal.maximumWidth
constexpr uintptr_t kScreenHeight   = 0xC17048; // RsGlobal.maximumHeight
constexpr uintptr_t kVehiclePool    = 0xB74494; // CPool<CVehicle,CHeli>* CPools::ms_pVehiclePool
constexpr uint32_t  kVehicleSlot    = 0xA18;    // sizeof(CHeli) = pool slot stride
constexpr uintptr_t kWeatherNew     = 0xC8131C; // int16 CWeather::NewWeatherType

// ---------------- Functions ----------------
constexpr uintptr_t fnCalcScreenCoors   = 0x70CE30; // CSprite::CalcScreenCoors (cdecl)
constexpr uintptr_t fnPoolsGetPed       = 0x54FF90; // CPools::GetPed(int) (cdecl)
constexpr uintptr_t fnPoolsGetPedRef    = 0x54FF60; // CPools::GetPedRef(CPed*)
constexpr uintptr_t fnPoolsGetVehicle   = 0x54FFF0; // CPools::GetVehicle(int)
constexpr uintptr_t fnPoolsGetVehicleRef= 0x54FFC0; // CPools::GetVehicleRef(CVehicle*)
constexpr uintptr_t fnEntityUpdateRwMatrix = 0x446F90; // CEntity::UpdateRwMatrix (thiscall)
constexpr uintptr_t fnEntityUpdateRwFrame  = 0x532B00; // CEntity::UpdateRwFrame (thiscall)
constexpr uintptr_t fnPhysicalRemoveAndAdd = 0x542560; // CPhysical::RemoveAndAdd (thiscall)
constexpr uintptr_t fnPlaceableSetHeading  = 0x43E0C0; // CPlaceable::SetHeading(float) (thiscall)
constexpr uintptr_t fnScriptInit           = 0x4648E0; // CRunningScript::Init (thiscall)
constexpr uintptr_t fnScriptProcessOne     = 0x469EB0; // CRunningScript::ProcessOneCommand (thiscall)

// CFont (all cdecl)
constexpr uintptr_t fnFontSetScale        = 0x719380;
constexpr uintptr_t fnFontSetSlant        = 0x719420;
constexpr uintptr_t fnFontSetColor        = 0x719430;
constexpr uintptr_t fnFontSetFontStyle    = 0x719490;
constexpr uintptr_t fnFontSetWrapx        = 0x7194D0;
constexpr uintptr_t fnFontSetCentreSize   = 0x7194E0;
constexpr uintptr_t fnFontSetRightJustifyWrap = 0x7194F0;
constexpr uintptr_t fnFontSetAlphaFade    = 0x719500;
constexpr uintptr_t fnFontSetDropColor    = 0x719510;
constexpr uintptr_t fnFontSetDropShadowPosition = 0x719570;
constexpr uintptr_t fnFontSetEdge         = 0x719590;
constexpr uintptr_t fnFontSetProportional = 0x7195B0;
constexpr uintptr_t fnFontSetBackground   = 0x7195C0;
constexpr uintptr_t fnFontSetBackgroundColor = 0x7195E0;
constexpr uintptr_t fnFontSetJustify      = 0x719600;
constexpr uintptr_t fnFontSetOrientation  = 0x719610;
constexpr uintptr_t fnFontPrintString     = 0x71A700;

// ---------------- Hook sites (all are E8 CALL instructions) ----------------
constexpr uintptr_t hookGameProcess = 0x53E981; // after CGame::Process  (plugin-sdk gameProcessEvent)
constexpr uintptr_t hookDrawing     = 0x53E293; // 2D drawing           (plugin-sdk drawingEvent)
constexpr uintptr_t hookShutdownRw  = 0x53D910; // before RW shutdown    (plugin-sdk shutdownRwEvent)
constexpr uintptr_t hookInitScripts[] = {0x53BDD7, 0x5BA340, 0x5D4FD7}; // CTheScripts::Init call sites

// ---------------- Structure offsets ----------------
namespace off {
constexpr uint32_t placementPos   = 0x04;  // CPlaceable::m_placement.m_vPosn (when no matrix)
constexpr uint32_t matrix         = 0x14;  // CPlaceable::m_matrix (CMatrix*)
constexpr uint32_t modelIndex     = 0x22;  // CEntity::m_nModelIndex (int16)
constexpr uint32_t moveSpeed      = 0x44;  // CPhysical::m_vecMoveSpeed (units per 1/50 s)
constexpr uint32_t turnSpeed      = 0x50;  // CPhysical::m_vecTurnSpeed
constexpr uint32_t entityFlags    = 0x1C;  // CEntity bitfield; bit 0 = bUsesCollision
constexpr uint32_t physicalFlags  = 0x40;  // CPhysical bitfield; bit 1 = bApplyGravity
constexpr uint32_t pedFlags       = 0x46C; // CPed bitfield block; bit 0 = bIsStanding, bit 8 = bInVehicle, bit 9 = bIsInTheAir
constexpr uint32_t pedState       = 0x530; // CPed::m_ePedState (54 = DIE, 55 = DEAD)
constexpr uint32_t pedMoveState   = 0x534; // CPed::m_nMoveState
constexpr uint32_t pedWeapons     = 0x5A0; // CWeapon m_aWeapons[13], 0x1C bytes each, +0 = eWeaponType
constexpr uint32_t weaponSize     = 0x1C;
constexpr uint32_t pedWeaponSlot  = 0x718; // uint8 m_nSelectedWepSlot
constexpr uint32_t pedLastDamWeapon = 0x760; // m_nLastWeaponDamage
constexpr uint32_t pedLastDamEntity = 0x764; // CEntity* m_pLastEntityDamage
constexpr uint32_t pedHealth      = 0x540; // float
constexpr uint32_t pedArmour      = 0x548; // float
constexpr uint32_t pedHeadingCur  = 0x558; // float radians
constexpr uint32_t pedHeadingGoal = 0x55C;
constexpr uint32_t pedVehicle     = 0x58C; // CVehicle*
constexpr uint32_t vehColor1      = 0x434;
constexpr uint32_t vehColor2      = 0x435;
constexpr uint32_t vehDriver      = 0x460; // CPed*
constexpr uint32_t vehPassengers  = 0x464; // CPed*[8]
constexpr uint32_t vehMaxPassengers = 0x488; // uint8
constexpr uint32_t vehHealth      = 0x4C0; // float
constexpr uint32_t vehCreatedBy   = 0x4A4; // uint8 eVehicleCreatedBy: 1 random, 2 mission, 3 parked, 4 permanent
// CMatrix layout
constexpr uint32_t mRight = 0x00, mFwd = 0x10, mUp = 0x20, mPos = 0x30;
} // namespace off

// ---------------- Helpers ----------------
template <class T> inline T& at(void* base, uint32_t o) { return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(base) + o); }
template <class T> inline T& global(uintptr_t a) { return *reinterpret_cast<T*>(a); }

const char* detectGameVersion(); // nullptr if unsupported

void* findPlayerPed();
bool gamePaused();
int screenWidth();
int screenHeight();
uint32_t gameTimeMs();

void* pedFromHandle(int h);
void* vehicleFromHandle(int h);
int vehicleRef(void* veh);

bool entityPos(void* ent, lbn::Vec3& out);
bool pedInVehicle(void* ped);
void* pedVehicle(void* ped);   // nullptr if on foot
int pedSeat(void* ped, void* veh); // 0 = driver, 1.. passenger index + 1, -1 = not in it
int16_t modelOf(void* ent);
int pedCurrentWeapon(void* ped);  // eWeaponType in the selected slot (0 = fists)
bool pedIsDead(void* ped);
// Calls fn for every vehicle in the game's vehicle pool.
void forEachVehicle(void (*fn)(void* veh, void* ctx), void* ctx);

// Writes position (and optionally rotation axes) and relinks the entity in the world sectors.
void setPhysicalTransform(void* ent, const lbn::Vec3& pos, const lbn::Vec3* right, const lbn::Vec3* fwd,
                          const lbn::Vec3* up, const lbn::Vec3& velMps);
void setPedHeading(void* ped, float heading);

bool worldToScreen(const lbn::Vec3& world, float& sx, float& sy, float& depth);

} // namespace sa
