// On-screen text through the game's CFont (original code for LBN Coop SA).
#include "overlay.h"
#include "game.h"

namespace overlay {

namespace {
using FF = void(__cdecl*)(float);
using FFF = void(__cdecl*)(float, float);
using FU = void(__cdecl*)(uint32_t);  // CRGBA passed by value (4 bytes)
using FS = void(__cdecl*)(int16_t);
using FB = void(__cdecl*)(bool);
using FBB = void(__cdecl*)(bool, bool);
using FI = void(__cdecl*)(int);
using FPrint = void(__cdecl*)(float, float, const char*);

template <class F> F fn(uintptr_t a) { return reinterpret_cast<F>(a); }

float baseScaleX() { return sa::screenWidth() / 640.f; }
float baseScaleY() { return sa::screenHeight() / 448.f; }
}

float lineHeight(float scale) { return 18.f * scale * baseScaleY(); }

void text(float x, float y, const char* s, uint32_t color, float scale, Align align, bool background) {
    if (!s || !*s) return;
    float sx = 0.35f * scale * baseScaleX();
    float sy = 0.85f * scale * baseScaleY();
    fn<FBB>(sa::fnFontSetBackground)(background, false);
    if (background) fn<FU>(sa::fnFontSetBackgroundColor)(rgba(0, 0, 0, 150));
    fn<FB>(sa::fnFontSetProportional)(true);
    fn<FB>(sa::fnFontSetJustify)(false);
    fn<FI>(sa::fnFontSetOrientation)(int(align));
    fn<FS>(sa::fnFontSetFontStyle)(1); // FONT_SUBTITLES
    fn<FFF>(sa::fnFontSetScale)(sx, sy);
    fn<FF>(sa::fnFontSetSlant)(0.f);
    fn<FF>(sa::fnFontSetAlphaFade)(255.f);
    fn<FS>(sa::fnFontSetDropShadowPosition)(1);
    fn<FS>(sa::fnFontSetEdge)(0);
    fn<FU>(sa::fnFontSetDropColor)(rgba(0, 0, 0, uint8_t(color >> 24)));
    fn<FU>(sa::fnFontSetColor)(color);
    float w = float(sa::screenWidth());
    if (align == Center) fn<FF>(sa::fnFontSetCentreSize)(w);
    else if (align == Right) fn<FF>(sa::fnFontSetRightJustifyWrap)(0.f);
    else fn<FF>(sa::fnFontSetWrapx)(w);
    fn<FPrint>(sa::fnFontPrintString)(x, y, s);
}

} // namespace overlay
