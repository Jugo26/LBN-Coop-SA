// LBN Coop SA - ASI entry point (original code).
#include <windows.h>
#include "coop.h"

extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        // Only memory patching + file reads happen here; no threads are started under the loader lock.
        coop::init(inst);
    }
    return TRUE;
}
