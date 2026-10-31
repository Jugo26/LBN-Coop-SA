#include "testfw.h"
#include "../src/common/interp.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace lbn;

static PlayerState at(uint32_t t, Vec3 p, Vec3 v = {}, float h = 0) {
    PlayerState s; s.timeMs = t; s.pos = p; s.vel = v; s.heading = h; return s;
}

TEST(interp_linear_midpoint) {
    Interpolator ip;
    // Constant velocity 10 m/s along X, snapshots every 100 ms.
    for (int i = 0; i <= 5; ++i) ip.push(at(1000 + i * 100, {float(i), 0, 0}, {10, 0, 0}), 5000 + i * 100);
    auto r = ip.sampleAtSenderTime(1250);
    CHECK(r.valid && !r.extrapolated);
    CHECK_NEAR(r.state.pos.x, 2.5, 1e-4); // hermite with consistent velocities is exact for linear motion
}

TEST(interp_hermite_endpoints) {
    Vec3 a{0, 0, 0}, b{1, 2, 3};
    Vec3 p0 = hermite(a, {1, 0, 0}, b, {0, 1, 0}, 0.1f, 0.f);
    Vec3 p1 = hermite(a, {1, 0, 0}, b, {0, 1, 0}, 0.1f, 1.f);
    CHECK_NEAR(p0.x, 0, 1e-6); CHECK_NEAR(p1.x, 1, 1e-6); CHECK_NEAR(p1.z, 3, 1e-6);
}

TEST(interp_extrapolation_is_bounded) {
    InterpConfig cfg; cfg.maxExtrapolateMs = 250;
    Interpolator ip(cfg);
    ip.push(at(0, {0, 0, 0}, {10, 0, 0}), 0);
    ip.push(at(100, {1, 0, 0}, {10, 0, 0}), 100);
    auto r = ip.sampleAtSenderTime(200);
    CHECK(r.extrapolated); CHECK_NEAR(r.state.pos.x, 2.0, 1e-4);
    r = ip.sampleAtSenderTime(5000);
    CHECK_NEAR(r.state.pos.x, 1.0 + 10 * 0.25, 1e-4);
}

TEST(interp_before_first_returns_first) {
    Interpolator ip;
    ip.push(at(1000, {5, 5, 5}), 0);
    ip.push(at(1100, {6, 5, 5}), 100);
    auto r = ip.sampleAtSenderTime(900);
    CHECK_NEAR(r.state.pos.x, 5, 1e-6);
}

TEST(interp_heading_shortest_arc) {
    Interpolator ip;
    ip.push(at(0, {}, {}, 3.0f), 0);
    ip.push(at(100, {}, {}, -3.0f), 100);
    auto r = ip.sampleAtSenderTime(50);
    // halfway between 3.0 and -3.0 the short way is near +-pi, not 0
    CHECK(std::fabs(r.state.heading) > 3.1f);
}

TEST(interp_teleport_snaps) {
    Interpolator ip;
    ip.push(at(0, {0, 0, 0}), 0);
    ip.push(at(100, {500, 0, 0}), 100);
    auto r = ip.sampleAtSenderTime(50);
    CHECK(r.state.pos.x == 0.f || r.state.pos.x == 500.f);
}

TEST(interp_vehicle_change_no_blend) {
    Interpolator ip;
    PlayerState a = at(0, {0, 0, 0}); a.flags = PlayerState::InVehicle; a.veh.key = 1; a.veh.model = 411; a.veh.pos = {0, 0, 0};
    PlayerState b = at(100, {2, 0, 0}); b.flags = PlayerState::InVehicle; b.veh.key = 2; b.veh.model = 560; b.veh.pos = {2, 0, 0};
    ip.push(a, 0); ip.push(b, 100);
    auto r = ip.sampleAtSenderTime(50);
    CHECK(r.state.veh.key == 1 && r.state.veh.pos.x == 0.f);
}

