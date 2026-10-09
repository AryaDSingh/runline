#pragma once
#include <vector>
#include "faith/World.h"

namespace faith {

/// Axis-aligned boxes, scanned in full (the greybox levels). The grid index of the Rust
/// version comes later, when real maps need it.
class BoxWorld : public World {
public:
    std::vector<Aabb> boxes;

    BoxWorld& Add(Aabb b) { boxes.push_back(b); return *this; }

    std::optional<SweepHit> Sweep(Vec3 half, Vec3 start, Vec3 delta) const override;
    bool Overlaps(const Aabb& region) const override;
};

} // namespace faith
