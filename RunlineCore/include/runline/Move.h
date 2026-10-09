#pragma once
#include <vector>
#include "runline/World.h"

namespace runline {

/// Player collider: feet position is the bottom-center of the box.
struct Body {
    float halfWidth;
    float height;

    Aabb AabbAt(Vec3 feet) const {
        return Aabb::New(feet - Vec3{halfWidth, 0.0f, halfWidth}, feet + Vec3{halfWidth, height, halfWidth});
    }
    /// How far the box reaches out from its centre line toward horizontal direction `n`.
    float ReachToward(Vec3 n) const { return halfWidth * (std::fabs(n.x) + std::fabs(n.z)); }
};

/// Result of a single-axis move.
struct AxisHit {
    bool blocked = false;
    /// What stopped it (when blocked).
    Vec3 normal{};
};

/// Move `feet` along one axis by `d`, stopping at the first thing in the way.
AxisHit MoveAxis(const World& world, Body body, Vec3& feet, int axis, float d);

/// Move `feet` horizontally by `delta`, sliding along whatever it runs into (up to three
/// contacts: the leftover motion loses its part into each surface). Returns the horizontal
/// normals of what it touched.
std::vector<Vec3> SlideMove(const World& world, Body body, Vec3& feet, Vec3 delta);

} // namespace runline
