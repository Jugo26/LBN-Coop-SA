// Minimal call-site hooking (original code for LBN Coop SA).
#pragma once
#include <cstdint>

namespace hooks {
// Redirects the E8 CALL at `site` to `fn`; stores the original target in *original.
// Returns false (and patches nothing) if the bytes at `site` are not an E8 CALL.
bool redirectCall(uintptr_t site, void* fn, uintptr_t* original);
// Restores a previously redirected call.
void restoreCall(uintptr_t site, uintptr_t original);
}
