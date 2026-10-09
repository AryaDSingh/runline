#include "runline/Move.h"

namespace runline {

AxisHit MoveAxis(const World& world, Body body, Vec3& feet, int axis, float d) {
    if (d == 0.0f) return {};
    const Aabb me = body.AabbAt(feet);
    Vec3 delta{};
    delta[axis] = d;
    if (const auto h = world.Sweep(me.Size() * 0.5f, me.Center(), delta)) {
        feet[axis] += d * h->t;
        return {true, h->normal};
    }
    feet[axis] += d;
    return {};
}

std::vector<Vec3> SlideMove(const World& world, Body body, Vec3& feet, Vec3 delta) {
    Vec3 rem{delta.x, 0.0f, delta.z};
    std::vector<Vec3> normals;
    for (int i = 0; i < 3; ++i) {
        if (rem.LengthSquared() < 1e-12f) break;
        const Aabb me = body.AabbAt(feet);
        const auto h = world.Sweep(me.Size() * 0.5f, me.Center(), rem);
        if (!h) {
            feet += rem;
            break;
        }
        const float progress = rem.Length() * h->t;
        feet += rem * h->t;
        rem *= 1.0f - h->t;
        if (h->normal.y >= WALKABLE) {
            // Ground you can walk up: carry on along it, up the slope.
            const float into = rem.Dot(h->normal);
            if (into < 0.0f) {
                rem -= h->normal * into;
            } else if (progress < 0.001f) {
                // Stopped dead by the edge of something flat just above the feet (a seam where
                // two floors meet a centimetre apart): over it, as a foot would. The walk
                // settles her onto it after.
                const AxisHit lifted = MoveAxis(world, body, feet, 1, LIP);
                if (!lifted.blocked) continue;
                break;
            }
            continue;
        }
        const Vec3 n = Vec3{h->normal.x, 0.0f, h->normal.z}.NormalizeOrZero();
        if (n == Vec3::Zero()) break;
        normals.push_back(n);
        const float into = rem.Dot(n);
        if (into < 0.0f) rem -= n * into;
    }
    return normals;
}

} // namespace runline
