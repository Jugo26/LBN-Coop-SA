// Minimal call-site hooking (original code for LBN Coop SA).
#include "hooks.h"
#include <windows.h>
#include <cstring>

namespace hooks {

static bool writeRel32(uintptr_t site, uintptr_t target) {
    DWORD old;
    if (!VirtualProtect(reinterpret_cast<void*>(site), 5, PAGE_EXECUTE_READWRITE, &old)) return false;
    int32_t rel = int32_t(target - (site + 5));
    std::memcpy(reinterpret_cast<void*>(site + 1), &rel, 4);
    VirtualProtect(reinterpret_cast<void*>(site), 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(site), 5);
    return true;
}

bool redirectCall(uintptr_t site, void* fn, uintptr_t* original) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<void*>(site), &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) return false;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(site);
    if (p[0] != 0xE8) return false;
    int32_t rel;
    std::memcpy(&rel, p + 1, 4);
    *original = site + 5 + uintptr_t(intptr_t(rel));
    return writeRel32(site, reinterpret_cast<uintptr_t>(fn));
}

void restoreCall(uintptr_t site, uintptr_t original) {
    if (original) writeRel32(site, original);
}

} // namespace hooks
