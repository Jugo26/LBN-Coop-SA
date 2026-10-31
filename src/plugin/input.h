// Keyboard handling by subclassing the game window (original code for LBN Coop SA).
#pragma once
#include <string>
#include <vector>

namespace input {

enum class Action { None, ToggleMenu, Start, ToggleMode, Disconnect, ChatOpened, ChatSubmit, ChatCancel, Unstuck };

struct Keys { int menu, start, toggleMode, disconnect, chat, unstuck; };

// Install on the main thread once the game window exists. Returns true when installed.
bool install(const Keys& keys);
void uninstall();
bool installed();

// Chat may only be opened while this returns true (set by the coop controller every frame).
void setChatAllowed(bool allowed);
// While on, losing the focus does not stop the game (set by the coop controller every frame).
void setBackgroundRun(bool on);
bool chatOpen();
const std::string& chatBuffer(); // UTF-8
void closeChat();

// Drains the actions queued by the window procedure (main thread only).
std::vector<Action> drainActions();
// Text submitted with Enter (valid right after a ChatSubmit action).
std::string takeSubmitted();

// Test harness only (enabled when the LBN_COOP_TEST environment variable is set): queue an action as if
// its key had been pressed; for ChatSubmit, `text` is the submitted chat line.
void injectForTest(Action a, const std::string& text);

} // namespace input
