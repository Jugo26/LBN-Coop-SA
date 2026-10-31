// Thread-safe file + console logger (original code for LBN Coop SA).
#pragma once
#include <string>

namespace lbn {

enum class LogLevel { Info, Warn, Error, Debug };

// Opens (truncates) the log file. Safe to call once; subsequent calls reopen.
bool logOpen(const std::string& path, bool alsoConsole);
void logClose();
void logWrite(LogLevel lvl, const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

#define LOGI(...) ::lbn::logWrite(::lbn::LogLevel::Info, __VA_ARGS__)
#define LOGW(...) ::lbn::logWrite(::lbn::LogLevel::Warn, __VA_ARGS__)
#define LOGE(...) ::lbn::logWrite(::lbn::LogLevel::Error, __VA_ARGS__)
#define LOGD(...) ::lbn::logWrite(::lbn::LogLevel::Debug, __VA_ARGS__)

} // namespace lbn
