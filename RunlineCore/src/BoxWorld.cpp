#include "runline/BoxWorld.h"
#include <cmath>
#include <limits>

namespace runline {
namespace {

/// How far a box may be inside something and still only be touching it.
constexpr float TOUCH = 1e-4f;

struct AxisHit { float t; Vec3 normal; };

/// Sweep a box against one axis-aligned box: (fraction, normal) of the first contact. The
/// usual slab test on the box grown by `half`, with the overlap judged strictly (TOUCH): a
/// box only touching sideways is passed, one touching ahead is hit at once.
std::optional<AxisHit> SweepAabb(const Aabb& b, Vec3 half, Vec3 c, Vec3 delta) {
    const Vec3 lo = b.min - half;
    const Vec3 hi = b.max + half;
    float enterS = -std::numeric_limits<float>::infinity();
    float exitS = std::numeric_limits<float>::infinity();
    float enter = 0.0f;
    Vec3 normal{};
    for (int a = 0; a < 3; ++a) {
        const float d = delta[a];
        if (std::fabs(d) < 1e-9f) {
            if (c[a] <= lo[a] + TOUCH || c[a] >= hi[a] - TOUCH) return std::nullopt;
            continue;
        }
        // Strict slab (shrunk by TOUCH) decides whether the paths overlap; the true faces
        // say when the contact is.
        const float sa = (lo[a] + TOUCH - c[a]) / d;
        const float sb = (hi[a] - TOUCH - c[a]) / d;
        const float se = sa < sb ? sa : sb;
        const float sx = sa < sb ? sb : sa;
        const float face = d > 0.0f ? (lo[a] - c[a]) / d : (hi[a] - c[a]) / d;
        if (se > enterS) {
            enterS = se;
            enter = face;
            normal = Vec3{};
            normal[a] = d > 0.0f ? -1.0f : 1.0f;   // -d.signum()
        }
        exitS = std::min(exitS, sx);
    }
    if (normal == Vec3{} || enterS >= exitS || enterS >= 1.0f || exitS <= 0.0f || enterS <= 0.0f)
        return std::nullopt;
    const float t = std::max(enter, 0.0f);
    if (t < 1.0f) return AxisHit{t, normal};
    return std::nullopt;
}

Vec3 ContactOnBox(const Aabb& b, Vec3 half, Vec3 c, Vec3 delta, float t, Vec3 n) {
    const Vec3 at = c + delta * t;
    Vec3 p = at.Clamp(b.min, b.max);
    for (int a = 0; a < 3; ++a)
        if (n[a] != 0.0f) p[a] = at[a] - n[a] * half[a];
    return p;
}

/// The first contact of a box swept through some boxes.
std::optional<SweepHit> SweepBoxes(const std::vector<Aabb>& boxes, Vec3 half, Vec3 start, Vec3 delta) {
    const Aabb from = Aabb::New(start - half, start + half);
    std::optional<SweepHit> best;
    const Aabb* bestBox = nullptr;
    for (const Aabb& b : boxes) {
        if (b.Overlaps(from)) continue;
        if (auto h = SweepAabb(b, half, start, delta)) {
            if (!best || h->t < best->t) { best = SweepHit{h->t, h->normal, {}}; bestBox = &b; }
        }
    }
    if (best) best->point = ContactOnBox(*bestBox, half, start, delta, best->t, best->normal);
    return best;
}

} // namespace

std::optional<SweepHit> BoxWorld::Sweep(Vec3 half, Vec3 start, Vec3 delta) const {
    const Aabb a = Aabb::New(start - half, start + half);
    const Aabb region = a.Union(a.Translated(delta));
    // Candidates touching the swept region (with a hair, for touching faces).
    const Aabb grown = Aabb::New(region.min - Vec3::Splat(0.01f), region.max + Vec3::Splat(0.01f));
    std::vector<Aabb> candidates;
    for (const Aabb& b : boxes)
        if (b.Overlaps(grown)) candidates.push_back(b);
    return SweepBoxes(candidates, half, start, delta);
}

bool BoxWorld::Overlaps(const Aabb& region) const {
    for (const Aabb& b : boxes)
        if (b.Overlaps(region)) return true;
    return false;
}

} // namespace runline
