// LBN Coop SA launcher/updater - platform independent helpers (unit tested on Linux).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace lbn::upd {

struct SemVer {
    int major = 0, minor = 0, patch = 0;
    bool valid = false;
};
// Accepts "0.1.0", "v0.1.0", "0.1.0-etapa1", " 0.1.0\r\n" (suffix after '-'/'+' ignored).
SemVer parseVersion(const std::string& s);
// <0 if a<b, 0 equal, >0 if a>b. Invalid versions compare lower than any valid one.
int compareVersion(const SemVer& a, const SemVer& b);
std::string versionString(const SemVer& v);

std::string trim(const std::string& s);

// SHA-256 (FIPS 180-4).
class Sha256 {
public:
    Sha256();
    void update(const void* data, size_t len);
    void final(uint8_t out[32]);
private:
    void block(const uint8_t* p);
    uint32_t h_[8];
    uint8_t buf_[64];
    uint64_t len_ = 0;
    size_t used_ = 0;
};
std::string sha256Hex(const void* data, size_t len);
// Extracts the first 64-hex-digit token of a ".sha256" file ("<hash>  name" or just "<hash>"), lowercased.
std::string parseSha256File(const std::string& text);

// Minimal JSON helpers for the GitHub "latest release" document.
struct ReleaseAsset { std::string name, url; uint64_t size = 0; };
struct ReleaseInfo {
    std::string tag;
    std::string htmlUrl;
    std::vector<ReleaseAsset> assets;
};
// Returns false if the document has no tag_name.
bool parseLatestRelease(const std::string& json, ReleaseInfo& out);
// Finds the mod zip ("LbnCoopSA-v*.zip") and its "<zip>.sha256" asset. Returns false if either is missing.
bool pickAssets(const ReleaseInfo& rel, ReleaseAsset& zip, ReleaseAsset& sha);

// Zip entry path safety: relative, forward or back slashes, no "..", no drive letters, no empty parts.
// Returns the normalized path with '/' separators, or "" if unsafe.
std::string safeRelativePath(const std::string& entryName);

enum class FileAction {
    Replace,          // overwrite (after backup) / create
    ExampleIni,       // user's .ini exists: write the new one as "<name>-exemplo.ini" instead
    IfOursOnly,       // dinput8.dll: only replace if the installed one is the one we shipped
    Skip,             // not a mod file: never touched
};
// Decides what the updater may do with one zip entry (normalized path). Only known mod files are allowed.
FileAction classifyEntry(const std::string& normPath);
// "LbnCoopSA.ini" -> "LbnCoopSA-exemplo.ini", "extras/LbnCoopSA-amigo.ini" -> "extras/LbnCoopSA-amigo-exemplo.ini"
std::string exampleIniName(const std::string& normPath);

// Parses a SHA256SUMS.txt ("<hash>  <path>" lines) and tells whether 'hash' is listed for 'path'.
bool sumsListHash(const std::string& sumsText, const std::string& path, const std::string& hashHex);

} // namespace lbn::upd
