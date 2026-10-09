#pragma once
#include <algorithm>

namespace faith {

// The subset of glam::Vec3 that faith_move uses. Metres, Y up.
struct Vec3 {
    float x = 0, y = 0, z = 0;

    float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
    float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }

    friend Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    friend Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    friend Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
    friend bool operator==(Vec3 a, Vec3 b) = default;

    static Vec3 Splat(float v) { return {v, v, v}; }
    static Vec3 Min(Vec3 a, Vec3 b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
    static Vec3 Max(Vec3 a, Vec3 b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }
    Vec3 Clamp(Vec3 lo, Vec3 hi) const { return Max(lo, Min(hi, *this)); }
};

} // namespace faith
