#pragma once
#include <optional>
#include <vector>
#include "runline/Move.h"

namespace runline {

/// Closest point on segment a–b to p, and its distance along from a.
struct Closest { Vec3 point; float along; };
Closest ClosestOnSegment(Vec3 a, Vec3 b, Vec3 p);

/// A wall face in front of the body: its horizontal normal and the gap to it.
struct WallProbe { Vec3 n; float gap; };
/// Sweep a slice of the body (from `yLo` to `yHi` above the feet) along `dir` for `reach`;
/// the wall it meets, if it faces us within about 80 degrees.
std::optional<WallProbe> ProbeWall(const World& world, Body body, Vec3 feet, Vec3 dir, float reach, float yLo, float yHi);

/// Where a small box swept from `start` along `dir` stopped, and the surface's normal.
struct BoxTrace { Vec3 at; Vec3 normal; };
std::optional<BoxTrace> TraceBox(const World& world, Vec3 start, Vec3 dir, float reach, Vec3 half);

/// A wall face found by TraceWall.
struct WallHit {
    /// Outward face normal (horizontal).
    Vec3 n;
    /// Where the body's centre line, straight along the normal, meets the face (at the trace height).
    Vec3 at;
    /// Gap between the body and the face.
    float gap;
};
/// Sweep a thin slice of the body (`y` above the feet, ±2 cm like ME's extent traces)
/// horizontally along `dir` for up to `reach`: the first wall it runs into, at any angle.
std::optional<WallHit> TraceWall(const World& world, Body body, Vec3 feet, Vec3 dir, float reach, float y);

/// Heights of the walkable tops under a column (half size `halfXZ` across) at `p`, highest
/// first, from `yHi` down to `yLo`.
std::vector<float> TopsBelow(const World& world, Vec3 p, float halfXZ, float yHi, float yLo);

/// Height of the top of whatever stands at `p` (x, z), traced down from `y1`; `y0` if nothing.
float ColumnTop(const World& world, Vec3 p, float y0, float y1);

/// A climbable top edge on the wall with outward normal `n`, between `lo` and `hi` above the
/// feet, with room to crouch on top and nothing overhanging the hands (TdPawn's FindLedge).
std::optional<float> FindLedge(const World& world, Body body, Vec3 feet, Vec3 n, float lo, float hi, float crouchHeight);

} // namespace runline
