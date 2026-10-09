#pragma once
#include <array>
#include <vector>
#include "runline/World.h"

namespace runline {

using Tri = std::array<Vec3, 3>;

Aabb TriBounds(const Tri& t);

/// Separating-axis test: does the triangle cut into the box (centre `c`, half size `h`)?
/// Akenine-Moller's box / triangle overlap, strict (touching doesn't count).
bool TriBoxOverlap(const Tri& t, Vec3 c, Vec3 h);

/// Triangles at any angle: what a game engine's static geometry is. Sweeps step the box along
/// the path (2 cm) and narrow the first contact down by bisection; overlaps are the
/// separating-axis box / triangle test.
class MeshWorld : public World {
public:
    std::vector<Tri> tris;

    /// Two triangles making the quad `a b c d` (in order round it).
    static std::array<Tri, 2> Quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d) { return {Tri{a, b, c}, Tri{a, c, d}}; }

    std::optional<SweepHit> Sweep(Vec3 half, Vec3 start, Vec3 delta) const override;
    bool Overlaps(const Aabb& region) const override;
};

} // namespace runline
