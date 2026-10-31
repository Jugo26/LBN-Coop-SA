// Snapshot interpolation for remote players (original code for LBN Coop SA).
#pragma once
#include <cstdint>
#include <deque>
#include "protocol.h"

namespace lbn {

// Estimates the offset between a remote sender clock and the local clock.
// offset = senderTime - localTime for the least-delayed packet seen recently.
class ClockSync {
public:
    explicit ClockSync(size_t window = 64) : window_(window) {}
    void addSample(uint32_t senderMs, uint32_t localRecvMs);
    bool valid() const { return !samples_.empty(); }
    int32_t offset() const { return offset_; }
    // Converts a local time to the sender's clock domain.
    uint32_t toSender(uint32_t localMs) const { return localMs + uint32_t(offset_); }
    void reset() { samples_.clear(); offset_ = 0; }
private:
    size_t window_;
    std::deque<int32_t> samples_;
    int32_t offset_ = 0;
};

struct InterpResult {
    bool valid = false;
    bool extrapolated = false;
    PlayerState state;   // interpolated state (discrete fields from the nearest snapshot)
};

struct InterpConfig {
    uint32_t delayMs = 100;          // render this far in the past
    uint32_t maxExtrapolateMs = 250; // beyond the newest snapshot
    float teleportDistance = 30.f;   // snap instead of interpolate above this jump
    size_t maxSnapshots = 32;
};

class Interpolator {
public:
    explicit Interpolator(InterpConfig cfg = {}) : cfg_(cfg) {}
    // Returns false if the snapshot was rejected (old/duplicate).
    bool push(const PlayerState& s, uint32_t localRecvMs);
    InterpResult sample(uint32_t localNowMs) const;
    // Sample at an explicit sender-domain time (used by tests).
    InterpResult sampleAtSenderTime(uint32_t senderMs) const;
    size_t size() const { return snaps_.size(); }
    const ClockSync& clock() const { return clock_; }
    void reset() { snaps_.clear(); clock_.reset(); }
    const InterpConfig& config() const { return cfg_; }
private:
    InterpConfig cfg_;
    ClockSync clock_;
    std::deque<PlayerState> snaps_; // ordered by timeMs ascending
};

// Hermite interpolation of a position with velocities (m/s), dtSec between samples.
Vec3 hermite(const Vec3& p0, const Vec3& v0, const Vec3& p1, const Vec3& v1, float dtSec, float t);

// Map a GTA ped move state + speed to our anim enum.
MoveAnim moveAnimFromPedState(int moveState, float speedMps);

} // namespace lbn
