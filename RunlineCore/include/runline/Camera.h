#pragma once
#include <optional>
#include "runline/Controller.h"

// Port of faith_move::camera: camera feel: bob, sway, tilt, landing kicks, screen shake and FOV.
//
// The controller hands back a plain eye position and look angles (Controller::GetView);
// CameraFx layers everything that makes it feel like a body is carrying the camera. Kept separate
// so a host engine can use the controller with its own camera if it wants.
//
// (camera.rs's SpeedBlur, Mirror's Edge's speed blur, isn't ported: faith-runner's app doesn't use
// it, and showing it needs a post-process material. Parked as a later polish item.)

namespace runline {

struct CameraFxSettings {
    /// Head bob, metres/degrees at full sprint.
    float bobVertical = 0.045f;
    float bobLateral = 0.03f;
    float bobRollDeg = 0.9f;
    /// Footsteps per second = base + per_speed x speed (m/s).
    float stepRateBase = 1.2f;
    float stepRatePerSpeed = 0.2f;
    /// Tilt into strafes and sharp turns.
    float strafeLeanDeg = 1.6f;
    float turnLeanDeg = 2.0f;
    /// Tilt away from the wall while wallrunning; lean while sliding.
    float wallrunRollDeg = 12.0f;
    float slideRollDeg = -5.0f;
    /// Screen shake at full trauma.
    float shakePitchDeg = 3.0f;
    float shakeYawDeg = 2.2f;
    float shakeRollDeg = 4.0f;
    float shakeOffset = 0.04f;
    /// Trauma lost per second.
    float traumaDecay = 1.5f;
    /// Constant low shake at full sprint (wind, footfalls).
    float sprintShake = 0.22f;
    float slideShake = 0.3f;
    float fovBase = 90.0f;
    float fovSprintAdd = 10.0f;
    float fovWallrunAdd = 4.0f;
    /// Forward somersault on a skill roll, as in Mirror's Edge.
    bool rollFlip = true;
    /// Tilt the view up toward the wall top while climbing/hanging.
    bool lookUpAssist = true;
    /// Scale for landing dips/kicks (0 disables).
    float landingKick = 1.0f;

    /// For when the camera motion comes from the character's own animation (the real Mirror's
    /// Edge arms drive bob, tilt, dips and the roll): only screen shake and a light landing kick
    /// are left procedural.
    static CameraFxSettings AnimationDriven();
};

/// Final camera for this frame, plus timing the viewmodel can sync to.
struct Shot {
    View view;
    /// Footstep phase in radians; one full cycle is two steps (left + right).
    float stepPhase = 0.0f;
    /// 0..1: how much the body is in its running gait right now.
    float gait = 0.0f;
    /// Current shake strength 0..1, after squaring.
    float shake = 0.0f;
};

class CameraFx {
public:
    CameraFxSettings settings;

    explicit CameraFx(CameraFxSettings s = {});
    /// Add screen shake (0..1). Big events stack up to 1.
    void AddTrauma(float amount);
    /// Call once per frame after Controller::Step.
    Shot Update(float dt, const Controller& c, const Input& input);

private:
    struct Spring {
        float x = 0.0f;
        float v = 0.0f;
        /// Semi-implicit Euler in small substeps, so long frames (hitches, slow machines) can't make
        /// the spring explode.
        float Step(float dt, float stiffness, float damping);
    };

    float time_ = 0.0f;
    float stepPhase_ = 0.0f;
    float gait_ = 0.0f;
    float roll_ = 0.0f;
    Spring lean_, dip_, pitchKick_;
    float trauma_ = 0.0f;
    float fov_ = 0.0f;
    float lastYaw_ = 0.0f;
    float yawRate_ = 0.0f;
    std::optional<float> flip_;
    /// Extra look-up while climbing/hanging, so the wall top and your hands are in view.
    float assist_ = 0.0f;
    /// TdPlayerController's zoom (TdMove_Vertigo's StartZoom / UnZoom), degrees off the FOV.
    float zoom_ = 0.0f;
};

} // namespace runline
