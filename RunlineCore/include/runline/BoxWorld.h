#pragma once
#include <vector>
#include "runline/World.h"

namespace runline {

/// Axis-aligned boxes, scanned in full (the greybox levels). The grid index of the Rust
/// version comes later, when real maps need it.
class BoxWorld : public World {
public:
    std::vector<Aabb> boxes;

    BoxWorld& Add(Aabb b) { boxes.push_back(b); return *this; }

    std::optional<SweepHit> Sweep(Vec3 half, Vec3 start, Vec3 delta) const override;
    bool Overlaps(const Aabb& region) const override;
};

/// BoxWorld's sweep over an explicit list of boxes, in list order (ties keep the earlier box):
/// the narrowphase FUnrealWorld runs on Unreal's broadphase candidates.
std::optional<SweepHit> SweepBoxes(const std::vector<Aabb>& boxes, Vec3 half, Vec3 start, Vec3 delta);
/// Whether any of `boxes` overlaps `region` strictly (touching faces don't count).
bool OverlapsBoxes(const std::vector<Aabb>& boxes, const Aabb& region);

} // namespace runline
