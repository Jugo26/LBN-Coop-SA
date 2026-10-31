// LBN Coop SA - Iniciador (LbnCoopSA-Iniciar.exe)
// Fica na pasta do gta_sa.exe. Confere no GitHub se ha versao nova do mod, baixa o .zip + .sha256,
// confere o hash, faz backup dos arquivos do mod em LbnCoopSA_backup\<versao antiga>\, troca SOMENTE os
// arquivos do mod (nunca o LbnCoopSA.ini do usuario) e abre o gta_sa.exe. Qualquer falha => abre o jogo
// com a versao atual. Nada e apagado.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "update_core.h"
#include "../common/version.h"
extern "C" {
#include "miniz.h"
}

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif

using namespace lbn::upd;

namespace {

const char* kDefaultApiUrl = "https://api.github.com/repos/Jugo26/LBN-Coop-SA/releases/latest";
const wchar_t* kUserAgent = L"LbnCoopSA-Iniciar/" LBN_MOD_VERSION_W;

std::wstring g_dir;          // game folder (where this exe is), with trailing backslash
std::wstring g_selfPath;     // full path of this exe
HANDLE g_log = INVALID_HANDLE_VALUE;
bool g_hadError = false;

std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), &w[0], n);
    return w;
}
std::string narrow(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

void writeLog(const std::string& line) {
    if (g_log == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st; GetLocalTime(&st);
    char ts[40];
    std::snprintf(ts, sizeof ts, "%04d-%02d-%02d %02d:%02d:%02d ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    std::string l = ts + line + "\r\n";
    DWORD wr; WriteFile(g_log, l.data(), DWORD(l.size()), &wr, nullptr);
}

void consoleOut(const std::string& utf8) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE || h == nullptr) return;
    std::wstring w = widen(utf8 + "\r\n");
    DWORD wr, mode;
    if (GetConsoleMode(h, &mode)) WriteConsoleW(h, w.data(), DWORD(w.size()), &wr, nullptr);
    else { std::string s = utf8 + "\r\n"; WriteFile(h, s.data(), DWORD(s.size()), &wr, nullptr); }
}

// Message for the player (console) + log.
void say(const char* fmt, ...) {
    char buf[2048]; va_list ap; va_start(ap, fmt); std::vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    consoleOut(buf); writeLog(buf);
}
// Log only (details).
void logf(const char* fmt, ...) {
    char buf[2048]; va_list ap; va_start(ap, fmt); std::vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    writeLog(buf);
}
void fail(const char* fmt, ...) {
    char buf[2048]; va_list ap; va_start(ap, fmt); std::vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    g_hadError = true;
    consoleOut(std::string("ERRO: ") + buf); writeLog(std::string("ERRO: ") + buf);
}

std::string winErr(DWORD e) {
    wchar_t* msg = nullptr;
    HMODULE mod = GetModuleHandleW(L"winhttp.dll");
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    if (e >= 12000 && e < 13000 && mod) flags |= FORMAT_MESSAGE_FROM_HMODULE;
    std::string r = "codigo " + std::to_string(e);
    if (FormatMessageW(flags, mod, e, 0, (LPWSTR)&msg, 0, nullptr) && msg) {
        std::string t = trim(narrow(msg)); LocalFree(msg);
        if (!t.empty()) r += " - " + t;
    }
    if (e == ERROR_WINHTTP_TIMEOUT) r += " (tempo esgotado)";
    if (e == ERROR_WINHTTP_NAME_NOT_RESOLVED) r += " (sem internet ou DNS)";
    if (e == ERROR_WINHTTP_CANNOT_CONNECT) r += " (nao conectou)";
    return r;
}

bool fileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
bool readFile(const std::wstring& p, std::string& out, size_t maxBytes = 512u << 20) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz; if (!GetFileSizeEx(h, &sz) || uint64_t(sz.QuadPart) > maxBytes) { CloseHandle(h); return false; }
    out.resize(size_t(sz.QuadPart));
    DWORD rd = 0; bool ok = true; size_t off = 0;
    while (off < out.size()) {
        DWORD want = DWORD(out.size() - off > (1u << 20) ? (1u << 20) : out.size() - off);
        if (!ReadFile(h, &out[off], want, &rd, nullptr) || rd == 0) { ok = false; break; }
        off += rd;
    }
    CloseHandle(h);
    return ok;
}
bool writeFileAll(const std::wstring& p, const void* data, size_t n) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const char* c = static_cast<const char*>(data); size_t off = 0; bool ok = true;
    while (off < n) {
        DWORD wr = 0, want = DWORD(n - off > (1u << 20) ? (1u << 20) : n - off);
        if (!WriteFile(h, c + off, want, &wr, nullptr) || wr == 0) { ok = false; break; }
        off += wr;
    }
    if (!FlushFileBuffers(h)) ok = false;
    CloseHandle(h);
    return ok;
}
// Creates every directory of 'path' (a directory path, may end with '\').
bool makeDirs(const std::wstring& path) {
    std::wstring cur;
    for (size_t i = 0; i < path.size(); ++i) {
        cur += path[i];
        if ((path[i] == L'\\' || i + 1 == path.size()) && cur.size() > 3) {
            std::wstring d = cur; if (d.back() == L'\\') d.pop_back();
            DWORD a = GetFileAttributesW(d.c_str());
            if (a == INVALID_FILE_ATTRIBUTES) { if (!CreateDirectoryW(d.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false; }
            else if (!(a & FILE_ATTRIBUTE_DIRECTORY)) return false;
        }
    }
    return true;
}
std::wstring parentDir(const std::wstring& p) { size_t k = p.find_last_of(L'\\'); return k == std::wstring::npos ? L"" : p.substr(0, k + 1); }
std::wstring toWinRel(const std::string& norm) { std::wstring w = widen(norm); for (auto& c : w) if (c == L'/') c = L'\\'; return w; }

// ---------------- HTTP (WinHTTP) ----------------
struct HttpResult {
    DWORD status = 0;
    std::string body;
    std::string error;           // non-empty => transport failure
    std::string rateRemaining;   // X-RateLimit-Remaining
    std::string rateReset;       // X-RateLimit-Reset (unix time)
};

std::string queryHeader(HINTERNET req, const wchar_t* name) {
    wchar_t buf[256]; DWORD len = sizeof buf;
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CUSTOM, name, buf, &len, WINHTTP_NO_HEADER_INDEX)) return narrow(std::wstring(buf, len / sizeof(wchar_t)));
    return "";
}

