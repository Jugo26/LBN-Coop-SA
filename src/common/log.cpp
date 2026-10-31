// Thread-safe logger implementation (original code for LBN Coop SA).
#include "log.h"
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace lbn {

namespace {
std::mutex g_mu;
FILE* g_file = nullptr;
bool g_console = false;
}

bool logOpen(const std::string& path, bool alsoConsole) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_file) { std::fclose(g_file); g_file = nullptr; }
    g_file = std::fopen(path.c_str(), "wb");
    g_console = alsoConsole;
    return g_file != nullptr;
}

void logClose() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_file) { std::fclose(g_file); g_file = nullptr; }
}

void logWrite(LogLevel lvl, const char* fmt, ...) {
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    int ms = int(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &tt);
#else
    localtime_r(&tt, &tmv);
#endif
    const char* tag = lvl == LogLevel::Info ? "INFO" : lvl == LogLevel::Warn ? "AVISO"
                    : lvl == LogLevel::Error ? "ERRO" : "DEBUG";
    char line[1200];
    std::snprintf(line, sizeof line, "[%02d:%02d:%02d.%03d] [%s] %s\n", tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ms, tag, msg);

    std::lock_guard<std::mutex> lk(g_mu);
    if (g_file) { std::fputs(line, g_file); std::fflush(g_file); }
    if (g_console) { std::fputs(line, stdout); std::fflush(stdout); }
}

} // namespace lbn
