// LBN Coop SA launcher/updater - platform independent helpers.
#include "update_core.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>

namespace lbn::upd {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    // also strip a UTF-8 BOM
    if (b >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) a = 3;
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

SemVer parseVersion(const std::string& in) {
    SemVer v;
    std::string s = trim(in);
    size_t i = 0;
    if (i < s.size() && (s[i] == 'v' || s[i] == 'V')) ++i;
    int parts[3] = {0, 0, 0};
    for (int k = 0; k < 3; ++k) {
        if (i >= s.size() || !std::isdigit((unsigned char)s[i])) return v;
        long n = 0; int digits = 0;
        while (i < s.size() && std::isdigit((unsigned char)s[i])) {
            n = n * 10 + (s[i] - '0'); ++i;
            if (++digits > 6) return v;
        }
        parts[k] = int(n);
        if (k < 2) { if (i >= s.size() || s[i] != '.') return v; ++i; }
    }
    if (i < s.size() && s[i] != '-' && s[i] != '+') return v;
    v.major = parts[0]; v.minor = parts[1]; v.patch = parts[2]; v.valid = true;
    return v;
}

int compareVersion(const SemVer& a, const SemVer& b) {
    if (a.valid != b.valid) return a.valid ? 1 : -1;
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    return 0;
}

std::string versionString(const SemVer& v) {
    if (!v.valid) return "desconhecida";
    return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
}

// ---------------- SHA-256 ----------------
namespace {
const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
inline uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
}

Sha256::Sha256() {
    const uint32_t init[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::memcpy(h_, init, sizeof h_);
}

void Sha256::block(const uint8_t* p) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) w[i] = uint32_t(p[i*4]) << 24 | uint32_t(p[i*4+1]) << 16 | uint32_t(p[i*4+2]) << 8 | p[i*4+3];
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = ror(w[i-15], 7) ^ ror(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = ror(w[i-2], 17) ^ ror(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=h_[0],b=h_[1],c=h_[2],d=h_[3],e=h_[4],f=h_[5],g=h_[6],h=h_[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
        uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h_[0]+=a; h_[1]+=b; h_[2]+=c; h_[3]+=d; h_[4]+=e; h_[5]+=f; h_[6]+=g; h_[7]+=h;
}

void Sha256::update(const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    len_ += len;
    while (len) {
        size_t n = 64 - used_; if (n > len) n = len;
        std::memcpy(buf_ + used_, p, n); used_ += n; p += n; len -= n;
        if (used_ == 64) { block(buf_); used_ = 0; }
    }
}

void Sha256::final(uint8_t out[32]) {
    uint64_t bits = len_ * 8;
    uint8_t pad = 0x80; update(&pad, 1);
    uint8_t z = 0; while (used_ != 56) update(&z, 1);
    uint8_t l[8]; for (int i = 0; i < 8; ++i) l[i] = uint8_t(bits >> (56 - 8 * i));
    update(l, 8);
    for (int i = 0; i < 8; ++i) { out[i*4] = uint8_t(h_[i] >> 24); out[i*4+1] = uint8_t(h_[i] >> 16); out[i*4+2] = uint8_t(h_[i] >> 8); out[i*4+3] = uint8_t(h_[i]); }
}

std::string sha256Hex(const void* data, size_t len) {
    Sha256 s; s.update(data, len); uint8_t d[32]; s.final(d);
    static const char* hx = "0123456789abcdef";
    std::string r; for (uint8_t b : d) { r += hx[b >> 4]; r += hx[b & 15]; }
    return r;
}

std::string parseSha256File(const std::string& text) {
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && !std::isxdigit((unsigned char)text[i])) ++i;
        size_t j = i;
        while (j < text.size() && std::isxdigit((unsigned char)text[j])) ++j;
        if (j - i == 64) {
            std::string h = text.substr(i, 64);
            for (auto& c : h) c = char(std::tolower((unsigned char)c));
            return h;
        }
        i = j;
    }
    return "";
}

// ---------------- tiny JSON ----------------
namespace {
struct JVal {
    enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
    std::string s; double n = 0; bool b = false;
    std::vector<JVal> arr;
    std::vector<std::pair<std::string, JVal>> obj;
    const JVal* get(const char* k) const {
        for (auto& kv : obj) if (kv.first == k) return &kv.second;
        return nullptr;
    }
};
struct JParser {
    const std::string& s; size_t i = 0; int depth = 0;
    explicit JParser(const std::string& in) : s(in) {}
    void ws() { while (i < s.size() && (s[i]==' '||s[i]=='\t'||s[i]=='\n'||s[i]=='\r')) ++i; }
    static void putUtf8(std::string& o, uint32_t cp) {
        if (cp < 0x80) o += char(cp);
        else if (cp < 0x800) { o += char(0xC0 | (cp >> 6)); o += char(0x80 | (cp & 63)); }
        else if (cp < 0x10000) { o += char(0xE0 | (cp >> 12)); o += char(0x80 | ((cp >> 6) & 63)); o += char(0x80 | (cp & 63)); }
        else { o += char(0xF0 | (cp >> 18)); o += char(0x80 | ((cp >> 12) & 63)); o += char(0x80 | ((cp >> 6) & 63)); o += char(0x80 | (cp & 63)); }
    }
    bool hex4(uint32_t& v) {
        if (i + 4 > s.size()) return false;
        v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s[i++]; v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0'; else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10; else return false;
        }
        return true;
    }
    bool str(std::string& o) {
        if (i >= s.size() || s[i] != '"') return false;
        ++i;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') { o += c; continue; }
            if (i >= s.size()) return false;
            char e = s[i++];
            switch (e) {
            case '"': o += '"'; break; case '\\': o += '\\'; break; case '/': o += '/'; break;
            case 'b': o += '\b'; break; case 'f': o += '\f'; break; case 'n': o += '\n'; break;
            case 'r': o += '\r'; break; case 't': o += '\t'; break;
            case 'u': {
                uint32_t cp; if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= s.size() && s[i] == '\\' && s[i+1] == 'u') {
                    i += 2; uint32_t lo; if (!hex4(lo)) return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                putUtf8(o, cp); break;
            }
            default: return false;
            }
        }
        return false;
    }
    bool val(JVal& v) {
        if (++depth > 64) return false;
        ws();
        if (i >= s.size()) return false;
        char c = s[i];
        bool ok = false;
        if (c == '{') {
            v.t = JVal::Obj; ++i; ws();
            if (i < s.size() && s[i] == '}') { ++i; ok = true; }
            else for (;;) {
                ws(); std::string k; if (!str(k)) break;
                ws(); if (i >= s.size() || s[i] != ':') break; ++i;
                JVal x; if (!val(x)) break;
                v.obj.emplace_back(std::move(k), std::move(x));
                ws(); if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; ok = true; }
                break;
            }
        } else if (c == '[') {
            v.t = JVal::Arr; ++i; ws();
            if (i < s.size() && s[i] == ']') { ++i; ok = true; }
            else for (;;) {
                JVal x; if (!val(x)) break;
                v.arr.push_back(std::move(x));
                ws(); if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; ok = true; }
                break;
            }
        } else if (c == '"') {
            v.t = JVal::Str; ok = str(v.s);
        } else if (s.compare(i, 4, "true") == 0) { v.t = JVal::Bool; v.b = true; i += 4; ok = true; }
        else if (s.compare(i, 5, "false") == 0) { v.t = JVal::Bool; i += 5; ok = true; }
        else if (s.compare(i, 4, "null") == 0) { v.t = JVal::Null; i += 4; ok = true; }
        else if (c == '-' || std::isdigit((unsigned char)c)) {
            size_t j = i; while (j < s.size() && (std::isdigit((unsigned char)s[j]) || std::strchr("+-.eE", s[j]))) ++j;
            v.t = JVal::Num; v.n = std::strtod(s.substr(i, j - i).c_str(), nullptr); i = j; ok = true;
        }
        --depth;
        return ok;
    }
};
} // namespace

