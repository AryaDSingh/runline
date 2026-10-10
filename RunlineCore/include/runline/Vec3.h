#pragma once
#include <cmath>

namespace runline {

// Scalar min/max/clamp with Rust's f32 tie-breaking: on equal values (including -0 and +0)
// the second argument wins, as glam's Vec3::min/max spell out. The scalars golden test checks
// this against Rust itself.
inline float FMin(float a, float b) { return a < b ? a : b; }
inline float FMax(float a, float b) { return a > b ? a : b; }
/// Rust's f32::rem_euclid.
inline float RemEuclid(float a, float b) {
    const float r = std::fmod(a, b);
    return r < 0.0f ? r + std::fabs(b) : r;
}
/// Rust's f32::clamp: below `lo` gives `lo`, above `hi` gives `hi`.
inline float FClamp(float x, float lo, float hi) {
    if (x < lo) x = lo;
    if (x > hi) x = hi;
    return x;
}

// The subset of glam::Vec3 that faith_move uses, in glam 0.32's exact operation order.
// Metres, Y up.
struct Vec3 {
    float x = 0, y = 0, z = 0;

    static constexpr Vec3 Zero() { return {0, 0, 0}; }
    static constexpr Vec3 UnitX() { return {1, 0, 0}; }
    static constexpr Vec3 UnitY() { return {0, 1, 0}; }
    static constexpr Vec3 UnitZ() { return {0, 0, 1}; }
    static constexpr Vec3 Splat(float v) { return {v, v, v}; }

    float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
    float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }

    friend Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    friend Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    friend Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
    friend Vec3 operator/(Vec3 a, float s) { return {a.x / s, a.y / s, a.z / s}; }
    friend Vec3 operator-(Vec3 a) { return {-a.x, -a.y, -a.z}; }
    friend bool operator==(Vec3 a, Vec3 b) = default;
    Vec3& operator+=(Vec3 b) { return *this = *this + b; }
    Vec3& operator-=(Vec3 b) { return *this = *this - b; }
    Vec3& operator*=(float s) { return *this = *this * s; }

    /// glam's min/max: per component, the second argument wins ties.
    static Vec3 Min(Vec3 a, Vec3 b) { return {FMin(a.x, b.x), FMin(a.y, b.y), FMin(a.z, b.z)}; }
    static Vec3 Max(Vec3 a, Vec3 b) { return {FMax(a.x, b.x), FMax(a.y, b.y), FMax(a.z, b.z)}; }
    /// glam's clamp: self.max(lo).min(hi).
    Vec3 Clamp(Vec3 lo, Vec3 hi) const { return Min(Max(*this, lo), hi); }

    float Dot(Vec3 b) const { return (x * b.x) + (y * b.y) + (z * b.z); }
    Vec3 Cross(Vec3 b) const { return {y * b.z - b.y * z, z * b.x - b.z * x, x * b.y - b.x * y}; }
    float LengthSquared() const { return Dot(*this); }
    float Length() const { return std::sqrt(Dot(*this)); }
    float LengthRecip() const { return 1.0f / Length(); }
    Vec3 Normalize() const { return *this * LengthRecip(); }
    /// Unit length, or zero when there's no direction (glam's normalize_or_zero).
    Vec3 NormalizeOrZero() const {
        const float rcp = LengthRecip();
        return (std::isfinite(rcp) && rcp > 0.0f) ? *this * rcp : Zero();
    }
};

} // namespace runline
