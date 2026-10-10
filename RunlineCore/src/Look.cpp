#include "runline/Look.h"
#include <cmath>

// Port of faith_move::look (look.rs), in source order.

namespace runline {
namespace {

float Uu(int r) { return static_cast<float>(r) / 65536.0f * kTau; }

/// Rust's f32::to_degrees: a multiply by the f32 constant (not 180 / PI).
float ToDegrees(float r) { return r * 57.2957795130823208767981548141051703f; }

constexpr float kFracPi4 = 0.785398163397448309615660845819875721f;

/// The player controller's own limit when no move constrains the look.
constexpr LookLimit FREE{-1.48f, 1.48f, std::nullopt};

LookLimit L(int minPitch, int maxPitch, std::optional<int> yaw) {
    LookLimit l;
    l.pitchMin = FMax(Uu(minPitch), -1.48f);
    l.pitchMax = FMin(Uu(maxPitch), 1.48f);
    if (yaw) l.yaw = Uu(*yaw);
    return l;
}

float Wrap(float a) { return RemEuclid(a + kPi, kTau) - kPi; }

} // namespace

LookLimit LookLimitFor(const Controller& c) {
    const State& s = c.state;
    // TdMove_Crouch
    if (std::holds_alternative<Ground>(s)) return c.crouched ? L(-14000, 14000, std::nullopt) : FREE;
    // TdMove_Coil
    if (std::holds_alternative<Air>(s)) return c.IsCoiled() ? L(-5000, 30000, std::nullopt) : FREE;
    // TdMove_Slide
    if (std::holds_alternative<Slide>(s)) return L(-10000, 10000, 10000);
    // TdMove_SkillRoll
    if (std::holds_alternative<Roll>(s)) return L(-2000, 32768, 5000);
    // TdMove_SoftLanding (closest to the hard-landing recovery)
    if (std::holds_alternative<Stunned>(s)) return L(-16384, 16384, 5000);
    // TdMove_WallRun
    if (std::holds_alternative<WallRun>(s)) return L(-13000, 13000, 16384);
    if (std::holds_alternative<WallClimb>(s) || std::holds_alternative<WallClimbTurned>(s)) return FREE;
    // TdMove_Grab
    if (std::holds_alternative<LedgeHang>(s)) return L(-3200, 16000, std::nullopt);
    // TdMove_GrabTransfer: look input ignored (DisableLookTime -1); held as the hang.
    if (std::holds_alternative<GrabTransfer>(s)) return L(-3200, 16000, std::nullopt);
    if (const auto* tr = std::get_if<Traverse>(&s)) {
        switch (tr->kind) {
            // TdMove_SpeedVault / TdMove_StepUp
            case TraverseKind::Vault:
            case TraverseKind::Mantle: return L(-3000, 6000, 8000);
            // TdMove_GrabPullUp
            case TraverseKind::PullUp: return L(0, 16384, 10000);
            case TraverseKind::SpringBoard: return FREE;
        }
    }
    // TdMove_SpeedVault
    if (std::holds_alternative<Vault>(s)) return L(-3000, 6000, 8000);
    // TdMove_LayOnGround
    if (std::holds_alternative<LayOnGround>(s)) return L(-2000, 32768, 5000);
    // TdMove_Stumble / TdMove_Landing (soft): look input ignored, levelled
    if (std::holds_alternative<SoftLand>(s)) return L(-16384, 16384, 5000);
    // Not ported yet (M4, with their states): Barge (-14000, 16384, 5000), AirBarge (free),
    // Stumble (-16384, 16384, 5000), Balance (-13000, 25000, 6000), ZipLine (-2500, 32768, 7000),
    // Swing / SwingJump (-11000, 16384), Takedown (free), StepUp (free), RumpSlide
    // (-5000, 5000, 5000), Vertigo (free), IntoClimb / ClimbExit (free), Climb (-5000, 10000, 32000).
    return FREE;
}

float LookLimiter::Base(const Controller& c) const {
    // TdMove_Slide's constraint is around the body, which turns during the slide.
    return std::holds_alternative<Slide>(c.state) ? c.slideYaw : entryYaw_;
}

void LookLimiter::Apply(Controller& c, float dt) {
    const std::size_t kind = c.state.index();
    if (kind_ != kind) {
        kind_ = kind;
        entryYaw_ = c.yaw;
        age_ = 0.0f;
    }
    age_ += dt;
    const LookLimit lim = LookLimitFor(c);
    const auto ease = [dt](float cur, float target, float age) {
        return age > 0.3f ? target : cur + (target - cur) * (1.0f - std::exp(-14.0f * dt));
    };
    const float lo = lim.pitchMin, hi = lim.pitchMax;
    if (c.pitch < lo) {
        c.pitch = ease(c.pitch, lo, age_);
    } else if (c.pitch > hi) {
        c.pitch = ease(c.pitch, hi, age_);
    }
    if (lim.yaw) {
        const float half = *lim.yaw;
        const float base = Base(c);
        const float d = Wrap(c.yaw - base);
        if (std::fabs(d) > half) {
            const float target = c.yaw - d + FClamp(d, -half, half);
            c.yaw = ease(c.yaw, target, age_);
        }
    }
}

std::optional<SwanNeckSettings> SwanNeckSettingsFor(const Controller& c) {
    // TdMove defaults; each move sets its own on StartMove and the defaults come back on StopMove.
    const SwanNeckSettings base{15.0f, 35.0f, 30.0f};
    const SwanNeckSettings off{0.0f, 0.0f, 0.0f};
    const State& s = c.state;
    // TdMove_Grab: SwanNeckEnableAtPitch 0, SwanNeckForward 70
    if (std::holds_alternative<LedgeHang>(s)) return SwanNeckSettings{0.0f, 70.0f, base.downCm};
    // TdMove_Climb: SwanNeckForward 40
    if (const auto* tr = std::get_if<Traverse>(&s); tr && tr->kind == TraverseKind::PullUp)
        return SwanNeckSettings{base.startDeg, 40.0f, base.downCm};
    // TdMove_180TurnInAir and TdMove_LayOnGround: SwanNeck 0, 0, 0
    if (std::holds_alternative<LayOnGround>(s)) return off;
    if (std::holds_alternative<Air>(s) && c.turnedInAir) return off;
    // TdMove_SkillRoll: a 0.2 s move timer calls DisableSwanneck (constraints 0, 0, 0)
    if (const auto* r = std::get_if<Roll>(&s); r && r->t >= 0.2f) return off;
    return base;
}

std::pair<float, float> SwanNeck::Update(const Controller& c, float dt) {
    const SwanNeckSettings s = SwanNeckSettingsFor(c).value_or(SwanNeckSettings{0.0f, 0.0f, 0.0f});
    // TdSwanNeck.GetSwanNeckTranslation: over the look-down range DownwardPitchWorld (48151) to
    // ForwardPitchWorld (65536), i.e. 0-95.5 degrees down, past the start angle u goes 0..1;
    // forward is F*u*cos(u*pi/4), down is D*u*sin(u*pi/4).
    const float rangeDeg = (65536.0f - 48151.0f) / 65536.0f * 360.0f;
    const float downDeg = ToDegrees(-c.pitch);
    float f = 0.0f, d = 0.0f;
    if (downDeg > 0.0f && downDeg < rangeDeg) {
        const float u = FMax((downDeg - s.startDeg) / (rangeDeg - s.startDeg), 0.0f);
        const float a = u * kFracPi4;
        f = s.forwardCm * u * std::cos(a) * 0.01f;
        d = s.downCm * u * std::sin(a) * 0.01f;
    }
    // UpdateSwanNeck: move a fraction dt/0.07 of the way each frame, never stepping further out
    // than the constraint in one go (FMin, so moving back is uncapped).
    const float k = FMin(dt / 0.07f, 1.0f);
    forward_ += FMin((f - forward_) * k, s.forwardCm * 0.01f);
    down_ += FMin((d - down_) * k, s.downCm * 0.01f);
    return {forward_, down_};
}

} // namespace runline
