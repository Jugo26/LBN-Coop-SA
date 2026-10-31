#include "testfw.h"
#include "../src/launcher/update_core.h"
#include "../src/common/version.h"

using namespace lbn::upd;

TEST(updater_version_parse_compare) {
    CHECK(parseVersion("0.1.0").valid);
    CHECK(parseVersion("v0.1.0\r\n").valid);
    CHECK(parseVersion("\xEF\xBB\xBF" "0.1.0").valid);
    CHECK(parseVersion("0.1.0-etapa1").valid);
    CHECK(!parseVersion("0.1").valid);
    CHECK(!parseVersion("abc").valid);
    CHECK(!parseVersion("1.2.3x").valid);
    CHECK(compareVersion(parseVersion("v0.2.0"), parseVersion("0.1.9")) > 0);
    CHECK(compareVersion(parseVersion("0.1.10"), parseVersion("0.1.9")) > 0);
    CHECK(compareVersion(parseVersion("1.0.0"), parseVersion("0.99.99")) > 0);
    CHECK(compareVersion(parseVersion("v0.1.0"), parseVersion("0.1.0")) == 0);
    CHECK(compareVersion(parseVersion("0.1.0"), parseVersion("")) > 0);  // missing local version => update
    CHECK(versionString(parseVersion(LBN_MOD_VERSION)) == LBN_MOD_VERSION);
}

