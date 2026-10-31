#include <cstdio>
#include "testfw.h"
#include "../src/common/protocol.h"
#include "../src/common/bytes.h"
#include <cstdlib>
#include <limits>

using namespace lbn;

static PlayerState sampleFoot() {
    PlayerState s;
    s.seq = 42; s.timeMs = 123456; s.flags = 0;
    s.pos = {2495.5f, -1665.25f, 13.33f}; s.vel = {1.5f, -2.f, 0.f};
    s.heading = 1.25f; s.anim = MoveAnim::Sprint; s.health = 87; s.armour = 50;
    return s;
}

TEST(bytes_roundtrip) {
    ByteWriter w;
    w.u8(0xAB); w.u16(0xBEEF); w.u32(0xDEADBEEF); w.i16(-1234); w.f32(3.5f); w.str("ola");
    ByteReader r(w.data().data(), w.size());
    CHECK(r.u8() == 0xAB); CHECK(r.u16() == 0xBEEF); CHECK(r.u32() == 0xDEADBEEF);
    CHECK(r.i16() == -1234); CHECK(r.f32() == 3.5f); CHECK(r.str() == "ola");
    CHECK(r.ok()); CHECK(r.remaining() == 0);
    r.u8(); CHECK(!r.ok());
}

TEST(bytes_little_endian_layout) {
    ByteWriter w; w.u32(0x01020304);
    CHECK(w.data()[0] == 4 && w.data()[3] == 1);
}

TEST(hello_welcome_roundtrip) {
    HelloMsg h; h.nick = "LBN"; h.skin = 106;
    auto b = encode(h);
    HelloMsg h2;
    CHECK(decode(b.data(), b.size(), h2));
    CHECK(h2.magic == LBN_PROTOCOL_MAGIC && h2.version == LBN_PROTOCOL_VERSION && h2.nick == "LBN" && h2.skin == 106);
    CHECK(h2.modVersion == LBN_MOD_VERSION);

    WelcomeMsg w; w.yourId = 3; w.serverName = "Servidor";
    auto wb = encode(w);
    WelcomeMsg w2;
    CHECK(decode(wb.data(), wb.size(), w2));
    CHECK(w2.yourId == 3 && w2.maxPlayers == LBN_MAX_PLAYERS && w2.serverName == "Servidor");
    // wrong type must fail
    CHECK(!decode(wb.data(), wb.size(), h2));
}

TEST(nick_truncated_on_wire) {
    HelloMsg h; h.nick = std::string(100, 'a');
    auto b = encode(h);
    HelloMsg h2; CHECK(decode(b.data(), b.size(), h2));
    CHECK(h2.nick.size() == kMaxNickLen);
}

TEST(state_on_foot_roundtrip) {
    PlayerState s = sampleFoot();
    auto b = encodeStateC2S(s);
    PlayerState o;
    CHECK(decodeStateC2S(b.data(), b.size(), o));
    CHECK(o.seq == 42 && o.timeMs == 123456 && o.flags == 0);
    CHECK(o.pos.x == s.pos.x && o.pos.y == s.pos.y && o.pos.z == s.pos.z);
    CHECK(o.vel.x == 1.5f && o.heading == 1.25f && o.anim == MoveAnim::Sprint);
    CHECK(o.health == 87 && o.armour == 50);
    CHECK(b.size() == 1 + 4 + 4 + 1 + 12 + 12 + 4 + 1 + 1 + 1 + 1 + 1);
}

TEST(state_vehicle_roundtrip_s2c) {
    PlayerState s = sampleFoot();
    s.flags = PlayerState::InVehicle; s.seat = 0;
    s.veh.key = 77; s.veh.model = 560; s.veh.pos = {10, 20, 30}; s.veh.vel = {5, 0, 0};
    s.veh.rot = normalize(Quat{0.1f, 0.2f, 0.3f, 0.9f}); s.veh.color1 = 3; s.veh.color2 = 7; s.veh.health = 755.f;
    auto b = encodeStateS2C(4, s);
    uint8_t id = 0; PlayerState o;
    CHECK(decodeStateS2C(b.data(), b.size(), id, o));
    CHECK(id == 4 && o.inVehicle() && !o.inRemoteVehicle());
    CHECK(o.veh.key == 77 && o.veh.model == 560 && o.veh.color1 == 3 && o.veh.color2 == 7);
    CHECK_NEAR(o.veh.health, 755.0, 0.001);
    CHECK_NEAR(o.veh.rot.x, s.veh.rot.x, 1e-3); CHECK_NEAR(o.veh.rot.w, s.veh.rot.w, 1e-3);
    CHECK(o.veh.pos.z == 30.f);
}

