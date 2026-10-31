// Minimal call-site hooking (original code for LBN Coop SA).
#pragma once
#include <cstddef>
#include <cstdint>

namespace hooks {
// Redirects the E8 CALL at `site` to `fn`; stores the original target in *original.
// Returns false (and patches nothing) if the bytes at `site` are not an E8 CALL.
bool redirectCall(uintptr_t site, void* fn, uintptr_t* original);
// Restores a previously redirected call.
void restoreCall(uintptr_t site, uintptr_t original);
// Writes `len` bytes at `addr`, only if the code there starts with `expect` (else patches nothing).
bool patchBytes(uintptr_t addr, const void* expect, size_t expectLen, const void* bytes, size_t len);
// Points module `mod`'s import of `dll!fn` at `newFn`; *orig receives the real function.
bool patchImport(void* mod, const char* dll, const char* fn, void* newFn, void** orig);
}
