// On-screen text through the game's CFont (original code for LBN Coop SA).
#pragma once
#include <cstdint>

namespace overlay {

enum Align { Center = 0, Left = 1, Right = 2 };

constexpr uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
}

// Text height in pixels for a given scale factor (1.0 = normal HUD-ish size).
float lineHeight(float scale);
// x, y in screen pixels. Text must already be ASCII without '~' tokens.
void text(float x, float y, const char* s, uint32_t color, float scale = 1.f, Align align = Left,
          bool background = false);

} // namespace overlay