TEST(state_remote_vehicle_roundtrip) {
    PlayerState s = sampleFoot();
    s.flags = PlayerState::InVehicle | PlayerState::RemoteVehicle; s.seat = 2; s.vehicleOwnerId = 1;
    auto b = encodeStateC2S(s);
    PlayerState o;
    CHECK(decodeStateC2S(b.data(), b.size(), o));
    CHECK(o.inRemoteVehicle() && o.seat == 2 && o.vehicleOwnerId == 1);
}

TEST(truncated_messages_rejected) {
    PlayerState s = sampleFoot();
    s.flags = PlayerState::InVehicle; s.veh.model = 411;
    auto b = encodeStateC2S(s);
    for (size_t n = 0; n < b.size(); ++n) {
        PlayerState o;
        CHECK(!decodeStateC2S(b.data(), n, o));
    }
}

TEST(fuzz_decoders_do_not_crash) {
    std::srand(1234);
    for (int i = 0; i < 20000; ++i) {
        uint8_t buf[96];
        size_t n = size_t(std::rand() % 96);
        for (size_t k = 0; k < n; ++k) buf[k] = uint8_t(std::rand());
        if (n) buf[0] = uint8_t(1 + std::rand() % 10);
        HelloMsg h; WelcomeMsg w; RejectMsg r; PlayerJoinMsg j; PlayerLeaveMsg l; PlayerState s; ChatMsg c; PingsMsg p;
        std::string t; uint8_t id;
        decode(buf, n, h); decode(buf, n, w); decode(buf, n, r); decode(buf, n, j); decode(buf, n, l);
        decodeStateC2S(buf, n, s); decodeStateS2C(buf, n, id, s); decodeChatC2S(buf, n, t); decodeChatS2C(buf, n, c);
        decode(buf, n, p);
    }
    CHECK(true);
}

TEST(chat_and_pings_roundtrip) {
    auto b = encodeChatS2C(2, "oi galera");
    ChatMsg c; CHECK(decodeChatS2C(b.data(), b.size(), c)); CHECK(c.from == 2 && c.text == "oi galera");
    auto cb = encodeChatC2S(std::string(500, 'x'));
    std::string t; CHECK(decodeChatC2S(cb.data(), cb.size(), t)); CHECK(t.size() == kMaxChatLen);
    PingsMsg pm; pm.entries = {{0, 12}, {3, 250}};
    auto pb = encode(pm);
    PingsMsg p2; CHECK(decode(pb.data(), pb.size(), p2));
    CHECK(p2.entries.size() == 2 && p2.entries[1].id == 3 && p2.entries[1].pingMs == 250);
}

TEST(sanity_checks) {
    PlayerState s = sampleFoot();
    CHECK(isSaneState(s));
    PlayerState bad = s; bad.pos.x = std::numeric_limits<float>::quiet_NaN(); CHECK(!isSaneState(bad));
    bad = s; bad.pos.x = 1e9f; CHECK(!isSaneState(bad));
    bad = s; bad.flags = PlayerState::InVehicle; bad.veh.model = 12; CHECK(!isSaneState(bad));
    bad = s; bad.flags = PlayerState::InVehicle | PlayerState::RemoteVehicle; bad.vehicleOwnerId = 9; CHECK(!isSaneState(bad));
    CHECK(isValidPedSkin(106)); CHECK(!isValidPedSkin(0)); CHECK(!isValidPedSkin(270)); CHECK(!isValidPedSkin(400));
    CHECK(isSpawnableVehicleModel(411)); CHECK(!isSpawnableVehicleModel(537)); CHECK(!isSpawnableVehicleModel(399));
}

static bool parseVersionOk(const char* v) { int a, b, c; return std::sscanf(v, "%d.%d.%d", &a, &b, &c) == 3; }

