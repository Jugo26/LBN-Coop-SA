// INI configuration and text helpers (original code for LBN Coop SA).
#include "config.h"
#include "protocol.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace lbn {

static std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

bool IniFile::loadFromFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    loadFromString(ss.str());
    return true;
}

void IniFile::loadFromString(const std::string& textIn) {
    std::string text = textIn;
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF)
        text.erase(0, 3); // tolerate a UTF-8 BOM
    std::string section;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[') {
            size_t e = line.find(']');
            section = lower(trim(line.substr(1, e == std::string::npos ? std::string::npos : e - 1)));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = lower(trim(line.substr(0, eq)));
        std::string val = trim(line.substr(eq + 1));
        // strip trailing inline comment " ; ..."
        size_t sc = val.find(" ;");
        if (sc != std::string::npos) val = trim(val.substr(0, sc));
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
        values_[section + "." + key] = val;
    }
}

bool IniFile::has(const std::string& s, const std::string& k) const {
    return values_.count(lower(s) + "." + lower(k)) != 0;
}
std::string IniFile::get(const std::string& s, const std::string& k, const std::string& def) const {
    auto it = values_.find(lower(s) + "." + lower(k));
    return it == values_.end() ? def : it->second;
}
int IniFile::getInt(const std::string& s, const std::string& k, int def) const {
    std::string v = get(s, k, "");
    if (v.empty()) return def;
    char* end = nullptr;
    long r = std::strtol(v.c_str(), &end, 0);
    if (end == v.c_str()) return def;
    return int(r);
}
bool IniFile::getBool(const std::string& s, const std::string& k, bool def) const {
    std::string v = lower(get(s, k, ""));
    if (v.empty()) return def;
    if (v == "1" || v == "true" || v == "sim" || v == "yes" || v == "on") return true;
    if (v == "0" || v == "false" || v == "nao" || v == "não" || v == "no" || v == "off") return false;
    return def;
}

const char* modeName(Mode m) { return m == Mode::Host ? "host" : "cliente"; }

CoopConfig configFromIni(const IniFile& ini, std::string* warnings) {
    CoopConfig c;
    const char* S = "LbnCoopSA";
    auto warn = [&](const std::string& w) { if (warnings) { *warnings += w; *warnings += "\n"; } };

    std::string nick = sanitizeNick(ini.get(S, "Nickname", c.nickname));
    if (nick.empty()) { warn("Nickname invalido; usando 'Jogador'."); nick = "Jogador"; }
    c.nickname = nick;

    std::string mode = lower(ini.get(S, "Mode", "join"));
    if (mode == "host" || mode == "hospedar" || mode == "servidor") c.mode = Mode::Host;
    else if (mode == "join" || mode == "cliente" || mode == "client" || mode == "entrar") c.mode = Mode::Join;
    else warn("Mode desconhecido '" + mode + "'; usando join.");

    c.hostIp = ini.get(S, "HostIP", c.hostIp);
    int port = ini.getInt(S, "Port", c.port);
    if (port < 1 || port > 65535) { warn("Port invalida; usando 7810."); port = LBN_DEFAULT_PORT; }
    c.port = uint16_t(port);

    int skin = ini.getInt(S, "Skin", c.skin);
    if (!(skin >= 0 && skin <= 0xFFFF && isValidPedSkin(uint16_t(skin)))) { warn("Skin " + std::to_string(skin) + " nao suportada; usando 106."); skin = 106; }
    c.skin = uint16_t(skin);

    c.autoStart = ini.getBool(S, "AutoStart", c.autoStart);
    c.sendRateHz = std::clamp(ini.getInt(S, "SendRate", c.sendRateHz), 5, 60);
    c.interpDelayMs = std::clamp(ini.getInt(S, "InterpDelayMs", c.interpDelayMs), 0, 500);
    c.remotePedCollision = ini.getBool(S, "RemotePedCollision", c.remotePedCollision);
    c.timeoutMs = std::clamp(ini.getInt(S, "TimeoutMs", c.timeoutMs), 3000, 60000);
    c.showNametags = ini.getBool(S, "ShowNametags", c.showNametags);
    c.nametagDistance = float(std::clamp(ini.getInt(S, "NametagDistance", int(c.nametagDistance)), 5, 500));
    c.serverName = ini.get(S, "ServerName", c.serverName);

    const char* K = "Hotkeys";
    c.keyMenu = ini.getInt(K, "Menu", c.keyMenu);
    c.keyStart = ini.getInt(K, "Start", c.keyStart);
    c.keyToggleMode = ini.getInt(K, "ToggleMode", c.keyToggleMode);
    c.keyDisconnect = ini.getInt(K, "Disconnect", c.keyDisconnect);
    c.keyUnstuck = ini.getInt(K, "Unstuck", c.keyUnstuck);
    std::string chatKey = ini.get(K, "Chat", "T");
    if (chatKey.size() == 1 && std::isalnum(static_cast<unsigned char>(chatKey[0])))
        c.keyChat = std::toupper(static_cast<unsigned char>(chatKey[0]));
    else if (!chatKey.empty())
        c.keyChat = ini.getInt(K, "Chat", c.keyChat);
    return c;
}