TEST(updater_sha256_vectors) {
    CHECK(sha256Hex("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256Hex("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const char* m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK(sha256Hex(m, 56) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    std::string mil(1000000, 'a');
    CHECK(sha256Hex(mil.data(), mil.size()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    Sha256 s; for (char c : std::string("abc")) s.update(&c, 1);
    uint8_t d[32]; s.final(d); CHECK(d[0] == 0xba && d[31] == 0xad);
    CHECK(parseSha256File("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD  LbnCoopSA-v0.1.0.zip\n") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(parseSha256File("nada aqui 1234").empty());
}

TEST(updater_parse_github_release) {
    const char* json = R"({"url":"x","html_url":"https://github.com/Jugo26/LBN-Coop-SA/releases/tag/v0.2.0",
      "tag_name" : "v0.2.0", "name":"LBN Coop SA v0.2.0 \u00e7\"q\"", "draft":false, "prerelease":false,
      "author":{"login":"Jugo26","id":1,"site_admin":false}, "body":null,
      "assets":[
        {"name":"LbnCoopSA-v0.2.0.zip.sha256","size":87,"browser_download_url":"https:\/\/github.com\/Jugo26\/LBN-Coop-SA\/releases\/download\/v0.2.0\/LbnCoopSA-v0.2.0.zip.sha256"},
        {"name":"LbnCoopSA-v0.2.0.zip","size":2100000,"browser_download_url":"https://github.com/Jugo26/LBN-Coop-SA/releases/download/v0.2.0/LbnCoopSA-v0.2.0.zip","uploader":{"a":[1,2,{"b":-1.5e3}]}}
      ]})";
    ReleaseInfo r;
    CHECK(parseLatestRelease(json, r));
    CHECK(r.tag == "v0.2.0");
    CHECK(r.assets.size() == 2);
    ReleaseAsset z, s;
    CHECK(pickAssets(r, z, s));
    CHECK(z.name == "LbnCoopSA-v0.2.0.zip" && z.size == 2100000);
    CHECK(s.url == "https://github.com/Jugo26/LBN-Coop-SA/releases/download/v0.2.0/LbnCoopSA-v0.2.0.zip.sha256");
    CHECK(!parseLatestRelease(R"({"message":"API rate limit exceeded"})", r));
    CHECK(!parseLatestRelease("<html>", r));
    CHECK(!parseLatestRelease(R"({"tag_name":"v1.0.0")", r)); // truncated
    ReleaseInfo noSha; noSha.tag = "v1.0.0"; noSha.assets.push_back({"LbnCoopSA-v1.0.0.zip", "https://x/y.zip", 1});
    CHECK(!pickAssets(noSha, z, s));
    ReleaseInfo http; http.tag = "v1.0.0";
    http.assets.push_back({"LbnCoopSA-v1.0.0.zip", "http://x/y.zip", 1});
    http.assets.push_back({"LbnCoopSA-v1.0.0.zip.sha256", "http://x/y.zip.sha256", 1});
    CHECK(!pickAssets(http, z, s)); // https only
}

TEST(updater_path_safety_and_classification) {
    CHECK(safeRelativePath("extras/LbnCoopSA-Server.exe") == "extras/LbnCoopSA-Server.exe");
    CHECK(safeRelativePath("extras\\LbnCoopSA-Server.exe") == "extras/LbnCoopSA-Server.exe");
    CHECK(safeRelativePath("../gta_sa.exe").empty());
    CHECK(safeRelativePath("extras/../../x").empty());
    CHECK(safeRelativePath("/etc/passwd").empty());
    CHECK(safeRelativePath("C:/Windows/x.dll").empty());
    CHECK(safeRelativePath("a//b").empty());
    CHECK(safeRelativePath("a./b").empty());

    CHECK(classifyEntry("LbnCoopSA.asi") == FileAction::Replace);
    CHECK(classifyEntry("LbnCoopSA-Iniciar.exe") == FileAction::Replace);
    CHECK(classifyEntry("LbnCoopSA.version") == FileAction::Replace);
    CHECK(classifyEntry("LEIA-ME.txt") == FileAction::Replace);
    CHECK(classifyEntry("LbnCoopSA.ini") == FileAction::ExampleIni);
    CHECK(classifyEntry("extras/LbnCoopSA-amigo.ini") == FileAction::ExampleIni);
    CHECK(classifyEntry("dinput8.dll") == FileAction::IfOursOnly);
    CHECK(classifyEntry("extras/LbnCoopSA-Server.exe") == FileAction::Replace);
    CHECK(classifyEntry("licencas/miniz-LICENSE.txt") == FileAction::Replace);
    // never game files or anything unknown
    CHECK(classifyEntry("gta_sa.exe") == FileAction::Skip);
    CHECK(classifyEntry("models/gta3.img") == FileAction::Skip);
    CHECK(classifyEntry("data/handling.cfg") == FileAction::Skip);
    CHECK(classifyEntry("vorbisFile.dll") == FileAction::Skip);
    CHECK(classifyEntry("scripts/LbnCoopSA.asi") == FileAction::Skip);
    CHECK(classifyEntry("extras/sub/LbnCoopSA-x.exe") == FileAction::Skip);
    CHECK(exampleIniName("LbnCoopSA.ini") == "LbnCoopSA-exemplo.ini");
    CHECK(exampleIniName("extras/LbnCoopSA-amigo.ini") == "extras/LbnCoopSA-amigo-exemplo.ini");

    std::string sums = "f0e76c41b6d96270b33dd8c0e27822a5efb631c839ec9a6671ab449f2fca28a1  dinput8.dll\r\n"
                       "00d6c9d1b14842f9b459c53aa663f37683bf077fb2cf50f633e68c2c60bed61d  extras/LbnCoopSA-Server.exe\n";
    CHECK(sumsListHash(sums, "dinput8.dll", "F0E76C41B6D96270B33DD8C0E27822A5EFB631C839EC9A6671AB449F2FCA28A1"));
    CHECK(!sumsListHash(sums, "dinput8.dll", "00d6c9d1b14842f9b459c53aa663f37683bf077fb2cf50f633e68c2c60bed61d"));
    CHECK(sumsListHash(sums, "extras\\LbnCoopSA-Server.exe", "00d6c9d1b14842f9b459c53aa663f37683bf077fb2cf50f633e68c2c60bed61d"));
}