bool parseLatestRelease(const std::string& json, ReleaseInfo& out) {
    out = ReleaseInfo();
    JParser p(json); JVal root;
    if (!p.val(root) || root.t != JVal::Obj) return false;
    const JVal* tag = root.get("tag_name");
    if (!tag || tag->t != JVal::Str || tag->s.empty()) return false;
    out.tag = tag->s;
    if (const JVal* h = root.get("html_url"); h && h->t == JVal::Str) out.htmlUrl = h->s;
    if (const JVal* a = root.get("assets"); a && a->t == JVal::Arr) {
        for (auto& e : a->arr) {
            if (e.t != JVal::Obj) continue;
            const JVal* n = e.get("name"); const JVal* u = e.get("browser_download_url"); const JVal* sz = e.get("size");
            if (!n || !u || n->t != JVal::Str || u->t != JVal::Str) continue;
            ReleaseAsset ra; ra.name = n->s; ra.url = u->s;
            if (sz && sz->t == JVal::Num && sz->n > 0) ra.size = uint64_t(sz->n);
            out.assets.push_back(ra);
        }
    }
    return true;
}

static bool endsWith(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}
static std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower((unsigned char)c)); return s; }

bool pickAssets(const ReleaseInfo& rel, ReleaseAsset& zip, ReleaseAsset& sha) {
    for (auto& a : rel.assets) {
        std::string n = lower(a.name);
        if (n.rfind("lbncoopsa", 0) == 0 && endsWith(n, ".zip")) {
            for (auto& b : rel.assets)
                if (lower(b.name) == n + ".sha256") {
                    if (a.url.rfind("https://", 0) != 0 || b.url.rfind("https://", 0) != 0) continue;
                    zip = a; sha = b; return true;
                }
        }
    }
    return false;
}

