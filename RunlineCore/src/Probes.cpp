#include "runline/Probes.h"
#include <algorithm>
#include <cmath>

namespace runline {
namespace {

/// The horizontal part of a surface normal, if it's a wall (steeper than ~45 degrees).
std::optional<Vec3> WallNormal(Vec3 n) {
    const Vec3 h{n.x, 0.0f, n.z};
    if (std::fabs(n.y) < 0.7f && h.LengthSquared() > 1e-6f) return h.Normalize();
    return std::nullopt;
}

/// Gap between the body and the plane through `point` with horizontal normal `n`.
float WallGap(Body body, Vec3 feet, Vec3 n, Vec3 point) {
    const float d = Vec3{feet.x - point.x, 0.0f, feet.z - point.z}.Dot(n);
    return FMax(d - body.ReachToward(n), 0.0f);
}

} // namespace

Closest ClosestOnSegment(Vec3 a, Vec3 b, Vec3 p) {
    const Vec3 ab = b - a;
    const float len = ab.Length();
    if (len < 1e-6f) return {a, 0.0f};
    const Vec3 u = ab / len;
    const float s = FClamp((p - a).Dot(u), 0.0f, len);
    return {a + u * s, s};
}

std::optional<WallProbe> ProbeWall(const World& world, Body body, Vec3 feet, Vec3 dir, float reach, float yLo, float yHi) {
    dir = Vec3{dir.x, 0.0f, dir.z}.NormalizeOrZero();
    if (dir == Vec3::Zero()) return std::nullopt;
    Aabb me = body.AabbAt(feet);
    me.min.y = feet.y + yLo;
    me.max.y = feet.y + yHi;
    const auto h = world.Sweep(me.Size() * 0.5f, me.Center(), dir * reach);
    if (!h) return std::nullopt;
    const auto n = WallNormal(h->normal);
    if (!n) return std::nullopt;
    if (n->Dot(dir) > -0.2f) return std::nullopt;
    return WallProbe{*n, WallGap(body, feet, *n, h->point)};
}

std::optional<BoxTrace> TraceBox(const World& world, Vec3 start, Vec3 dir, float reach, Vec3 half) {
    dir = dir.NormalizeOrZero();
    if (dir == Vec3::Zero() || reach <= 0.0f) return std::nullopt;
    const auto h = world.Sweep(half, start, dir * reach);
    if (!h) return std::nullopt;
    return BoxTrace{start + dir * reach * h->t, h->normal};
}

std::optional<WallHit> TraceWall(const World& world, Body body, Vec3 feet, Vec3 dir, float reach, float y) {
    dir = Vec3{dir.x, 0.0f, dir.z}.NormalizeOrZero();
    if (dir == Vec3::Zero() || reach <= 0.0f) return std::nullopt;
    const Vec3 half{body.halfWidth, 0.02f, body.halfWidth};
    const Vec3 c = feet + Vec3::UnitY() * y;
    const auto h = world.Sweep(half, c, dir * reach);
    if (!h) return std::nullopt;
    const auto n = WallNormal(h->normal);
    if (!n) return std::nullopt;
    const Vec3 p{c.x, c.y, c.z};
    const Vec3 at = p - *n * Vec3{p.x - h->point.x, 0.0f, p.z - h->point.z}.Dot(*n);
    return WallHit{*n, at, WallGap(body, feet, *n, h->point)};
}

std::vector<float> TopsBelow(const World& world, Vec3 p, float halfXZ, float yHi, float yLo) {
    const Vec3 half{halfXZ, 0.005f, halfXZ};
    std::vector<float> out;
    float y = yHi;
    for (int i = 0; i < 8; ++i) {
        const float d = yLo - y;
        if (d >= 0.0f) break;
        const Vec3 c{p.x, y + half.y, p.z};
        const auto h = world.Sweep(half, c, Vec3::UnitY() * d);
        if (!h) break;
        const float top = y + d * h->t;
        if (h->normal.y > 0.7f) out.push_back(top);
        y = top - 0.03f;
    }
    return out;
}

float ColumnTop(const World& world, Vec3 p, float y0, float y1) {
    const auto tops = TopsBelow(world, p, 0.01f, y1, y0);
    return tops.empty() ? y0 : tops.front();
}

std::optional<float> FindLedge(const World& world, Body body, Vec3 feet, Vec3 n, float lo, float hi, float crouchHeight) {
    const float hw = body.halfWidth;
    n = Vec3{n.x, 0.0f, n.z}.NormalizeOrZero();
    // Column just inside the wall face, in front of the player.
    const Vec3 inside = feet - n * (body.ReachToward(n) + 0.02f + hw);
    const Aabb me = body.AabbAt(feet);
    auto tops = TopsBelow(world, inside, hw, feet.y + hi + 0.05f, feet.y + lo - 0.05f);
    std::erase_if(tops, [&](float t) { return !(t >= feet.y + lo && t <= feet.y + hi); });
    std::stable_sort(tops.begin(), tops.end());   // Rust's sort_by is stable
    for (const float t : tops) {
        // Room to crouch on top of the ledge.
        const Aabb stand = Aabb::New({inside.x - hw, t + 0.01f, inside.z - hw}, {inside.x + hw, t + crouchHeight, inside.z + hw});
        if (!world.IsFree(stand)) continue;
        // Hands/head space on our side of the wall, just above the lip.
        Aabb hands = me;
        hands.min.y = FMax(t, feet.y + 0.1f);
        hands.max.y = t + 0.35f;
        if (!world.IsFree(hands)) continue;
        return t;
    }
    return std::nullopt;
}

} // namespace runline
