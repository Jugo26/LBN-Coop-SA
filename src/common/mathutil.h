// Small math helpers (vectors, quaternions, angles). Original code for LBN Coop SA.
#pragma once
#include <cmath>

namespace lbn {

constexpr float kPi = 3.14159265358979323846f;

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float X, float Y, float Z) : x(X), y(Y), z(Z) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    float length() const { return std::sqrt(dot(*this)); }
};

inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// Wrap angle to (-pi, pi].
inline float wrapAngle(float a) {
    while (a > kPi) a -= 2 * kPi;
    while (a <= -kPi) a += 2 * kPi;
    return a;
}

// Interpolate angles along the shortest arc.
inline float lerpAngle(float a, float b, float t) {
    return wrapAngle(a + wrapAngle(b - a) * t);
}

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
    Quat() = default;
    Quat(float X, float Y, float Z, float W) : x(X), y(Y), z(Z), w(W) {}
    float dot(const Quat& o) const { return x * o.x + y * o.y + z * o.z + w * o.w; }
};

inline Quat normalize(const Quat& q) {
    float n = std::sqrt(q.dot(q));
    if (n < 1e-8f) return Quat();
    return {q.x / n, q.y / n, q.z / n, q.w / n};
}

// Normalized lerp / slerp hybrid: slerp when angle is large, nlerp when small.
inline Quat slerp(Quat a, Quat b, float t) {
    float d = a.dot(b);
    if (d < 0) { b = {-b.x, -b.y, -b.z, -b.w}; d = -d; }
    if (d > 0.9995f) {
        return normalize({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                          a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t});
    }
    float th = std::acos(d);
    float s = std::sin(th);
    float wa = std::sin((1 - t) * th) / s;
    float wb = std::sin(t * th) / s;
    return {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
}

// GTA SA CMatrix stores three axis vectors: right (X), "up" which is actually
// forward (Y) and "at" which is actually up (Z). These helpers convert between
// that basis (columns of a rotation matrix) and a quaternion.
inline Quat quatFromAxes(const Vec3& right, const Vec3& fwd, const Vec3& up) {
    // Rotation matrix R with columns right, fwd, up: R = [r f u]
    float m00 = right.x, m01 = fwd.x, m02 = up.x;
    float m10 = right.y, m11 = fwd.y, m12 = up.y;
    float m20 = right.z, m21 = fwd.z, m22 = up.z;
    float tr = m00 + m11 + m22;
    Quat q;
    if (tr > 0) {
        float s = std::sqrt(tr + 1.0f) * 2;
        q.w = 0.25f * s;
        q.x = (m21 - m12) / s;
        q.y = (m02 - m20) / s;
        q.z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        float s = std::sqrt(1.0f + m00 - m11 - m22) * 2;
        q.w = (m21 - m12) / s;
        q.x = 0.25f * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
    } else if (m11 > m22) {
        float s = std::sqrt(1.0f + m11 - m00 - m22) * 2;
        q.w = (m02 - m20) / s;
        q.x = (m01 + m10) / s;
        q.y = 0.25f * s;
        q.z = (m12 + m21) / s;
    } else {
        float s = std::sqrt(1.0f + m22 - m00 - m11) * 2;
        q.w = (m10 - m01) / s;
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25f * s;
    }
    return normalize(q);
}

inline void axesFromQuat(const Quat& qin, Vec3& right, Vec3& fwd, Vec3& up) {
    Quat q = normalize(qin);
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    right = {1 - 2 * (yy + zz), 2 * (xy + wz), 2 * (xz - wy)};
    fwd   = {2 * (xy - wz), 1 - 2 * (xx + zz), 2 * (yz + wx)};
    up    = {2 * (xz + wy), 2 * (yz - wx), 1 - 2 * (xx + yy)};
}

// GTA heading (radians, 0 = facing +Y, counter-clockwise positive) <-> forward vector.
inline Vec3 headingToForward(float h) { return {-std::sin(h), std::cos(h), 0}; }
inline float forwardToHeading(const Vec3& f) { return std::atan2(-f.x, f.y); }

} // namespace lbn
