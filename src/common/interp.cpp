// Snapshot interpolation implementation (original code for LBN Coop SA).
#include "interp.h"
#include <algorithm>

namespace lbn {

static inline int32_t tdiff(uint32_t a, uint32_t b) { return int32_t(a - b); }

void ClockSync::addSample(uint32_t senderMs, uint32_t localRecvMs) {
    int32_t s = tdiff(senderMs, localRecvMs);
    samples_.push_back(s);
    while (samples_.size() > window_) samples_.pop_front();
    offset_ = *std::max_element(samples_.begin(), samples_.end());
}

Vec3 hermite(const Vec3& p0, const Vec3& v0, const Vec3& p1, const Vec3& v1, float dtSec, float t) {
    float t2 = t * t, t3 = t2 * t;
    float h00 = 2 * t3 - 3 * t2 + 1;
    float h10 = t3 - 2 * t2 + t;
    float h01 = -2 * t3 + 3 * t2;
    float h11 = t3 - t2;
    return p0 * h00 + v0 * (h10 * dtSec) + p1 * h01 + v1 * (h11 * dtSec);
}

bool Interpolator::push(const PlayerState& s, uint32_t localRecvMs) {
    if (!snaps_.empty()) {
        const PlayerState& last = snaps_.back();
        if (tdiff(s.timeMs, last.timeMs) <= 0) {
            // Out of order or duplicate; insert only if it fits strictly between.
            auto it = std::find_if(snaps_.begin(), snaps_.end(),
                                   [&](const PlayerState& x) { return tdiff(x.timeMs, s.timeMs) >= 0; });
            if (it == snaps_.end() || it->timeMs == s.timeMs) return false;
            snaps_.insert(it, s);
            clock_.addSample(s.timeMs, localRecvMs);
            return true;
        }
    }
    snaps_.push_back(s);
    while (snaps_.size() > cfg_.maxSnapshots) snaps_.pop_front();
    clock_.addSample(s.timeMs, localRecvMs);
    return true;
}

InterpResult Interpolator::sample(uint32_t localNowMs) const {
    if (snaps_.empty()) return {};
    uint32_t senderNow = clock_.toSender(localNowMs);
    return sampleAtSenderTime(senderNow - cfg_.delayMs);
}

InterpResult Interpolator::sampleAtSenderTime(uint32_t rt) const {
    InterpResult out;
    if (snaps_.empty()) return out;
    out.valid = true;

    const PlayerState& first = snaps_.front();
    const PlayerState& last = snaps_.back();

    if (tdiff(rt, first.timeMs) <= 0) { out.state = first; return out; }

    if (tdiff(rt, last.timeMs) >= 0) {
        // Extrapolate from the newest snapshot (bounded).
        int32_t ahead = tdiff(rt, last.timeMs);
        if (ahead > int32_t(cfg_.maxExtrapolateMs)) ahead = int32_t(cfg_.maxExtrapolateMs);
        float dt = ahead / 1000.f;
        out.state = last;
        out.extrapolated = ahead > 0;
        out.state.pos = last.pos + last.vel * dt;
        if (last.inVehicle() && !last.inRemoteVehicle())
            out.state.veh.pos = last.veh.pos + last.veh.vel * dt;
        return out;
    }

    // Find bracketing pair a <= rt < b.
    size_t i = 0;
    while (i + 1 < snaps_.size() && tdiff(snaps_[i + 1].timeMs, rt) <= 0) ++i;
    const PlayerState& a = snaps_[i];
    const PlayerState& b = snaps_[i + 1];
    int32_t span = tdiff(b.timeMs, a.timeMs);
    float t = span > 0 ? float(tdiff(rt, a.timeMs)) / float(span) : 1.f;
    t = std::clamp(t, 0.f, 1.f);
    float dtSec = span / 1000.f;

    // Discrete fields come from the nearest snapshot.
    out.state = (t < 0.5f) ? a : b;
    out.state.timeMs = rt;

    // Mode change (enter/exit vehicle, change vehicle) => no blending.
    bool sameMode = (a.flags == b.flags) && (a.seat == b.seat) &&
                    (!a.inVehicle() || a.inRemoteVehicle() ||
                     (a.veh.key == b.veh.key && a.veh.model == b.veh.model));
    if (!sameMode) { out.state = (t < 1.f) ? a : b; return out; }

    if ((b.pos - a.pos).length() > cfg_.teleportDistance) {
        out.state = (t < 1.f) ? a : b;
        return out;
    }

    out.state.pos = hermite(a.pos, a.vel, b.pos, b.vel, dtSec, t);
    out.state.vel = lerp(a.vel, b.vel, t);
    out.state.heading = lerpAngle(a.heading, b.heading, t);

    if (a.inVehicle() && !a.inRemoteVehicle()) {
        if ((b.veh.pos - a.veh.pos).length() > cfg_.teleportDistance) {
            out.state.veh = (t < 1.f) ? a.veh : b.veh;
        } else {
            out.state.veh.pos = hermite(a.veh.pos, a.veh.vel, b.veh.pos, b.veh.vel, dtSec, t);
            out.state.veh.vel = lerp(a.veh.vel, b.veh.vel, t);
            out.state.veh.rot = slerp(a.veh.rot, b.veh.rot, t);
            out.state.veh.health = a.veh.health + (b.veh.health - a.veh.health) * t;
        }
    }
    return out;
}

MoveAnim moveAnimFromPedState(int moveState, float speedMps) {
    // GTA SA eMoveState: 0 none, 1 still, 2 turn L, 3 turn R, 4 walk, 5 jog, 6 run, 7 sprint
    switch (moveState) {
    case 4: return MoveAnim::Walk;
    case 5: case 6: return MoveAnim::Run;
    case 7: return MoveAnim::Sprint;
    default: break;
    }
    // Fallback from horizontal speed (e.g. while the state is "none" during jumps).
    if (speedMps > 6.5f) return MoveAnim::Sprint;
    if (speedMps > 3.0f) return MoveAnim::Run;
    if (speedMps > 0.6f) return MoveAnim::Walk;
    return MoveAnim::Idle;
}

} // namespace lbn
