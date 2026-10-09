#pragma once
#include <optional>
#include "faith/Vec3.h"

namespace faith {

// Port of faith_move::world. Everything the controller knows about the level goes through
// World: sweep the player's box through it (first contact, with the surface normal) and ask
// whether a box is clear. Those are the two questions an engine's physics answers natively.

struct Aabb {
    Vec3 min, max;

    static Aabb New(Vec3 a, Vec3 b) { return {Vec3::Min(a, b), Vec3::Max(a, b)}; }

    /// Strict overlap (touching faces do not count).
    bool Overlaps(const Aabb& o) const {
        constexpr float E = 1e-4f;
        return min.x < o.max.x - E && max.x > o.min.x + E
            && min.y < o.max.y - E && max.y > o.min.y + E
            && min.z < o.max.z - E && max.z > o.min.z + E;
    }
    Aabb Translated(Vec3 d) const { return {min + d, max + d}; }
    Aabb Union(const Aabb& o) const { return {Vec3::Min(min, o.min), Vec3::Max(max, o.max)}; }
};

/// Where a swept box first touched something.
struct SweepHit {
    float t;      ///< How much of the move was made before the contact (0..1).
    Vec3 normal;  ///< The surface's normal at the contact, facing back at the box.
    Vec3 point;   ///< The contact point, on the surface.
};

/// The level, as the controller sees it.
class World {
public:
    virtual ~World() = default;
    /// Sweep a box (half size `half`, centred at `start`) along `delta` and return the first
    /// solid it runs into before the end. Solids it starts inside (beyond touching) don't
    /// count, like the game's extent traces.
    virtual std::optional<SweepHit> Sweep(Vec3 half, Vec3 start, Vec3 delta) const = 0;
    /// Whether anything solid is inside `region` (touching its faces doesn't count).
    virtual bool Overlaps(const Aabb& region) const = 0;
    bool IsFree(const Aabb& region) const { return !Overlaps(region); }
};

} // namespace faith