std::string safeRelativePath(const std::string& in) {
    if (in.empty()) return "";
    std::string p = in;
    for (auto& c : p) if (c == '\\') c = '/';
    if (p[0] == '/' || p.find(':') != std::string::npos) return "";
    std::string out, part;
    size_t i = 0;
    while (i <= p.size()) {
        if (i == p.size() || p[i] == '/') {
            if (part.empty()) { if (i == p.size() && !out.empty()) break; return ""; }
            if (part == "." || part == "..") return "";
            if (part.back() == '.' || part.back() == ' ') return ""; // Windows trims these: ambiguous
            for (unsigned char c : part) if (c < 32 || std::strchr("<>\"|?*", c)) return "";
            if (!out.empty()) out += '/';
            out += part; part.clear();
        } else part += p[i];
        ++i;
    }
    return out;
}

FileAction classifyEntry(const std::string& np) {
    std::string n = lower(np);
    size_t slash = n.find('/');
    if (slash == std::string::npos) {
        if (n == "lbncoopsa.ini") return FileAction::ExampleIni;
        if (n == "dinput8.dll") return FileAction::IfOursOnly;
        if (n == "lbncoopsa.asi" || n == "lbncoopsa-iniciar.exe" || n == "lbncoopsa.version" ||
            n == "leia-me.txt" || n == "sha256sums.txt")
            return FileAction::Replace;
        return FileAction::Skip;
    }
    std::string dir = n.substr(0, slash), rest = n.substr(slash + 1);
    if (rest.empty() || rest.find('/') != std::string::npos) return FileAction::Skip; // one level only
    if (dir == "extras" && rest.rfind("lbncoopsa", 0) == 0) {
        if (endsWith(rest, ".ini")) return FileAction::ExampleIni;
        if (endsWith(rest, ".exe") || endsWith(rest, ".txt")) return FileAction::Replace;
    }
    if (dir == "licencas" && endsWith(rest, ".txt")) return FileAction::Replace;
    return FileAction::Skip;
}

std::string exampleIniName(const std::string& np) {
    if (endsWith(lower(np), ".ini")) return np.substr(0, np.size() - 4) + "-exemplo.ini";
    return np + "-exemplo";
}

bool sumsListHash(const std::string& text, const std::string& path, const std::string& hashHex) {
    std::string want = lower(path); for (auto& c : want) if (c == '\\') c = '/';
    size_t i = 0;
    while (i < text.size()) {
        size_t e = text.find('\n', i); if (e == std::string::npos) e = text.size();
        std::string line = trim(text.substr(i, e - i));
        i = e + 1;
        if (line.size() < 66) continue;
        std::string h = lower(line.substr(0, 64));
        std::string name = trim(line.substr(64));
        if (!name.empty() && name[0] == '*') name.erase(0, 1);
        name = lower(name); for (auto& c : name) if (c == '\\') c = '/';
        if (name == want && h == lower(hashHex)) return true;
    }
    return false;
}

} // namespace lbn::upd
