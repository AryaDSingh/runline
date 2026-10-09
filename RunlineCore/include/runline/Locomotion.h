#pragma once
// Running on the ground, the way Mirror's Edge does it. Port of faith_move::locomotion, which
// ports the game's native ATdPawn::GetSprintAcceleration / GetWalkAcceleration / CalcVelocity.
// Units are metres and seconds.
#include <cstddef>
#include <utility>
#include <vector>
#include "runline/Vec3.h"

namespace runline {

/// TdPawn.SpeedCurve_LightWeapon: (seconds of running, speed m/s), linear.
inline const std::vector<std::pair<float, float>> SPEED_CURVE{{0.0f, 0.0f}, {0.4f, 4.0f}, {1.0f, 5.2f}, {3.5f, 6.5f}, {7.0f, 7.2f}};

/// A piecewise-linear FInterpCurveFloat (CIM_Linear points), evaluated the way
/// FInterpCurve::Eval does: clamped at both ends.
struct Curve {
    std::vector<std::pair<float, float>> points;
    float Eval(float x) const;
};

/// ATdPawn building AccelCurve_LightWeapon from the speed curve: acceleration against speed.
Curve AccelCurveFrom(const std::vector<std::pair<float, float>>& speed, std::size_t steps);

/// What the pawn carries between frames.
struct RunState {
    /// TdPawn.SpeedSprintEnergy (m/s above SpeedMaxBaseVelocity).
    float energy = 0.0f;
    /// TdPlayerController.AccelerationTime / bIsStopping (the tap-stop).
    float accelTime = 0.0f;
    float stopping = 0.0f;
};

/// One frame's input, as the controller hands it to the pawn.
struct RunInput {
    /// Facing (horizontal, unit) and its right.
    Vec3 forward{};
    Vec3 right{};
    /// PlayerInput.aForward / aStrafe (-1..1).
    float aForward = 0.0f;
    float aStrafe = 0.0f;
    /// How far the view turned this frame, in radians.
    float turn = 0.0f;
};

/// TdPawn's ground-movement numbers (defaults from TdGame.u, uu converted to m).
struct Locomotion {
    Curve accelCurve = AccelCurveFrom(SPEED_CURVE, 10);
    /// SpeedMaxBaseVelocity = 400: above this, the excess is sprint energy.
    float maxBase = 4.0f;
    /// SpeedMinBaseVelocity = 10.
    float minBase = 0.1f;
    /// Speed{Sprint,Walk,Strafe}VelocityAccelerationFactor = 30 / 7 / 10.
    float sprintFactor = 30.0f;
    float walkFactor = 7.0f;
    float strafeFactor = 10.0f;
    /// SpeedEnergyDecelerationTime = 3, SpeedEnergyDecelerationExponent = 0.5.
    float energyDecelTime = 3.0f;
    float energyDecelExponent = 0.5f;
    /// SpeedTurnDecelerationFactor = 10.
    float turnDecel = 10.0f;
    /// Pawn.GroundSpeed = 720 (top speed) and AccelRate = 6144 (most acceleration).
    float groundSpeed = 7.2f;
    float accelRate = 61.44f;
    /// PhysicsVolume.GroundFriction = 8.
    float groundFriction = 8.0f;
    /// TdPlayerPawn.BrakingFrictionStrength (TdPawn's own is 1.0).
    float brakingStrength = 0.5f;
    /// TdPlayerController.InputMaxSprintHeightLimit / RaduisLimit = 0.7.
    float sprintInputForward = 0.7f;
    float sprintInputSize = 0.7f;

    /// ATdPawn::GetSprintAcceleration.
    Vec3 SprintAccel(RunState& st, const RunInput& i, Vec3 vel, float dt, bool falling) const;
    /// ATdPawn::GetWalkAcceleration.
    Vec3 WalkAccel(RunState& st, const RunInput& i, Vec3 vel, float dt, bool falling) const;
    /// TdPlayerController's PlayerWalking: which acceleration to ask for, and the tap-stop.
    /// May set `vel` for the stop.
    Vec3 ControllerAccel(RunState& st, const RunInput& i, Vec3& vel, float dt) const;
    /// ATdPawn::CalcVelocity for walking.
    Vec3 CalcVelocity(Vec3 vel, Vec3 accel, float dt, float speedMod, float friction) const;

private:
    Vec3 InputDir(const RunInput& i) const;
    Vec3 FrictionComp(Vec3 vel) const;
    Vec3 TurnDamping(Vec3 vel, float turn) const;
};

} // namespace runline