HttpResult httpGet(const std::string& url, bool api, int timeoutMs, size_t maxBytes, DWORD totalLimitMs) {
    HttpResult r;
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc{}; uc.dwStructSize = sizeof uc;
    wchar_t host[256], path[4096];
    uc.lpszHostName = host; uc.dwHostNameLength = 256; uc.lpszUrlPath = path; uc.dwUrlPathLength = 4096;
    wchar_t extra[2048]; uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) { r.error = "URL invalida: " + url; return r; }
    if (uc.nScheme != INTERNET_SCHEME_HTTPS) { r.error = "so HTTPS e aceito: " + url; return r; }
    std::wstring fullPath = std::wstring(path, uc.dwUrlPathLength) + std::wstring(extra, uc.dwExtraInfoLength);

    HINTERNET ses = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) ses = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) { r.error = "WinHttpOpen: " + winErr(GetLastError()); return r; }
    WinHttpSetTimeouts(ses, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    DWORD protos = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    if (!WinHttpSetOption(ses, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof protos)) {
        protos = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(ses, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof protos);
    }
    HINTERNET con = WinHttpConnect(ses, std::wstring(host, uc.dwHostNameLength).c_str(), uc.nPort, 0);
    HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", fullPath.c_str(), nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    if (!req) {
        r.error = "conexao: " + winErr(GetLastError());
        if (con) WinHttpCloseHandle(con);
        WinHttpCloseHandle(ses); return r;
    }
#ifdef LBN_TEST_INSECURE_TLS
    // TEST BUILD ONLY (lbn_launcher_testtls.exe, never shipped): accept the local self-signed test server.
    DWORD sec = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    WinHttpSetOption(req, WINHTTP_OPTION_SECURITY_FLAGS, &sec, sizeof sec);
