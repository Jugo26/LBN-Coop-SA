// Window mode, two copies on one PC and the mouse of a copy in the background (original code for LBN Coop SA).
#pragma once
#include "../common/config.h"

namespace window {
// Installs what the [Janela] ini asks for. Must run before the game's WinMain (from DllMain).
void install(const lbn::CoopConfig& cfg, bool secondCopy);
// The game window (PsGlobal.window), or nullptr before it exists.
void* gameWindow();
}
