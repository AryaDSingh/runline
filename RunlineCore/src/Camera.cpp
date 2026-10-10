#include "runline/Camera.h"
#include <cmath>

// Port of faith_move::camera (camera.rs), in source order.

namespace runline {
namespace {

/// Rust's f32::to_radians: a multiply by the f32 constant PI / 180.
constexpr float kRadsPerDeg = kPi / 180.0f;
float ToRadians(float deg) { return deg * kRadsPerDeg; }

/// Smooth, cheap pseudo-noise in about [-1, 1].
float Noise(float t, float seed) {
    return 0.5f * std::sin(t * 23.0f + seed) + 0.3f * std::sin(t * 37.3f + seed * 1.7f) + 0.2f * std::sin(t * 53.1f + seed * 2.3f);
}

float Approach(float cur, float target, float rate, float dt) {
    return cur + (target - cur) * (1.0f - std::exp(-rate * dt));
}

} // namespace

CameraFxSettings CameraFxSettings::AnimationDriven() {
    CameraFxSettings s;
    s.bobVertical = 0.0f;
    s.bobLateral = 0.0f;
    s.bobRollDeg = 0.0f;
    s.strafeLeanDeg = 0.0f;
    s.turnLeanDeg = 0.0f;
    s.wallrunRollDeg = 0.0f;
    s.slideRollDeg = 0.0f;
    s.sprintShake = 0.08f;
    s.slideShake = 0.15f;
    s.fovSprintAdd = 0.0f;
    s.fovWallrunAdd = 0.0f;
    s.rollFlip = false;
    s.lookUpAssist = false;
    s.landingKick = 0.5f;
    return s;
}

float CameraFx::Spring::Step(float dt, float stiffness, float damping) {
    float left = FMin(dt, 0.25f);
    while (left > 0.0f) {
        const float h = FMin(left, 1.0f / 240.0f);
        v += (-stiffness * x - damping * v) * h;
        x += v * h;
        left -= h;
    }
    return x;
}

CameraFx::CameraFx(CameraFxSettings s) : settings(s), fov_(s.fovBase) {}

void CameraFx::AddTrauma(float amount) {
    trauma_ = FMin(trauma_ + amount, 1.0f);
}

Shot CameraFx::Update(float dt, const Controller& c, const Input& input) {
    dt = FClamp(dt, 0.0f, 0.1f);
    const CameraFxSettings s = settings;
    time_ += dt;
    const View base = c.GetView();
    const Tuning& tu = c.tuning;
    const float speed = c.HorizontalSpeed();
    const float sprintFrac = FClamp((speed - tu.runSpeed) / (tu.sprintSpeed - tu.runSpeed), 0.0f, 1.0f);

    // Yaw rate, ignoring the 180 turn (that's a deliberate spin, not a lean).
    const float dyaw = base.yaw - lastYaw_;
    lastYaw_ = base.yaw;
    const float rawRate = dt > 0.0f ? dyaw / dt : 0.0f;
    yawRate_ = Approach(yawRate_, FClamp(rawRate, -12.0f, 12.0f), 10.0f, dt);

    // ---- events -> kicks and trauma
    for (const Event& e : c.events) {
        switch (e.kind) {
            case EventKind::Jump: AddTrauma(0.03f); break;
            case EventKind::Land:
                dip_.v -= s.landingKick * FMin(e.impact * 0.07f, 1.8f);
                pitchKick_.v -= s.landingKick * FMin(e.impact * 0.06f, 1.2f);
                AddTrauma(FClamp((e.impact - 5.0f) * 0.05f, 0.0f, 0.35f));
                break;
            case EventKind::HardLand:
                dip_.v -= 2.4f * s.landingKick;
                pitchKick_.v -= 3.0f * s.landingKick;
                AddTrauma(0.8f);
                break;
            case EventKind::Roll:
                AddTrauma(0.3f);
                if (s.rollFlip) flip_ = 0.0f;
                break;
            case EventKind::WallRunStart: AddTrauma(0.12f); break;
            case EventKind::WallJump: AddTrauma(0.15f); break;
            case EventKind::WallClimbStart:
                AddTrauma(0.12f);
                pitchKick_.v += 0.5f;
                break;
            case EventKind::WallKick: AddTrauma(0.25f); break;
            case EventKind::LedgeGrab:
                AddTrauma(0.35f);
                dip_.v -= 1.0f;
                pitchKick_.v -= 0.4f;
                break;
            case EventKind::PullUp:
            case EventKind::Mantle: AddTrauma(0.1f); break;
            case EventKind::Vault:
                AddTrauma(0.12f);
                dip_.v += 0.5f;
                break;
            case EventKind::Slide: AddTrauma(0.18f); break;
            case EventKind::Dodge:
            case EventKind::WallRunDodge:
            case EventKind::WallClimbDodge: {
                AddTrauma(0.15f);
                // Lean into the dodge.
                const Vec3 right{std::cos(base.yaw), 0.0f, -std::sin(base.yaw)};
                lean_.v += e.dir.Dot(right) * 0.9f;
                break;
            }
            case EventKind::Death:
                trauma_ = 0.0f;
                flip_.reset();
                break;
            default: break;
        }
    }

    // ---- gait and footstep phase
    const bool running = std::holds_alternative<Ground>(c.state) && speed > 0.5f;
    gait_ = Approach(gait_, running ? 1.0f : 0.0f, 8.0f, dt);
    if (running) stepPhase_ = std::fmod(stepPhase_ + dt * kPi * (s.stepRateBase + s.stepRatePerSpeed * speed), kTau);
    const float amp = gait_ * std::sqrt(FMin(speed / tu.sprintSpeed, 1.0f));
    const float bobY = s.bobVertical * amp * std::sin(2.0f * stepPhase_);
    const float bobX = s.bobLateral * amp * std::sin(stepPhase_);
    const float bobRoll = ToRadians(s.bobRollDeg) * amp * std::sin(stepPhase_);

    // ---- tilt
    const Vec3 right{std::cos(base.yaw), 0.0f, -std::sin(base.yaw)};
    float stateRoll = 0.0f;
    if (const auto* w = std::get_if<WallRun>(&c.state)) stateRoll = w->normal.Dot(right) * ToRadians(s.wallrunRollDeg);
    else if (std::holds_alternative<Slide>(c.state)) stateRoll = ToRadians(s.slideRollDeg);
    float steerRoll = 0.0f;
    if (std::holds_alternative<Ground>(c.state) || std::holds_alternative<Air>(c.state))
        steerRoll = input.moveAxis.x * ToRadians(s.strafeLeanDeg) - FClamp(yawRate_ / 8.0f, -1.0f, 1.0f) * ToRadians(s.turnLeanDeg);
    roll_ = Approach(roll_, stateRoll + steerRoll, 8.0f, dt);
    const float lean = lean_.Step(dt, 60.0f, 11.0f);

    // ---- springs
    const float dip = FClamp(dip_.Step(dt, 120.0f, 17.0f), -0.45f, 0.2f);
    const float pitchKick = FClamp(pitchKick_.Step(dt, 110.0f, 16.0f), -0.35f, 0.2f);

    // ---- shake
    trauma_ = FMax(trauma_ - s.traumaDecay * dt, 0.0f);
    float floor = 0.0f;
    if (std::holds_alternative<Slide>(c.state)) floor = s.slideShake;
    else if (std::holds_alternative<Ground>(c.state)) floor = s.sprintShake * sprintFrac * sprintFrac;
    else if (std::holds_alternative<WallRun>(c.state)) floor = s.sprintShake * 0.8f;
    const float shake = FMax(trauma_ * trauma_, floor * floor);
    const float t = time_;
    const float shPitch = ToRadians(s.shakePitchDeg) * shake * Noise(t, 1.0f);
    const float shYaw = ToRadians(s.shakeYawDeg) * shake * Noise(t, 7.0f);
    const float shRoll = ToRadians(s.shakeRollDeg) * shake * Noise(t, 13.0f);
    const Vec3 shOff = Vec3{Noise(t, 21.0f), Noise(t, 29.0f), 0.0f} * s.shakeOffset * shake;

    // ---- skill-roll somersault
    float flipPitch = 0.0f;
    float flipDrop = 0.0f;
    if (flip_) {
        *flip_ += dt / 0.5f;
        const float k = FClamp(*flip_, 0.0f, 1.0f);
        const float e = k * k * (3.0f - 2.0f * k);
        flipPitch = -kTau * e;
        flipDrop = -0.5f * std::sin(kPi * k);
        if (*flip_ >= 1.0f) flip_.reset();
    }

    // ---- look-up assist on walls (ME constrains/raises the view here too)
    float assistTarget = 0.0f;
    if (std::holds_alternative<WallClimb>(c.state)) assistTarget = 0.5f;
    else if (std::holds_alternative<WallClimbTurned>(c.state)) assistTarget = 0.15f;
    else if (const auto* l = std::get_if<LedgeHang>(&c.state); l && !l->turned) assistTarget = 0.42f;
    else if (const auto* tr = std::get_if<Traverse>(&c.state); tr && tr->kind == TraverseKind::PullUp) assistTarget = 0.42f * FMax(1.0f - tr->t, 0.0f);
    if (!s.lookUpAssist) assistTarget = 0.0f;
    assist_ = Approach(assist_, assistTarget, 7.0f, dt);

    // ---- FOV
    float stateFov = 0.0f;
    if (std::holds_alternative<WallRun>(c.state)) stateFov = s.fovWallrunAdd;
    else if (const auto* tr = std::get_if<Traverse>(&c.state); tr && tr->kind == TraverseKind::Vault) stateFov = 3.0f;
    else if (const auto* v = std::get_if<Vault>(&c.state); v && !v->Onto()) stateFov = 3.0f;
    const float targetFov = s.fovBase + s.fovSprintAdd * sprintFrac + stateFov;
    fov_ = Approach(fov_, targetFov, 4.0f, dt);
    // TdMove_Vertigo: StartZoom(ZoomFOV 84 of the default 90, ZoomRate 30 a second); UnZoom back at 20.
    const float zoomTo = c.vertigoZoom ? 84.0f - 90.0f : 0.0f;
    const float zoomRate = c.vertigoZoom ? 30.0f : 20.0f;
    zoom_ += FClamp(zoomTo - zoom_, -zoomRate * dt, zoomRate * dt);

    const Vec3 eye = base.eye
        + Vec3::UnitY() * (dip + bobY + flipDrop)
        + right * bobX
        + right * shOff.x
        + Vec3::UnitY() * shOff.y;

    Shot shot;
    shot.view = View{eye, base.yaw + shYaw, base.pitch + assist_ + pitchKick + shPitch + flipPitch, roll_ + bobRoll + lean + shRoll, fov_ + zoom_};
    shot.stepPhase = stepPhase_;
    shot.gait = gait_;
    shot.shake = shake;
    return shot;
}

} // namespace runline
