#pragma once
#include <cstddef>
#include <optional>
#include <utility>
#include "runline/Controller.h"

// Port of faith_move::look: Mirror's Edge's first-person camera rules that aren't animation:
//
// - Look limits per move (TdMove_*.MinLookConstraint / MaxLookConstraint): e.g. hanging on a
//   ledge you can look down only ~18 degrees, sliding +-55.
// - Swan neck (TdSwanNeck, TdMove.SwanNeck*): looking down past 15 degrees slides the camera
//   forward and down (quadratically, up to 35 cm / 30 cm), so you look over your chest instead of
//   out from inside it.
//
// Angles from the game are in Unreal rotation units (65536 = 360 degrees).

namespace runline {

/// Pitch range (down, up) and optional yaw half-range around the yaw the move started with, in
/// radians.
struct LookLimit {
    float pitchMin = 0.0f;
    float pitchMax = 0.0f;
    std::optional<float> yaw;
};

/// The game's constraint for the current move (look_limit).
LookLimit LookLimitFor(const Controller& c);

/// Applies LookLimitFor to the controller's view: eases into a new limit over a moment (the game
/// blends the view too), then holds it.
class LookLimiter {
public:
    void Apply(Controller& c, float dt);
    /// The yaw the current limit is centred on: the body's in a slide (it turns during the
    /// slide), else the yaw the move started with.
    float Base(const Controller& c) const;

private:
    std::optional<std::size_t> kind_;
    float entryYaw_ = 0.0f;
    float age_ = 0.0f;
};

/// Swan-neck settings for the current move (degrees, centimetres).
struct SwanNeckSettings {
    /// TdMove.SwanNeckEnableAtPitch: start translating past this much look-down.
    float startDeg = 0.0f;
    /// TdMove.SwanNeckForward / SwanNeckDown at full look-down.
    float forwardCm = 0.0f;
    float downCm = 0.0f;
};

std::optional<SwanNeckSettings> SwanNeckSettingsFor(const Controller& c);

/// The camera's swan-neck offset (TdSwanNeck, "quadratic" type, as TdPawn.CalcCamera applies it).
class SwanNeck {
public:
    /// Returns (forward, down) in metres, in the view's yaw frame (GetSwanNeckPos takes the view
    /// rotation with pitch and roll zeroed).
    std::pair<float, float> Update(const Controller& c, float dt);

private:
    float forward_ = 0.0f;
    float down_ = 0.0f;
};

/// Upper bound on looking down at all (the controller's own clamp).
inline constexpr float MAX_LOOK_DOWN = -1.57079632679489661923f + 0.09f;

} // namespace runline
