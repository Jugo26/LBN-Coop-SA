#include "testfw.h"
#include "../src/common/config.h"
#include "../src/common/protocol.h"

using namespace lbn;

TEST(ini_parse_basic) {
    IniFile ini;
    ini.loadFromString("\xEF\xBB\xBF; comentario\r\n[LbnCoopSA]\r\nNickname = LBN \r\nmode=HOST\r\nHostIP=26.196.166.86\r\nPort=7810 ; porta\r\nSkin=107\r\nAutoStart=sim\r\n[Hotkeys]\r\nMenu=0x77\r\nChat=y\r\n");
    std::string warn;
    CoopConfig c = configFromIni(ini, &warn);
    CHECK(c.nickname == "LBN");
    CHECK(c.mode == Mode::Host);
    CHECK(c.hostIp == "26.196.166.86");
    CHECK(c.port == 7810);
    CHECK(c.skin == 107);
    CHECK(c.autoStart);
    CHECK(c.keyMenu == 0x77);
    CHECK(c.keyChat == 'Y');
    CHECK(warn.empty());
}

TEST(ini_defaults_and_fallbacks) {
    IniFile ini;
    ini.loadFromString("[LbnCoopSA]\nPort=99999\nSkin=0\nMode=banana\nSendRate=1000\n");
    std::string warn;
    CoopConfig c = configFromIni(ini, &warn);
    CHECK(c.port == 7810);
    CHECK(c.skin == 106);
    CHECK(c.mode == Mode::Join);
    CHECK(c.sendRateHz == 60);
    CHECK(!warn.empty());
}

TEST(default_ini_text_parses) {
    IniFile ini; ini.loadFromString(defaultIniText());
    std::string warn;
    CoopConfig c = configFromIni(ini, &warn);
    CHECK(c.mode == Mode::Host && c.port == 7810 && c.hostIp == "26.196.166.86" && warn.empty());
    CHECK(c.keyStart == 0x76 && c.keyDisconnect == 0x79 && c.keyChat == 'T');
}

TEST(text_folding) {
    CHECK(foldToAscii("Ol\xC3\xA1, a\xC3\xA7\xC3\xA3o \xC3\x89") == "Ola, acao E");
    CHECK(latin1ToUtf8("\xE7") == "\xC3\xA7");
    CHECK(sanitizeChat("  oi~r~ \x01 tudo bem?  ") == "oir  tudo bem?");
    CHECK(sanitizeNick("  Jo\xC3\xA3o<script>  ") == "Joaoscript");
    CHECK(sanitizeNick(std::string(60, 'a')).size() == kMaxNickLen);
}

#ifdef LBN_SOURCE_DIR
TEST(dist_ini_files_parse) {
    for (const char* rel : {"/dist/LbnCoopSA.ini", "/dist/extras/LbnCoopSA-amigo.ini"}) {
        IniFile ini;
        bool loaded = ini.loadFromFile(std::string(LBN_SOURCE_DIR) + rel);
        CHECK(loaded);
        if (!loaded) continue;
        std::string warn;
        CoopConfig c = configFromIni(ini, &warn);
        CHECK(warn.empty());
        CHECK(c.port == 7810 && c.hostIp == "26.196.166.86");
        CHECK(c.keyStart == 0x76 && c.keyMenu == 0x77 && c.keyToggleMode == 0x78 && c.keyDisconnect == 0x79 && c.keyChat == 'T');
        CHECK(c.sendRateHz == 30 && c.interpDelayMs == 120 && !c.remotePedCollision && c.keyUnstuck == 0x75);
        if (std::string(rel).find("amigo") != std::string::npos) CHECK(c.mode == Mode::Join && c.nickname == "Amigo");
        else CHECK(c.mode == Mode::Host && c.nickname == "LBN");
    }
}
#endif

TEST(config_defaults_v011) {
    CoopConfig d;
    CHECK(d.sendRateHz == 30 && d.interpDelayMs == 120 && !d.remotePedCollision && d.keyUnstuck == 0x75);
    IniFile ini; ini.loadFromString(defaultIniText());
    std::string warn; CoopConfig c = configFromIni(ini, &warn);
    CHECK(warn.empty());
    CHECK(c.sendRateHz == 30 && c.interpDelayMs == 120 && !c.remotePedCollision && c.keyUnstuck == 0x75);
    IniFile old; old.loadFromString("[LbnCoopSA]\nRemotePedCollision=1\n[Hotkeys]\nUnstuck=0x7A\n");
    CoopConfig o = configFromIni(old);
    CHECK(o.remotePedCollision && o.keyUnstuck == 0x7A);
}
