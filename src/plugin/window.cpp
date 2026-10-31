// Window mode, two copies on one PC and the mouse of a copy in the background (original code for LBN Coop SA).
#include "window.h"
#include "../common/log.h"
#include "game.h"
#include "hooks.h"
#include <windows.h>

namespace window {

namespace {
struct RwVideoMode { int32_t width, height, depth, flags, refRate, format; };
constexpr int32_t kVideoModeExclusive = 0x1; // rwVIDEOMODEEXCLUSIVE
using GetNumModesFn = int(__cdecl*)();
using GetModeInfoFn = RwVideoMode*(__cdecl*)(RwVideoMode*, int);
using SetModeFn = int(__cdecl*)(int);
using SetCursorPosFn = BOOL(WINAPI*)(int, int);
using ClipCursorFn = BOOL(WINAPI*)(const RECT*);
using SetWindowLongFn = LONG(WINAPI*)(HWND, int, LONG);
constexpr DWORD kWindowStyle = WS_OVERLAPPEDWINDOW | WS_VISIBLE;

uintptr_t g_origGetModeInfo = 0, g_origSetMode = 0;
SetCursorPosFn g_origSetCursorPos = nullptr;
ClipCursorFn g_origClipCursor = nullptr;
SetWindowLongFn g_origSetWindowLong = nullptr;
bool g_secondCopy = false, g_windowedActive = false;
int g_width = 1280, g_height = 720;

bool inFront() {
    HWND w = static_cast<HWND>(gameWindow());
    return !w || GetForegroundWindow() == w;
}
// A copy running behind another window must not move or confine the mouse of the window in front.
BOOL WINAPI hkSetCursorPos(int x, int y) { return inFront() ? g_origSetCursorPos(x, y) : TRUE; }
BOOL WINAPI hkClipCursor(const RECT* r) { return (!r || inFront()) ? g_origClipCursor(r) : TRUE; }
// After RenderWare starts, WinMain makes the window borderless (WS_POPUP) when it thinks it is fullscreen or
// the PC has several video adapters: keep the bordered style while the windowed mode is on.
LONG WINAPI hkSetWindowLong(HWND h, int idx, LONG v) {
    if (g_windowedActive && idx == GWL_STYLE && h == static_cast<HWND>(gameWindow())) v = LONG(kWindowStyle);
    return g_origSetWindowLong(h, idx, v);
}

int windowedModeIndex() {
    int n = reinterpret_cast<GetNumModesFn>(sa::fnRwGetNumVideoModes)();
    for (int i = 0; i < n; ++i) {
        RwVideoMode vm{};
        reinterpret_cast<GetModeInfoFn>(g_origGetModeInfo)(&vm, i);
        if (!(vm.flags & kVideoModeExclusive)) return i;
    }
    return -1;
}

// psSelectDevice reads the chosen mode's info right before setting it: switching to the windowed mode here
// makes the rest of psSelectDevice take its own windowed path (no refresh rate, no fullscreen flag).
RwVideoMode* __cdecl hkGetModeInfo(RwVideoMode* out, int mode) {
    int w = windowedModeIndex();
    if (w < 0) LOGW("Janela: o jogo nao ofereceu modo em janela; ficando em tela cheia");
    else if (w != mode) {
        LOGI("Janela: modo de video %d trocado pelo modo em janela %d", mode, w);
        mode = w;
        sa::global<int32_t>(sa::kGcurSelVM) = w;
    }
    return reinterpret_cast<GetModeInfoFn>(g_origGetModeInfo)(out, mode);
}

void resizeWindow() {
    HWND h = static_cast<HWND>(gameWindow());
    if (!h) return;
    DWORD style = kWindowStyle;
    RECT r{0, 0, g_width, g_height};
    AdjustWindowRect(&r, style, FALSE);
    int w = r.right - r.left, ht = r.bottom - r.top;
    // Two copies side by side: the first one on the left, the second one on the right of the main screen.
    int x = g_secondCopy ? GetSystemMetrics(SM_CXSCREEN) - w : 0;
    if (x < 0) x = 0;
    SetWindowLongA(h, GWL_STYLE, LONG(style));
    SetWindowPos(h, nullptr, x, 0, w, ht, SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    LOGI("Janela: %dx%d em x=%d", g_width, g_height, x);
}

int __cdecl hkSetMode(int mode) {
    int ok = reinterpret_cast<SetModeFn>(g_origSetMode)(mode);
    if (!ok) return ok;
    g_windowedActive = true;
    sa::global<uint8_t>(sa::kWindowed) = 1;
    sa::global<int32_t>(sa::kPsFullScreen) = 0;
    // The windowed path of psSelectDevice leaves the screen size alone; the camera is created from it next.
    sa::global<int>(sa::kScreenWidth) = g_width;
    sa::global<int>(sa::kScreenHeight) = g_height;
    resizeWindow();
    return ok;
}

bool hookVideo() {
    if (!hooks::redirectCall(sa::hookGetVideoModeInfo, reinterpret_cast<void*>(&hkGetModeInfo), &g_origGetModeInfo))
        return false;
    // Only patch a psSelectDevice that calls exactly the RenderWare functions we expect.
    if (g_origGetModeInfo != sa::fnRwGetVideoModeInfo ||
        !hooks::redirectCall(sa::hookSetVideoMode, reinterpret_cast<void*>(&hkSetMode), &g_origSetMode) ||
        g_origSetMode != sa::fnRwSetVideoMode) {
        hooks::restoreCall(sa::hookGetVideoModeInfo, g_origGetModeInfo);
        hooks::restoreCall(sa::hookSetVideoMode, g_origSetMode);
        return false;
    }
    return true;
}
} // namespace

void* gameWindow() { return sa::global<void*>(sa::kPsGlobal); }

void install(const lbn::CoopConfig& cfg, bool secondCopy) {
    g_secondCopy = secondCopy;
    g_width = cfg.windowWidth;
    g_height = cfg.windowHeight;
    if (cfg.twoCopies) {
        // IsAlreadyRunning() starts with "mov eax,[0x8D6224]"; make it "xor eax,eax; ret" (never running).
        const uint8_t expect[] = {0xA1, 0x24, 0x62, 0x8D, 0x00};
        const uint8_t patch[] = {0x33, 0xC0, 0xC3};
        if (hooks::patchBytes(sa::fnIsAlreadyRunning, expect, sizeof expect, patch, sizeof patch))
            LOGI("Duas copias: o jogo pode abrir de novo neste PC");
        else
            LOGW("Duas copias: bytes inesperados em IsAlreadyRunning; segunda copia NAO liberada");
    }
    if (cfg.windowed) {
        if (hookVideo()) LOGI("Janela: modo janela pedido (%dx%d)", g_width, g_height);
        else LOGW("Janela: ganchos de video nao instalados (bytes inesperados); jogo em tela cheia");
        if (!hooks::patchImport(GetModuleHandleA(nullptr), "user32.dll", "SetWindowLongA",
                                reinterpret_cast<void*>(&hkSetWindowLong), reinterpret_cast<void**>(&g_origSetWindowLong)))
            LOGW("Janela: nao consegui manter a borda (SetWindowLongA)");
    }
    if (cfg.windowed || cfg.backgroundRun) {
        void* exe = GetModuleHandleA(nullptr);
        bool c = hooks::patchImport(exe, "user32.dll", "SetCursorPos", reinterpret_cast<void*>(&hkSetCursorPos),
                                    reinterpret_cast<void**>(&g_origSetCursorPos));
        bool d = hooks::patchImport(exe, "user32.dll", "ClipCursor", reinterpret_cast<void*>(&hkClipCursor),
                                    reinterpret_cast<void**>(&g_origClipCursor));
        if (!c || !d) LOGW("Segundo plano: mouse nao protegido (SetCursorPos=%d ClipCursor=%d)", int(c), int(d));
    }
}

} // namespace window
