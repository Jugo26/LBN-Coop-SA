// LBN Coop SA - ASI entry point (original code).
#include <windows.h>
#include "coop.h"
#include "input.h"
#include <string>

// Used only by tests/fake_game_harness.cpp (no window there). Does nothing unless LBN_COOP_TEST is set.
extern "C" __declspec(dllexport) void __cdecl LbnCoopSA_TestInject(int action, const char* text) {
    if (!GetEnvironmentVariableA("LBN_COOP_TEST", nullptr, 0)) return;
    input::injectForTest(static_cast<input::Action>(action), text ? std::string(text) : std::string());
}

extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        // Guard against being loaded twice (e.g. a copy in the game folder AND in scripts\ with
        // two different ASI loaders): only the first copy may install hooks.
        char name[64];
        wsprintfA(name, "Local\\LbnCoopSA_%lu", GetCurrentProcessId());
        HANDLE mtx = CreateMutexA(nullptr, FALSE, name);
        if (mtx && GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(mtx);
            return TRUE; // stay loaded but inactive
        }
        // Only memory patching + file reads happen here; no threads are started under the loader lock.
        coop::init(inst);
    }
    return TRUE;
}
