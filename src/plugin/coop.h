// LBN Coop SA in-game controller (original code).
#pragma once
#include <windows.h>

namespace coop {
bool init(HMODULE self); // called from DllMain(PROCESS_ATTACH): config, log, hooks
}
