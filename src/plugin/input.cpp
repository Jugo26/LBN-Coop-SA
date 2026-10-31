// Keyboard handling by subclassing the game window (original code for LBN Coop SA).
#include "input.h"
#include "../common/config.h"
#include "../common/protocol.h"
#include "game.h"
#include <windows.h>

namespace input {

namespace {
HWND g_hwnd = nullptr;
WNDPROC g_orig = nullptr;
Keys g_keys{};
bool g_chatAllowed = false;
bool g_background = false;
bool g_chatOpen = false;
bool g_swallowNextChar = false;
std::string g_buffer;
std::string g_submitted;
std::vector<Action> g_actions;

void utf8Append(std::string& out, wchar_t wc) {
    unsigned cp = unsigned(wc);
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
    else { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
}

void popUtf8(std::string& s) {
    while (!s.empty()) {
        unsigned char c = (unsigned char)s.back();
        s.pop_back();
        if ((c & 0xC0) != 0x80) break; // removed the lead byte
    }
}

void handleChatKey(WPARAM vk, LPARAM lp) {
    if (vk == VK_RETURN) {
        g_submitted = g_buffer;
        g_buffer.clear();
        g_chatOpen = false;
        g_actions.push_back(Action::ChatSubmit);
        return;
    }
    if (vk == VK_ESCAPE) {
        g_buffer.clear();
        g_chatOpen = false;
        g_actions.push_back(Action::ChatCancel);
        return;
    }
    if (vk == VK_BACK) { popUtf8(g_buffer); return; }
    BYTE ks[256];
    if (!GetKeyboardState(ks)) return;
    wchar_t out[4] = {};
    UINT scan = (UINT(lp) >> 16) & 0xFF;
    // Flag 0x4: do not change the keyboard (dead-key) state (Windows 10 1607+).
    int n = ToUnicode(UINT(vk), scan, ks, out, 4, 0x4);
    for (int k = 0; k < n; ++k) {
        if (out[k] >= 32 && g_buffer.size() < lbn::kMaxChatLen * 2) utf8Append(g_buffer, out[k]);
    }
}

LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (g_chatOpen) { handleChatKey(wp, lp); return 0; }
        if (int(wp) == g_keys.menu) { g_actions.push_back(Action::ToggleMenu); return 0; }
        if (int(wp) == g_keys.start) { g_actions.push_back(Action::Start); return 0; }
        if (int(wp) == g_keys.toggleMode) { g_actions.push_back(Action::ToggleMode); return 0; }
        if (int(wp) == g_keys.disconnect) { g_actions.push_back(Action::Disconnect); return 0; }
        if (int(wp) == g_keys.unstuck && !(lp & (1 << 30))) { g_actions.push_back(Action::Unstuck); return 0; }
        if (int(wp) == g_keys.chat && g_chatAllowed && !(lp & (1 << 30))) {
            g_chatOpen = true;
            g_buffer.clear();
            g_swallowNextChar = true;
            g_actions.push_back(Action::ChatOpened);
            return 0;
        }
        break;
    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_DEADCHAR:
        if (g_chatOpen || g_swallowNextChar) { g_swallowNextChar = false; return 0; }
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        // Always let key-ups through so the game never sees a key stuck down.
        break;
    case WM_KILLFOCUS:
        if (g_chatOpen) { g_chatOpen = false; g_buffer.clear(); g_actions.push_back(Action::ChatCancel); }
        // The game would clear ForegroundApp, and its main loop only sleeps while that is false.
        if (g_background) return 0;
        break;
    case WM_ACTIVATE:
        // The game would pause its sounds and clear ForegroundApp.
        if (g_background && LOWORD(wp) == WA_INACTIVE) return 0;
        break;
    case WM_SETFOCUS:
        // The game would open the pause menu when the window comes back to the front.
        if (g_background) { sa::global<uint8_t>(sa::kForegroundApp) = 1; return 0; }
        break;
    default: break;
    }
    return CallWindowProcA(g_orig, h, msg, wp, lp);
}

BOOL CALLBACK findWnd(HWND h, LPARAM lp) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h) && GetWindow(h, GW_OWNER) == nullptr) {
        *reinterpret_cast<HWND*>(lp) = h;
        return FALSE;
    }
    return TRUE;
}
} // namespace

bool install(const Keys& keys) {
    if (g_hwnd) return true;
    g_keys = keys;
    HWND h = FindWindowA("Grand theft auto San Andreas", nullptr);
    DWORD pid = 0;
    if (h) GetWindowThreadProcessId(h, &pid);
    if (!h || pid != GetCurrentProcessId()) { h = nullptr; EnumWindows(findWnd, reinterpret_cast<LPARAM>(&h)); }
    if (!h) return false;
    // The window must belong to this (main) thread, otherwise our proc would run elsewhere.
    if (GetWindowThreadProcessId(h, nullptr) != GetCurrentThreadId()) return false;
    g_orig = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(wndProc)));
    if (!g_orig) return false;
    g_hwnd = h;
    return true;
}

void uninstall() {
    if (g_hwnd && g_orig) SetWindowLongPtrA(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_orig));
    g_hwnd = nullptr;
    g_orig = nullptr;
}

bool installed() { return g_hwnd != nullptr; }

void setChatAllowed(bool allowed) {
    g_chatAllowed = allowed;
    if (!allowed && g_chatOpen) { g_chatOpen = false; g_buffer.clear(); g_actions.push_back(Action::ChatCancel); }
}
void setBackgroundRun(bool on) { g_background = on; }
bool chatOpen() { return g_chatOpen; }
const std::string& chatBuffer() { return g_buffer; }
void closeChat() { g_chatOpen = false; g_buffer.clear(); }

std::vector<Action> drainActions() {
    std::vector<Action> out;
    out.swap(g_actions);
    return out;
}

void injectForTest(Action a, const std::string& text) {
    if (a == Action::ChatSubmit) g_submitted = text;
    g_actions.push_back(a);
}

std::string takeSubmitted() {
    std::string s;
    s.swap(g_submitted);
    return s;
}

} // namespace input