std::string defaultIniText() {
    return
        "; LBN Coop SA - configuracao (etapa 1)\r\n"
        "; Salve este arquivo como UTF-8 sem BOM ou ANSI. Linhas com ';' sao comentarios.\r\n"
        "[LbnCoopSA]\r\n"
        "; Seu apelido (1 a 24 caracteres, sem acentos de preferencia)\r\n"
        "Nickname=LBN\r\n"
        "; host = voce hospeda (o servidor roda dentro do seu jogo) | join = voce entra no host\r\n"
        "Mode=host\r\n"
        "; IP do host (para quem entra). No Radmin VPN use o IP 26.x do host.\r\n"
        "HostIP=26.196.166.86\r\n"
        "; Porta UDP (abra no firewall do host)\r\n"
        "Port=7810\r\n"
        "; Modelo do seu boneco como os outros o veem (ID de ped do SA). 106 = Grove. 0 (CJ) ainda nao suportado.\r\n"
        "Skin=106\r\n"
        "; 1 = conecta/hospeda sozinho quando o jogo carrega o mapa\r\n"
        "AutoStart=0\r\n"
        "; Pacotes de posicao por segundo (5-60). Padrao 30.\r\n"
        "SendRate=30\r\n"
        "; Atraso de interpolacao em ms (maior = mais suave, menor = mais responsivo). Padrao 120.\r\n"
        "InterpDelayMs=120\r\n"
        "; 0 = bonecos dos outros jogadores NAO tem colisao (nunca prendem/empurram voce). 1 = com colisao (antigo)\r\n"
        "RemotePedCollision=0\r\n"
        "; Tempo sem resposta ate considerar o jogador caido (ms)\r\n"
        "TimeoutMs=10000\r\n"
        "ShowNametags=1\r\n"
        "NametagDistance=80\r\n"
        "ServerName=LBN Coop SA\r\n"
        "\r\n"
        "[Hotkeys]\r\n"
        "; Codigos virtual-key do Windows em hexadecimal (0x76 = F7, 0x77 = F8, 0x78 = F9, 0x79 = F10)\r\n"
        "Start=0x76\r\n"
        "Menu=0x77\r\n"
        "ToggleMode=0x78\r\n"
        "Disconnect=0x79\r\n"
        "; F6 com o menu (F8) aberto = soltar o boneco (mesmo que /soltar no chat)\r\n"
        "Unstuck=0x75\r\n"
        "Chat=T\r\n";
}

std::string latin1ToUtf8(const std::string& in) {
    std::string out;
    for (unsigned char c : in) {
        if (c < 0x80) out += char(c);
        else { out += char(0xC0 | (c >> 6)); out += char(0x80 | (c & 0x3F)); }
    }
    return out;
}

std::string foldToAscii(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { out += char(c); continue; }
        // Decode a 2-byte UTF-8 sequence in the Latin-1 range.
        if ((c == 0xC2 || c == 0xC3) && i + 1 < s.size()) {
            unsigned cp = ((c & 0x1F) << 6) | ((unsigned char)s[i + 1] & 0x3F);
            ++i;
            static const char* map =
                // 0xC0..0xFF
                "AAAAAAACEEEEIIII" "DNOOOOOxOUUUUYTs"
                "aaaaaaaceeeeiiii" "dnooooo/ouuuuyty";
            if (cp >= 0xC0 && cp <= 0xFF) out += map[cp - 0xC0];
            else if (cp == 0xBA || cp == 0xB0) out += 'o';
            else if (cp == 0xAA) out += 'a';
            else out += '?';
            continue;
        }
        // Other multibyte sequences: skip continuation bytes, emit '?'.
        size_t len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;
        i += len - 1;
        out += '?';
    }
    return out;
}

std::string sanitizeNick(const std::string& in) {
    std::string a = foldToAscii(in);
    std::string out;
    for (char c : a) {
        unsigned char u = (unsigned char)c;
        if (std::isalnum(u) || c == '_' || c == '-' || c == '.' || c == '[' || c == ']' || c == ' ')
            out += c;
    }
    out = trim(out);
    if (out.size() > kMaxNickLen) out.resize(kMaxNickLen);
    return out;
}

std::string sanitizeChat(const std::string& in) {
    std::string a = foldToAscii(in);
    std::string out;
    for (char c : a) {
        unsigned char u = (unsigned char)c;
        if (u >= 32 && u < 127 && c != '~') out += c; // '~' starts GTA font tokens
    }
    out = trim(out);
    if (out.size() > kMaxChatLen) out.resize(kMaxChatLen);
    return out;
}

} // namespace lbn