#endif
    DWORD redir = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof redir);
    std::wstring hdr = api ? L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n"
                           : L"Accept: application/octet-stream\r\n";
    DWORD start = GetTickCount();
    if (!WinHttpSendRequest(req, hdr.c_str(), DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req, nullptr)) {
        r.error = winErr(GetLastError());
    } else {
        DWORD st = 0, len = sizeof st;
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &st, &len, WINHTTP_NO_HEADER_INDEX);
        r.status = st;
        r.rateRemaining = queryHeader(req, L"X-RateLimit-Remaining");
        r.rateReset = queryHeader(req, L"X-RateLimit-Reset");
        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(req, &avail)) { r.error = "download: " + winErr(GetLastError()); break; }
            if (avail == 0) break;
            if (r.body.size() + avail > maxBytes) { r.error = "resposta grande demais"; break; }
            size_t off = r.body.size(); r.body.resize(off + avail);
            DWORD rd = 0;
            if (!WinHttpReadData(req, &r.body[off], avail, &rd)) { r.error = "download: " + winErr(GetLastError()); break; }
            r.body.resize(off + rd);
            if (GetTickCount() - start > totalLimitMs) { r.error = "download demorou demais"; break; }
        }
    }
    WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
    return r;
}

std::string describeHttpFailure(const HttpResult& r) {
    if (!r.error.empty()) return "sem conexao com o GitHub (sem internet?) - " + r.error;
    if ((r.status == 403 || r.status == 429) && (r.rateRemaining == "0" || r.status == 429)) {
        std::string s = "limite de consultas da API do GitHub atingido (HTTP " + std::to_string(r.status) + ")";
        if (!r.rateReset.empty()) {
            long long reset = std::atoll(r.rateReset.c_str());
            FILETIME ft; GetSystemTimeAsFileTime(&ft);
            long long now = (((long long)ft.dwHighDateTime << 32 | ft.dwLowDateTime) - 116444736000000000LL) / 10000000LL;
            if (reset > now) s += "; libera em ~" + std::to_string((reset - now + 59) / 60) + " min";
        }
        return s;
    }
    if (r.status == 404) return "nenhuma versao publicada encontrada (HTTP 404)";
    return "resposta inesperada do servidor (HTTP " + std::to_string(r.status) + ")";
}

// ---------------- update ----------------
struct PlanItem {
    std::string entry;        // normalized zip path
    std::wstring dest;        // relative destination (Windows separators)
    std::string data;
    bool existed = false;
    bool isSelf = false;
    bool done = false;
    std::wstring backup;      // full backup path (if existed)
};

bool sameFile(const std::wstring& a, const std::wstring& b) {
    wchar_t fa[MAX_PATH * 2], fb[MAX_PATH * 2];
    if (!GetFullPathNameW(a.c_str(), MAX_PATH * 2, fa, nullptr) || !GetFullPathNameW(b.c_str(), MAX_PATH * 2, fb, nullptr)) return false;
    return lstrcmpiW(fa, fb) == 0;
}

std::wstring uniqueBackupDir(const std::string& oldVer) {
    std::wstring base = g_dir + L"LbnCoopSA_backup\\" + widen(oldVer);
    std::wstring d = base;
    for (int i = 2; GetFileAttributesW(d.c_str()) != INVALID_FILE_ATTRIBUTES; ++i) d = base + L"_" + std::to_wstring(i);
    return d + L"\\";
}