TEST(interp_vehicle_rotation_slerp) {
    Interpolator ip;
    PlayerState a = at(0, {}); a.flags = PlayerState::InVehicle; a.veh.model = 411; a.veh.key = 5;
    a.veh.rot = Quat(0, 0, 0, 1);
    PlayerState b = a; b.timeMs = 100;
    float h = kPi / 2; // 90 degrees around Z
    b.veh.rot = Quat(0, 0, std::sin(h / 2), std::cos(h / 2));
    ip.push(a, 0); ip.push(b, 100);
    auto r = ip.sampleAtSenderTime(50);
    Vec3 rt, fw, up; axesFromQuat(r.state.veh.rot, rt, fw, up);
    CHECK_NEAR(forwardToHeading(fw), kPi / 4, 1e-3);
}

TEST(interp_out_of_order_and_duplicates) {
    Interpolator ip;
    CHECK(ip.push(at(100, {1, 0, 0}), 0));
    CHECK(ip.push(at(300, {3, 0, 0}), 0));
    CHECK(ip.push(at(200, {2, 0, 0}), 0));  // late but fits
    CHECK(!ip.push(at(200, {9, 0, 0}), 0)); // duplicate
    CHECK(ip.size() == 3);
    auto r = ip.sampleAtSenderTime(250);
    CHECK_NEAR(r.state.pos.x, 2.5, 0.3);
}

TEST(interp_buffer_bounded) {
    InterpConfig cfg; cfg.maxSnapshots = 8;
    Interpolator ip(cfg);
    for (int i = 0; i < 100; ++i) ip.push(at(i * 50, {float(i), 0, 0}), i * 50);
    CHECK(ip.size() == 8);
}

TEST(clock_sync_uses_least_delayed_sample) {
    ClockSync cs;
    // sender clock is 10000 ahead; latencies 80, 30, 50 ms
    cs.addSample(10000 + 0, 0 + 80);
    cs.addSample(10000 + 100, 100 + 30);
    cs.addSample(10000 + 200, 200 + 50);
    CHECK(cs.offset() == 10000 - 30);
}

TEST(clock_sync_handles_wraparound) {
    ClockSync cs;
    cs.addSample(5, 0xFFFFFFF0u); // sender just wrapped
    CHECK(cs.toSender(0xFFFFFFF0u) == 5);
}

TEST(interp_sample_uses_delay) {
    InterpConfig cfg; cfg.delayMs = 100;
    Interpolator ip(cfg);
    // sender clock == local clock + 0 latency
    for (int i = 0; i <= 10; ++i) ip.push(at(i * 50, {float(i), 0, 0}, {20, 0, 0}), i * 50);
    auto r = ip.sample(500);  // render time 400 => pos 8
    CHECK_NEAR(r.state.pos.x, 8.0, 1e-3);
}

TEST(quat_axes_roundtrip) {
    for (float h = -3.f; h < 3.f; h += 0.37f) {
        Vec3 fwd = headingToForward(h);
        Vec3 up{0, 0, 1};
        Vec3 right = cross(fwd, up);
        Quat q = quatFromAxes(right, fwd, up);
        Vec3 r2, f2, u2; axesFromQuat(q, r2, f2, u2);
        CHECK_NEAR(f2.x, fwd.x, 1e-4); CHECK_NEAR(f2.y, fwd.y, 1e-4); CHECK_NEAR(u2.z, 1, 1e-4);
        CHECK_NEAR(r2.x, right.x, 1e-4);
        CHECK_NEAR(forwardToHeading(f2), h, 1e-4);
    }
}

TEST(move_anim_mapping) {
    CHECK(moveAnimFromPedState(4, 0) == MoveAnim::Walk);
    CHECK(moveAnimFromPedState(6, 0) == MoveAnim::Run);
    CHECK(moveAnimFromPedState(7, 0) == MoveAnim::Sprint);
    CHECK(moveAnimFromPedState(1, 0) == MoveAnim::Idle);
    CHECK(moveAnimFromPedState(0, 8) == MoveAnim::Sprint);
}

