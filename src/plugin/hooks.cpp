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

bool patchBytes(uintptr_t addr, const void* expect, size_t expectLen, const void* bytes, size_t len) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) return false;
    if (std::memcmp(reinterpret_cast<void*>(addr), expect, expectLen) != 0) return false;
    DWORD old;
    if (!VirtualProtect(reinterpret_cast<void*>(addr), len, PAGE_EXECUTE_READWRITE, &old)) return false;
    std::memcpy(reinterpret_cast<void*>(addr), bytes, len);
    VirtualProtect(reinterpret_cast<void*>(addr), len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(addr), len);
    return true;
}

bool patchImport(void* mod, const char* dll, const char* fn, void* newFn, void** orig) {
    HMODULE lib = GetModuleHandleA(dll);
    FARPROC real = lib ? GetProcAddress(lib, fn) : nullptr;
    auto* base = static_cast<uint8_t*>(mod);
    if (!real || !base) return false;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return false;
    // Match the IAT slot by its resolved address: works with or without the name table.
    for (auto* d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); d->Name; ++d) {
        if (lstrcmpiA(reinterpret_cast<const char*>(base + d->Name), dll) != 0) continue;
        for (auto* t = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + d->FirstThunk); t->u1.Function; ++t) {
            if (t->u1.Function != DWORD(uintptr_t(real))) continue;
            DWORD old;
            if (!VirtualProtect(&t->u1.Function, sizeof(DWORD), PAGE_READWRITE, &old)) return false;
            if (orig) *orig = reinterpret_cast<void*>(real);
            t->u1.Function = DWORD(uintptr_t(newFn));
            VirtualProtect(&t->u1.Function, sizeof(DWORD), old, &old);
            return true;
        }
    }
    return false;
}

} // namespace hooks