TEST(hello_mod_version_roundtrip_and_message) {
    HelloMsg h; h.nick = "Amigo"; h.modVersion = "0.0.9";
    auto b = encode(h);
    HelloMsg h2;
    CHECK(decode(b.data(), b.size(), h2));
    CHECK(h2.modVersion == "0.0.9");
    std::string msg = modVersionMismatchPtBr("0.1.0", "0.0.9");
    CHECK(msg.find("host v0.1.0") != std::string::npos && msg.find("voce v0.0.9") != std::string::npos);
    CHECK(msg.find("LbnCoopSA-Iniciar.exe") != std::string::npos);
    CHECK(msg.size() <= 128); // fits RejectMsg detail
    CHECK(modVersionMismatchPtBr("0.1.0", "").size() <= 128);
    CHECK(parseVersionOk(LBN_MOD_VERSION));
}

TEST(state_v3_weapon_wanted_roundtrip) {
    PlayerState s = sampleFoot(); s.weapon = 31; s.wanted = 4;
    auto b = encodeStateC2S(s);
    PlayerState o; CHECK(decodeStateC2S(b.data(), b.size(), o));
    CHECK(o.weapon == 31 && o.wanted == 4);
    s.wanted = 7; CHECK(!isSaneState(s));
}

TEST(weapon_table) {
    CHECK(isValidWeapon(0) && isValidWeapon(24) && isValidWeapon(46));
    CHECK(!isValidWeapon(19) && !isValidWeapon(20) && !isValidWeapon(21) && !isValidWeapon(47) && !isValidWeapon(255));
    CHECK(weaponModel(0) == -1 && weaponModel(22) == 346 && weaponModel(24) == 348 && weaponModel(31) == 356);
    CHECK(weaponModel(32) == 372 && weaponModel(38) == 362 && weaponModel(46) == 371 && weaponModel(9) == 341);
    for (int w = 1; w <= 46; ++w) if (isValidWeapon(uint8_t(w))) CHECK(weaponModel(uint8_t(w)) >= 321 && weaponModel(uint8_t(w)) <= 372);
    CHECK(std::string(weaponNamePtBr(30)) == "ak-47");
}

TEST(damage_world_session_roundtrip) {
    DamageMsg d; d.peer = 2; d.amountX10 = 465; d.weapon = 24;
    auto b = encodeDamageC2S(d); DamageMsg o;
    CHECK(decodeDamageC2S(b.data(), b.size(), o) && o.peer == 2 && o.amountX10 == 465 && o.weapon == 24);
    CHECK(!decodeDamageS2C(b.data(), b.size(), o)); // type is checked
    auto b2 = encodeDamageS2C(d); CHECK(decodeDamageS2C(b2.data(), b2.size(), o));
    CHECK(!decodeDamageC2S(b.data(), b.size() - 1, o)); // truncated

    WorldState w; w.hour = 23; w.minute = 59; w.weather = 8; w.wanted = 3;
    w.flags = WorldState::PvP | WorldState::SyncClock; w.carDensityX100 = 50; w.pedDensityX100 = 150;
    auto wb = encodeWorldC2S(w); WorldState wo;
    CHECK(decodeWorldC2S(wb.data(), wb.size(), wo));
    CHECK(wo.hour == 23 && wo.minute == 59 && wo.weather == 8 && wo.wanted == 3 && wo.flags == w.flags);
    CHECK(wo.carDensityX100 == 50 && wo.pedDensityX100 == 150);
    WorldS2CMsg m; m.authorityId = 0; m.w = w;
    auto mb = encode(m); WorldS2CMsg mo;
    CHECK(decode(mb.data(), mb.size(), mo) && mo.authorityId == 0 && mo.w.weather == 8);
    w.hour = 24; auto bad = encodeWorldC2S(w); CHECK(!decodeWorldC2S(bad.data(), bad.size(), wo));
    w.hour = 1; w.weather = -1; bad = encodeWorldC2S(w); CHECK(!decodeWorldC2S(bad.data(), bad.size(), wo));
    w.weather = 1; w.wanted = 9; bad = encodeWorldC2S(w); CHECK(!decodeWorldC2S(bad.data(), bad.size(), wo));

    SessionMsg sm; sm.authorityId = 3; auto sb = encode(sm); SessionMsg so;
    CHECK(decode(sb.data(), sb.size(), so) && so.authorityId == 3);
}

TEST(protocol_version_is_3) {
    CHECK(LBN_PROTOCOL_VERSION == 3);
    CHECK(std::string(LBN_MOD_VERSION) == "0.1.2");
}