void rollback(std::vector<PlanItem>& plan) {
    say("Desfazendo a atualizacao (restaurando o backup)...");
    for (auto it = plan.rbegin(); it != plan.rend(); ++it) {
        if (!it->done) continue;
        std::wstring dest = g_dir + it->dest;
        if (it->existed) {
            bool ok;
            if (it->isSelf) {
                std::wstring aside = dest + L".falhou";
                MoveFileExW(dest.c_str(), aside.c_str(), MOVEFILE_REPLACE_EXISTING);
                ok = MoveFileExW(it->backup.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
            } else {
                ok = CopyFileW(it->backup.c_str(), dest.c_str(), FALSE) != 0;
            }
            logf("  restaurado %s: %s", narrow(it->dest).c_str(), ok ? "ok" : winErr(GetLastError()).c_str());
        } else {
            logf("  arquivo novo mantido (nao apagamos nada): %s", narrow(it->dest).c_str());
        }
    }
}

// Returns true if the mod was updated.
bool runUpdate(const std::string& apiUrl, const SemVer& local, int timeoutMs) {
    say("Procurando atualizacao do LBN Coop SA...");
    logf("Consultando %s (timeout %d ms)", apiUrl.c_str(), timeoutMs);
    HttpResult api = httpGet(apiUrl, true, timeoutMs, 2u << 20, DWORD(timeoutMs) * 2);
    if (!api.error.empty() || api.status != 200) {
        fail("nao foi possivel verificar atualizacao: %s.", describeHttpFailure(api).c_str());
        return false;
    }
    ReleaseInfo rel;
    if (!parseLatestRelease(api.body, rel)) { fail("resposta do GitHub nao reconhecida (JSON invalido)."); return false; }
    SemVer remote = parseVersion(rel.tag);
    logf("Ultima versao publicada: %s (%s)", rel.tag.c_str(), rel.htmlUrl.c_str());
    if (!remote.valid) { fail("versao publicada com nome invalido: '%s'.", rel.tag.c_str()); return false; }
    if (compareVersion(remote, local) <= 0) {
        say("Voce ja esta na versao mais recente (v%s).", versionString(local).c_str());
        return false;
    }
    say("Nova versao disponivel: v%s (instalada: v%s).", versionString(remote).c_str(), versionString(local).c_str());

    ReleaseAsset zipA, shaA;
    if (!pickAssets(rel, zipA, shaA)) { fail("a versao v%s nao tem o .zip e o .sha256 esperados.", versionString(remote).c_str()); return false; }
    say("Baixando %s...", zipA.name.c_str());
    HttpResult sha = httpGet(shaA.url, false, timeoutMs * 2, 4096, 60000);
    if (!sha.error.empty() || sha.status != 200) { fail("falha ao baixar %s: %s.", shaA.name.c_str(), describeHttpFailure(sha).c_str()); return false; }
    std::string want = parseSha256File(sha.body);
    if (want.empty()) { fail("arquivo %s sem um SHA-256 valido.", shaA.name.c_str()); return false; }
    HttpResult zip = httpGet(zipA.url, false, timeoutMs * 3, 200u << 20, 300000);
    if (!zip.error.empty() || zip.status != 200) { fail("falha ao baixar %s: %s.", zipA.name.c_str(), describeHttpFailure(zip).c_str()); return false; }
    std::string got = sha256Hex(zip.body.data(), zip.body.size());
    logf("Baixado %s: %zu bytes, SHA-256 %s (esperado %s)", zipA.name.c_str(), zip.body.size(), got.c_str(), want.c_str());
    if (got != want) { fail("o SHA-256 do download NAO confere (arquivo corrompido ou alterado). Nada foi trocado."); return false; }
    say("Download conferido (SHA-256 ok).");

    // ---- read zip ----
    mz_zip_archive za; std::memset(&za, 0, sizeof za);
    if (!mz_zip_reader_init_mem(&za, zip.body.data(), zip.body.size(), 0)) { fail("o .zip baixado esta invalido."); return false; }
    std::vector<PlanItem> plan;
    bool zipOk = true;
    mz_uint n = mz_zip_reader_get_num_files(&za);
    for (mz_uint i = 0; i < n && zipOk; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&za, i, &st)) { zipOk = false; break; }
        if (mz_zip_reader_is_file_a_directory(&za, i)) continue;
        std::string norm = safeRelativePath(st.m_filename);
        if (norm.empty()) { logf("  ignorado (caminho inseguro): %s", st.m_filename); continue; }
        FileAction act = classifyEntry(norm);
        if (act == FileAction::Skip) { logf("  ignorado (nao e arquivo do mod): %s", norm.c_str()); continue; }
        if (st.m_uncomp_size > (100u << 20)) { zipOk = false; break; }
        size_t sz = 0;
        void* p = mz_zip_reader_extract_to_heap(&za, i, &sz, 0);   // CRC-32 checked by miniz
        if (!p) { zipOk = false; break; }
        PlanItem it; it.entry = norm; it.data.assign(static_cast<char*>(p), sz); mz_free(p);
        it.dest = toWinRel(norm);
        // Where is the ASI installed? Respect scripts\ if that is where the user put it.
        std::string lo = norm; for (auto& c : lo) c = char(tolower((unsigned char)c));
        if ((lo == "lbncoopsa.asi" || lo == "lbncoopsa.ini") && !fileExists(g_dir + L"LbnCoopSA.asi") &&
            fileExists(g_dir + L"scripts\\LbnCoopSA.asi"))
            it.dest = L"scripts\\" + it.dest;
        std::wstring full = g_dir + it.dest;
        it.existed = fileExists(full);
        std::string cur;
        if (it.existed && readFile(full, cur) && cur == it.data) { logf("  sem mudanca: %s", narrow(it.dest).c_str()); continue; }
        if (act == FileAction::ExampleIni && it.existed) {
            it.dest = toWinRel(exampleIniName(narrow(it.dest)));
            full = g_dir + it.dest;
            it.existed = fileExists(full);
            if (it.existed && readFile(full, cur) && cur == it.data) continue;
            say("Suas configuracoes em %s foram mantidas; o modelo novo foi salvo como %s.",
                norm.c_str(), narrow(it.dest).c_str());
        }
        if (act == FileAction::IfOursOnly) {
            if (!it.existed) { logf("  dinput8.dll nao instalado aqui (talvez outro ASI loader): nao adicionado"); continue; }
            std::string sums; readFile(g_dir + L"SHA256SUMS.txt", sums, 1u << 20);
            if (!sumsListHash(sums, "dinput8.dll", sha256Hex(cur.data(), cur.size()))) {
                logf("  dinput8.dll atual nao e o que o mod instalou: mantido como esta"); continue;
            }
        }
        it.isSelf = it.existed && sameFile(full, g_selfPath);
        plan.push_back(std::move(it));
    }
    mz_zip_reader_end(&za);
    if (!zipOk) { fail("erro ao extrair o .zip baixado. Nada foi trocado."); return false; }

    // The zip must contain the version file matching the release tag (sanity check).
    bool verOk = false;
    {
        mz_zip_archive z2; std::memset(&z2, 0, sizeof z2);
        if (mz_zip_reader_init_mem(&z2, zip.body.data(), zip.body.size(), 0)) {
            size_t sz = 0; void* p = mz_zip_reader_extract_file_to_heap(&z2, "LbnCoopSA.version", &sz, 0);
            if (p) { verOk = compareVersion(parseVersion(std::string((char*)p, sz)), remote) == 0; mz_free(p); }
            mz_zip_reader_end(&z2);
        }
    }
    if (!verOk) { fail("o .zip da v%s nao tem um LbnCoopSA.version coerente. Nada foi trocado.", versionString(remote).c_str()); return false; }

    // Version file last, so an interrupted update is retried next time.
    std::stable_partition(plan.begin(), plan.end(), [](const PlanItem& p) { return p.entry != "LbnCoopSA.version"; });

    // ---- backup ----
    std::wstring bdir = uniqueBackupDir(versionString(local));
    bool needBackup = false;
    for (auto& it : plan) if (it.existed) needBackup = true;
    if (needBackup) {
        if (!makeDirs(bdir)) { fail("nao consegui criar a pasta de backup %s.", narrow(bdir).c_str()); return false; }
        for (auto& it : plan) {
            if (!it.existed || it.isSelf) continue; // the running launcher is moved (not copied) during the swap
            it.backup = bdir + it.dest;
            if (!makeDirs(parentDir(it.backup)) || !CopyFileW((g_dir + it.dest).c_str(), it.backup.c_str(), TRUE)) {
                fail("falha no backup de %s (%s). Nada foi trocado.", narrow(it.dest).c_str(), winErr(GetLastError()).c_str());
                return false;
            }
        }
        logf("Backup feito em %s", narrow(bdir).c_str());
    }

    // ---- install ----
    for (auto& it : plan) {
        std::wstring dest = g_dir + it.dest;
        std::wstring tmp = dest + L".lbnnovo";
        if (fileExists(tmp)) {               // leftover of an interrupted run: move it aside, do not delete
            std::wstring aside = tmp + L"." + std::to_wstring(GetTickCount());
            MoveFileExW(tmp.c_str(), aside.c_str(), 0);
        }
        bool ok = makeDirs(parentDir(dest)) && writeFileAll(tmp, it.data.data(), it.data.size());
        if (ok && it.isSelf) {
            it.backup = bdir + it.dest;
            ok = makeDirs(parentDir(it.backup)) && MoveFileExW(dest.c_str(), it.backup.c_str(), 0);
            if (ok) {
                it.done = true;
                ok = MoveFileExW(tmp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
            }
        } else if (ok) {
            ok = MoveFileExW(tmp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
            if (ok) it.done = true;
        }
        if (!ok) {
            DWORD e = GetLastError();
            fail("nao consegui gravar %s (%s). O jogo esta aberto?", narrow(it.dest).c_str(), winErr(e).c_str());
            rollback(plan);
            return false;
        }
        logf("  instalado: %s%s", narrow(it.dest).c_str(), it.existed ? " (substituido; copia no backup)" : " (novo)");
    }
    say("LBN Coop SA atualizado para v%s. Backup da versao anterior em %s", versionString(remote).c_str(),
        needBackup ? narrow(bdir.substr(g_dir.size())).c_str() : "(nada a guardar)");
    return true;
}

bool launchGame(const std::wstring& extraArgs) {
    std::wstring exe = g_dir + L"gta_sa.exe";
    if (!fileExists(exe)) {
        fail("gta_sa.exe nao encontrado em %s. Coloque o LbnCoopSA-Iniciar.exe na pasta do GTA San Andreas.", narrow(g_dir).c_str());
        return false;
    }
    std::wstring cmd = L"\"" + exe + L"\"" + extraArgs;
    STARTUPINFOW si{}; si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, g_dir.c_str(), &si, &pi)) {
        fail("nao consegui abrir o gta_sa.exe (%s).", winErr(GetLastError()).c_str());
        return false;
    }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    say("Abrindo o GTA San Andreas...");
    return true;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    wchar_t self[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, self, MAX_PATH * 2);
    g_selfPath.assign(self, n);
    g_dir = parentDir(g_selfPath);
    SetConsoleTitleW(L"LBN Coop SA - Iniciador");

    bool noUpdate = false, noGame = false, noPause = false;
    std::string apiUrl = kDefaultApiUrl;
    int timeoutMs = 6000;
    std::wstring extra;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--sem-atualizar") noUpdate = true;
        else if (a == L"--nao-abrir-jogo") noGame = true;
        else if (a == L"--sem-pausa") noPause = true;
        else if (a.rfind(L"--api-url=", 0) == 0) apiUrl = narrow(a.substr(10));
        else if (a.rfind(L"--timeout-ms=", 0) == 0) timeoutMs = _wtoi(a.c_str() + 13);
        else extra += L" \"" + a + L"\"";   // forwarded to gta_sa.exe
    }
    if (timeoutMs < 500) timeoutMs = 500;
    if (timeoutMs > 60000) timeoutMs = 60000;

    g_log = CreateFileW((g_dir + L"LbnCoopSA-update.log").c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    std::string verText;
    SemVer local;
    if (readFile(g_dir + L"LbnCoopSA.version", verText, 4096)) local = parseVersion(verText);
    say("LBN Coop SA - Iniciador (v" LBN_MOD_VERSION ")");
    logf("Pasta do jogo: %s", narrow(g_dir).c_str());
    if (local.valid) say("Versao instalada do mod: v%s", versionString(local).c_str());
    else say("Versao instalada do mod: desconhecida (LbnCoopSA.version ausente ou invalido).");

    if (noUpdate) say("Atualizacao desligada (--sem-atualizar).");
    else if (!runUpdate(apiUrl, local, timeoutMs) && g_hadError)
        say("Abrindo o jogo com a versao atual (v%s).", versionString(local).c_str());

    int rc = 0;
    if (noGame) say("(teste) --nao-abrir-jogo: o jogo nao sera aberto.");
    else if (!launchGame(extra)) rc = 1;
    logf("----");
    if (g_log != INVALID_HANDLE_VALUE) CloseHandle(g_log);
    if (!noPause) Sleep(g_hadError ? 6000 : 1500);
    return rc;
}