// v0.1.1 defaults (SendRate=30, InterpDelayMs=120) under a Radmin-like link: 18 ms average latency with
// jitter, 3% packet loss, and a sender whose frames are not perfectly regular. Rendering at 60 fps must
// stay close to the true path and never jump/snap between frames.
namespace {
struct SimResult { double maxErr = 0, maxStep = 0, avgErr = 0; int extrapolated = 0, frames = 0; };
SimResult simulateLink(int sendHz, uint32_t delayMs, float speed, uint32_t seed) {
    InterpConfig cfg; cfg.delayMs = delayMs;
    Interpolator ip(cfg);
    uint32_t rng = seed;
    auto rnd = [&]() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0; };
    auto truth = [&](double t) { // walk/sprint around a 15 m circle
        double w = speed / 15.0;
        return Vec3{float(15 * std::cos(w * t)), float(15 * std::sin(w * t)), 13.f};
    };
    auto truthVel = [&](double t) {
        double w = speed / 15.0;
        return Vec3{float(-15 * w * std::sin(w * t)), float(15 * w * std::cos(w * t)), 0.f};
    };
    struct InFlight { uint32_t arrive; PlayerState s; };
    std::vector<InFlight> net;
    const uint32_t senderBase = 500000, localBase = 9000;  // unrelated clocks
    double nextSend = 0;
    SimResult r; double sumErr = 0; Vec3 prev{}; bool havePrev = false;
    for (uint32_t ms = 0; ms < 20000; ++ms) {
        if (ms >= nextSend) {
            double interval = 1000.0 / sendHz;
            nextSend += interval * (0.85 + 0.3 * rnd()); // irregular game frames
            double t = ms / 1000.0;
            PlayerState s; s.timeMs = senderBase + ms; s.pos = truth(t); s.vel = truthVel(t);
            if (rnd() > 0.03) net.push_back({ms + 12 + uint32_t(rnd() * 14) + (rnd() < 0.05 ? 40u : 0u), s});
        }
        for (size_t i = 0; i < net.size();) {
            if (net[i].arrive <= ms) { ip.push(net[i].s, localBase + ms); net.erase(net.begin() + long(i)); }
            else ++i;
        }
        if (ms % 16 == 0 && ms > 1500) {
            InterpResult res = ip.sample(localBase + ms);
            if (!res.valid) continue;
            // Compare with where the sender was at the time being rendered.
            double renderT = (int32_t(res.state.timeMs - senderBase)) / 1000.0;
            if (res.extrapolated) { r.extrapolated++; renderT = (ip.clock().toSender(localBase + ms) - delayMs - senderBase) / 1000.0; }
            double err = (res.state.pos - truth(renderT)).length();
            r.maxErr = std::max(r.maxErr, err); sumErr += err; r.frames++;
            if (havePrev) r.maxStep = std::max(r.maxStep, double((res.state.pos - prev).length()));
            prev = res.state.pos; havePrev = true;
        }
    }
    r.avgErr = r.frames ? sumErr / r.frames : 0;
    return r;
}
}

TEST(interp_defaults_30hz_120ms_no_snapping) {
    for (float speed : {1.6f, 4.5f, 7.0f}) {           // walk, run, sprint (m/s)
        for (uint32_t seed : {1u, 7u, 42u}) {
            SimResult r = simulateLink(30, 120, speed, seed);
            // Per 16 ms frame a sprinting ped moves ~0.11 m; anything well above that would be a visible snap.
            double frameMove = speed * 0.016;
            CHECK(r.maxStep < frameMove * 2.5 + 0.02);
            CHECK(r.maxErr < 0.25);
            CHECK(r.extrapolated < r.frames / 50);          // < 2% of frames outrun the buffer
            if (speed == 7.0f && seed == 1u)
                std::printf("    30Hz/120ms sprint: erro medio %.3f m, max %.3f m, maior passo %.3f m, extrapolados %d/%d\n",
                            r.avgErr, r.maxErr, r.maxStep, r.extrapolated, r.frames);
        }
    }
    // Old defaults for comparison (logged only): 20 Hz / 100 ms extrapolates more often on the same link.
    SimResult old = simulateLink(20, 100, 7.0f, 1);
    std::printf("    20Hz/100ms sprint: erro medio %.3f m, max %.3f m, maior passo %.3f m, extrapolados %d/%d\n",
                old.avgErr, old.maxErr, old.maxStep, old.extrapolated, old.frames);
}
