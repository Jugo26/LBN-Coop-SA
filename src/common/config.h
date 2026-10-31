// INI configuration (original code for LBN Coop SA).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include "version.h"

namespace lbn {

// Minimal INI reader: [Section], key=value, ';' or '#' comments. Keys are case-insensitive.
class IniFile {
public:
    bool loadFromFile(const std::string& path);
    void loadFromString(const std::string& text);
    bool has(const std::string& section, const std::string& key) const;
    std::string get(const std::string& section, const std::string& key, const std::string& def = "") const;
    int getInt(const std::string& section, const std::string& key, int def) const;
    bool getBool(const std::string& section, const std::string& key, bool def) const;
private:
    std::map<std::string, std::string> values_; // "section.key" lowercased -> value
};

enum class Mode { Host, Join };

struct CoopConfig {
    std::string nickname = "Jogador";
    Mode mode = Mode::Join;
    std::string hostIp = "127.0.0.1";
    uint16_t port = LBN_DEFAULT_PORT;
    uint16_t skin = 106;           // fam2 (Grove); 0 (CJ) not supported for remote peds yet
    bool autoStart = false;
    int sendRateHz = 20;
    int interpDelayMs = 100;
    int timeoutMs = 10000;
    bool showNametags = true;
    float nametagDistance = 80.f;
    std::string serverName = "LBN Coop SA";
    // Hotkeys (Windows virtual-key codes).
    int keyMenu = 0x77;            // F8
    int keyStart = 0x76;           // F7
    int keyToggleMode = 0x78;      // F9
    int keyDisconnect = 0x79;      // F10
    int keyChat = 'T';
};

// Fills cfg from an ini file; returns a list of warnings (pt-BR) through `warnings`.
CoopConfig configFromIni(const IniFile& ini, std::string* warnings = nullptr);
const char* modeName(Mode m);

// Default ini text written when no file exists.
std::string defaultIniText();

// Sanitize a nickname: printable ASCII, no spaces at the ends, 1..24 chars.
std::string sanitizeNick(const std::string& in);
// Sanitize a chat line: drop control chars, fold accents, trim, max 120.
std::string sanitizeChat(const std::string& in);
// Fold common UTF-8 Latin-1 letters (á, ç, ã...) to ASCII for the GTA font.
std::string foldToAscii(const std::string& utf8);
// Convert a Windows-1252/Latin-1 byte string (as typed via WM_CHAR in ANSI) to UTF-8.
std::string latin1ToUtf8(const std::string& in);

} // namespace lbn
