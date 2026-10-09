#include "runline/MeshWorld.h"
#include <cmath>
#include <cstddef>
#include <utility>

namespace runline {
namespace {

float Support(Vec3 half, Vec3 n) { return half.x * std::fabs(n.x) + half.y * std::fabs(n.y) + half.z * std::fabs(n.z); }

Vec3 FaceNormal(const Tri& t) { return (t[1] - t[0]).Cross(t[2] - t[0]).NormalizeOrZero(); }

} // namespace

Aabb TriBounds(const Tri& t) { return Aabb{Vec3::Min(Vec3::Min(t[0], t[1]), t[2]), Vec3::Max(Vec3::Max(t[0], t[1]), t[2])}; }

bool TriBoxOverlap(const Tri& t, Vec3 c, Vec3 h) {
    const Vec3 v[3] = {t[0] - c, t[1] - c, t[2] - c};
    const Vec3 e[3] = {v[1] - v[0], v[2] - v[1], v[0] - v[2]};
    const auto sep = [&](Vec3 axis) {
        if (axis.LengthSquared() < 1e-12f) return false;
        axis = axis.Normalize();
        const float p0 = v[0].Dot(axis), p1 = v[1].Dot(axis), p2 = v[2].Dot(axis);
        const float r = h.x * std::fabs(axis.x) + h.y * std::fabs(axis.y) + h.z * std::fabs(axis.z);
        const float mn = FMin(FMin(p0, p1), p2);
        const float mx = FMax(FMax(p0, p1), p2);
        return mn >= r - TOUCH || mx <= -r + TOUCH;
    };
    const Vec3 axes[3] = {Vec3::UnitX(), Vec3::UnitY(), Vec3::UnitZ()};
    for (const Vec3& a : axes)
        if (sep(a)) return false;
    const Vec3 n = e[0].Cross(e[1]);
    if (sep(n)) return false;
    for (const Vec3& a : axes)
        for (const Vec3& ed : e)
            if (sep(a.Cross(ed))) return false;
    return true;
}

std::optional<SweepHit> MeshWorld::Sweep(Vec3 half, Vec3 start, Vec3 delta) const {
    const Aabb a = Aabb::New(start - half, start + half);
    const Aabb region = a.Union(a.Translated(delta));
    const Aabb grown = Aabb::New(region.min - Vec3::Splat(0.01f), region.max + Vec3::Splat(0.01f));
    std::vector<const Tri*> near;
    for (const Tri& t : tris)
        if (TriBounds(t).Overlaps(grown)) near.push_back(&t);
    // A surface the box already cuts into: what it starts inside doesn't count (it can always
    // get out), except a surface it has only sunk into by a hair (rounding far from the origin,
    // a refreshed world): moving further into that is blocked straight away, so the box can't
    // sink on through it.
    std::optional<std::pair<float, Vec3>> skin;
    for (const Tri* t : near) {
        if (!TriBoxOverlap(*t, start, half)) continue;
        Vec3 n = FaceNormal(*t);
        if (n == Vec3::Zero()) continue;
        if (n.Dot(start - (*t)[0]) < 0.0f) n = -n;
        const float depth = Support(half, n) - n.Dot(start - (*t)[0]);
        const float toward = -n.Dot(delta);
        if (depth < SKIN && toward > 1e-7f && (!skin || toward > skin->first)) skin = std::pair{toward, n};
    }
    if (skin) {
        const Vec3 n = skin->second;
        return SweepHit{0.0f, n, start - n * Support(half, n)};
    }
    std::vector<const Tri*> ts;
    for (const Tri* t : near)
        if (!TriBoxOverlap(*t, start, half)) ts.push_back(t);
    if (ts.empty()) return std::nullopt;
    const auto hitAt = [&](float s) {
        for (const Tri* t : ts)
            if (TriBoxOverlap(*t, start + delta * s, half)) return true;
        return false;
    };
    const float len = delta.Length();
    // Steps no longer than the box is thick along the path, so a thin box can't skip over a
    // surface between two of them.
    const Vec3 dir = delta / FMax(len, 1e-9f);
    const float thick = 2.0f * (half.x * std::fabs(dir.x) + half.y * std::fabs(dir.y) + half.z * std::fabs(dir.z));
    const float step = FMax(FMin(0.02f, thick * 0.9f), 1e-4f);
    const std::size_t steps = static_cast<std::size_t>(FMax(std::ceil(len / step), 1.0f));
    float prev = 0.0f;
    for (std::size_t i = 1; i <= steps; ++i) {
        const float s = static_cast<float>(i) / static_cast<float>(steps);
        if (hitAt(s)) {
            float lo = prev, hi = s;
            for (int k = 0; k < 14; ++k) {
                const float mid = (lo + hi) * 0.5f;
                if (hitAt(mid)) hi = mid;
                else lo = mid;
            }
            // Of the triangles touched (several at an edge), the one met most head-on.
            const Vec3 at = start + delta * lo;
            // Surfaces met within a hair of each other count as met together (the bisection
            // settles on whichever overlap test flips first, to rounding).
            const Vec3 probe = start + delta * FMin(hi + 4.0f * TOUCH / FMax(len, 1e-9f), 1.0f);
            std::optional<std::pair<float, Vec3>> best;
            for (const Tri* t : ts) {
                if (!TriBoxOverlap(*t, probe, half)) continue;
                Vec3 n = FaceNormal(*t);
                if (n.Dot(at - (*t)[0]) < 0.0f) n = -n;
                const float headOn = -n.Dot(delta);
                if (!best || headOn > best->first) best = std::pair{headOn, n};
            }
            if (!best) return std::nullopt;
            const Vec3 n = best->second;
            // Bisection stops where the overlap test flips: TOUCH deep. Back off along the path
            // to just outside the surface, so sliding along it from here is clear.
            const float toward = -n.Dot(delta);
            const float t = toward > 1e-6f ? FMax(lo - 2.0f * TOUCH / toward, 0.0f) : lo;
            const Vec3 at2 = start + delta * t;
            return SweepHit{t, n, at2 - n * Support(half, n)};
        }
        prev = s;
    }
    return std::nullopt;
}

bool MeshWorld::Overlaps(const Aabb& region) const {
    const Vec3 c = region.Center();
    const Vec3 h = region.Size() * 0.5f;
    for (const Tri& t : tris)
        if (TriBounds(t).Overlaps(region) && TriBoxOverlap(t, c, h)) return true;
    return false;
}

} // namespace runline
