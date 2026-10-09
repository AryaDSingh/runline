#include "runline/Controller.h"
#include <cmath>
#include <limits>
#include "runline/Probes.h"

// Port of faith_move::controller's core (controller.rs, in source order). Everything not ported
// yet is in ControllerStubs.cpp.

namespace runline {
namespace {

constexpr float SUBSTEP = 1.0f / 120.0f;

Vec3 ForwardOf(float yaw) { return {-std::sin(yaw), 0.0f, -std::cos(yaw)}; }
Vec3 RightOf(float yaw) { return {std::cos(yaw), 0.0f, -std::sin(yaw)}; }
Vec3 Horiz(Vec3 v) { return {v.x, 0.0f, v.z}; }
/// Unreal units per second (cm/s) to m/s, for thresholds written inline in the game's scripts.
constexpr float UuPerS(float v) { return v / 100.0f; }
float Smoothstep(float x) {
    x = FClamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}
/// Rust's f32::signum: +-1 by the sign bit (so 0.0 gives 1.0).
float Signum(float x) { return std::copysign(1.0f, x); }
/// Rust's f32::rem_euclid.
float RemEuclid(float a, float b) {
    const float r = std::fmod(a, b);
    return r < 0.0f ? r + std::fabs(b) : r;
}
/// glam's Vec3::clamp_length_max.
Vec3 ClampLengthMax(Vec3 v, float max) {
    const float lengthSq = v.LengthSquared();
    if (lengthSq > max * max) {
        const Vec3 u = v / std::sqrt(lengthSq);
        return {max * u.x, max * u.y, max * u.z};
    }
    return v;
}
/// glam's Vec3::normalize_or.
Vec3 NormalizeOr(Vec3 v, Vec3 fallback) {
    const float rcp = v.LengthRecip();
    return (std::isfinite(rcp) && rcp > 0.0f) ? v * rcp : fallback;
}

template <class T>
bool Is(const State& s) { return std::holds_alternative<T>(s); }

} // namespace

const char* EventName(EventKind k) {
    static const char* const names[] = {
        "Jump", "Land", "HardLand", "Roll", "Slide", "WallRunStart", "WallJump", "WallClimbStart", "WallKick", "LedgeGrab",
        "PullUp", "Vault", "Mantle", "Turn180", "WallRunTurn", "LandOnBack", "ShimmyCorner", "SlideEnd", "BackRoll", "Barge",
        "AirBarge", "AirBargeImpact", "Vertigo", "SwingToSwing", "ClimbStart", "ClimbStep", "ClimbExit", "ClimbLetGo",
        "ClimbSlide", "ClimbSlideEnd", "AirBargeLand", "DoorOpened", "Stumble", "SoftLand", "Dodge", "WallRunDodge",
        "WallClimbDodge", "Death", "SpringBoard", "BalanceStart", "BalanceFall", "Takedown", "TakedownDone", "StepUp",
        "RumpSlide", "GrabTransfer", "ZipStart", "ZipBrace", "ZipEnd", "SwingStart", "SwingJump", "Melee", "MeleeOutcome",
        "MeleeHit", "Taunt",
    };
    static_assert(sizeof(names) / sizeof(names[0]) == static_cast<size_t>(EventKind::Taunt) + 1);
    return names[static_cast<size_t>(k)];
}

const char* StateName(const State& s) {
    if (Is<Ground>(s)) return "Ground";
    if (Is<Air>(s)) return "Air";
    if (Is<Slide>(s)) return "Slide";
    if (Is<Roll>(s)) return "Roll";
    if (Is<Stunned>(s)) return "Hard landing";
    if (Is<WallRun>(s)) return "Wallrun";
    if (Is<WallClimb>(s)) return "Wallclimb";
    if (Is<WallClimbTurned>(s)) return "Wallclimb (turned)";
    if (const auto* h = std::get_if<LedgeHang>(&s)) return h->turned ? "Ledge hang (turned)" : "Ledge hang";
    if (Is<SoftLand>(s)) return "Soft landing";
    const auto& l = std::get<LayOnGround>(s);
    if (l.backRoll) return "Back roll";
    if (!l.gettingUp) return "On your back";
    return "Getting up";
}

Controller::Controller(Tuning t, Vec3 sp, float spYaw)
    : tuning(std::move(t)), feet(sp), yaw(spYaw), spawn(sp), spawnYaw(spYaw),
      rollTriggerAge_(std::numeric_limits<float>::infinity()), lastJumpAt_(sp) {
    eyeHeight_ = tuning.standHeight - tuning.eyeFromTop;
    fallPeak_ = sp.y;
}

void Controller::Respawn() {
    feet = spawn;
    vel = Vec3::Zero();
    yaw = spawnYaw;
    pitch = 0.0f;
    state = Air{};
    crouched = false;
    sprintCharge = 0.0f;
    run_ = RunState{};
    turn_.reset();
    climbedThisAir_ = false;
    lastWallNormal_.reset();
    fallPeak_ = spawn.y;
    coiled_ = false;
    melee_.reset();
    lowGrav_ = 0.0f;
    dodging_ = false;
    lookAt_.reset();
    wallTurned_ = false;
    dodgeRedo_ = 0.0f;
    fixtureCooldown_ = 0.0f;
    springboard_ = false;
    takeoff_ = Takeoff::Other;
    chainWallruns_ = 0;
    lastJumpAt_ = spawn;
}

float Controller::Height() const {
    if (coiled_) return tuning.standHeight - tuning.coilLift;
    if (crouched) return tuning.crouchHeight;
    return tuning.standHeight;
}

/// Feet height as if the legs weren't tucked.
float Controller::BodyY() const { return feet.y - (coiled_ ? tuning.coilLift : 0.0f); }

Body Controller::GetBody() const { return Body{tuning.halfWidth, Height()}; }

float Controller::HorizontalSpeed() const { return Horiz(vel).Length(); }

Vec3 Controller::Forward() const { return ForwardOf(yaw); }

void Controller::Step(float dt, const Input& input0, const World& world) {
    events.clear();
    Input input = input0;
    if (input.strafeRaw == 0.0f) input.strafeRaw = FClamp(input.moveAxis.x, -1.0f, 1.0f);
    input.moveAxis = input.moveAxis.ClampLengthMax(1.0f);

    // Doors are solid until knocked open: fixtures (doors among them) come in M4; until then the
    // world is used as it is (faith_move's WithDoors over no doors answers exactly as the world).

    // Look.
    const float yawBefore = yaw;
    // TdMove_Landing.LandHard: SetIgnoreLookInput, and ResetCameraLook(0.3) levels the view.
    if (Is<Stunned>(state)) {
        input.look = Vec2{};
        pitch -= pitch * FMin(dt / 0.3f, 1.0f);
    }
    // TdMOVE_Disarm, TdMove_AirBarge, TdMove_IntoClimb and the takedown blank input in states
    // not ported yet.
    VertigoInput(input);
    if (!turn_) yaw += input.look.x;
    frameTurn_ = !turn_ ? input.look.x : 0.0f;
    pitch = FClamp(pitch + input.look.y, -1.5f, 1.5f);

    if (input.jumpPressed) jumpBuffer_ = tuning.jumpBuffer;
    // TdPawn.Tick: a crouch press arms the roll, but only 0.6 s after the last one did.
    if (input.crouchPressed && rollTriggerAge_ >= tuning.rollRetrigger) rollTriggerAge_ = 0.0f;
    if (input.turnPressed && !turn_ && CanTurn()) StartTurn();
    if (input.meleePressed) {
        if (melee_) MeleePressedAgain();
        else StartMelee();
    }
    if (meleeNoInput_) input.moveAxis = Vec2{};
    if (input.takedownPressed) TryTakedown(world);

    // Substep physics. Edge-triggered inputs only count on the first substep.
    float remaining = FMin(dt, 0.1f);
    bool first = true;
    while (remaining > 1e-6f) {
        const float h = FMin(remaining, SUBSTEP);
        Input sub = input;
        if (!first) {
            sub.jumpPressed = false;
            sub.crouchPressed = false;
            sub.turnPressed = false;
            sub.meleePressed = false;
            sub.takedownPressed = false;
            sub.look = Vec2{};
        }
        Substep(h, sub, world);
        remaining -= h;
        first = false;
    }
    MeleeTick(FMin(dt, 0.1f));
    SmoothMesh(FMin(dt, 0.1f));
    if (Is<Ground>(state)) meleeNoInput_ = false;

    if (!Is<Air>(state)) turnedInAir = false;

    // Turn animation: the clip's root rotation. Without the game's turn curves (None by default;
    // ported with them) it eases round to the right over turn180_time.
    if (turn_) {
        Turn t = *turn_;
        t.t += dt;
        const float u = FMin(t.t / tuning.turn180Time, 1.0f);
        const float angle = -kPi * Smoothstep(u);
        const bool done = u >= 1.0f;
        yaw = t.from + angle;
        if (done) turn_.reset();
        else turn_ = t;
    }
    // The move's look-at; ending the move ends it (TdMove.StopMove: AbortLookAtTarget).
    if (lookAt_) {
        LookAt la = *lookAt_;
        la.left -= dt;
        if (la.left < 0.0f || !Is<WallRun>(state)) {
            lookAt_.reset();
        } else {
            const float d = RemEuclid(la.yaw - yaw + kPi, 2.0f * kPi) - kPi;
            yaw += d * FMin(dt / la.interp, 1.0f);
            lookAt_ = la;
        }
    }
    yawRate_ = dt > 0.0f ? std::fabs(yaw - yawBefore) / dt : 0.0f;
    againstWall = Is<Ground>(state) ? CheckAgainstWall(world) : AgainstWall{};

    UpdateView(dt);

    if (tuning.killY && feet.y < *tuning.killY) {
        events.push_back({EventKind::Death});
        if (tuning.respawnOnDeath) Respawn();
    }
}

/// ATdPawn::CheckAgainstWall: two thin traces (2x2x5 uu) straight ahead from 68 uu above the
/// pawn's centre (26 crouched), 14 uu to either side, reaching max(40, 0.28 x forward speed) uu;
/// a hand goes up on a wall facing you (normal . forward < -0.5).
AgainstWall Controller::CheckAgainstWall(const World& world) const {
    const Vec3 fwd = ForwardOf(yaw);
    const Vec3 rgt = RightOf(yaw);
    const Vec3 centre = feet + Vec3::UnitY() * (Height() * 0.5f);
    const float up = crouched ? Uu(26.0f) : Uu(68.0f);
    const float reach = FMax(Uu(40.0f), vel.Dot(fwd) * 0.7f * 0.4f);
    const Vec3 half{Uu(2.0f), Uu(5.0f), Uu(2.0f)};
    const auto hand = [&](float side) -> std::optional<Vec3> {
        const Vec3 start = centre + Vec3::UnitY() * up + rgt * (Uu(14.0f) * side);
        const auto r = TraceBox(world, start, fwd, reach, half);
        if (r && r->normal.Dot(fwd) < -0.5f) return r->at;
        return std::nullopt;
    };
    const auto left = hand(-1.0f);
    const auto right = hand(1.0f);
    return AgainstWall{left, right};
}

/// Where Q (180 turn) works: walking on the ground (not crouched), in the air while moving the
/// way you face, and on the wall moves (which have their own turn moves).
bool Controller::CanTurn() const {
    if (Is<Ground>(state)) return !crouched;
    if (Is<Air>(state)) return Horiz(vel).NormalizeOrZero().Dot(ForwardOf(yaw)) >= 0.2f;
    return Is<WallClimb>(state) || Is<LedgeHang>(state) || Is<WallRun>(state);
}

void Controller::StartTurn() {
    // TdMove_WallRun.HandleMoveAction(MA_Turn): on a wallrun the view swings to look straight out
    // from the wall in 0.15 s, and you keep running.
    if (const auto* w = std::get_if<WallRun>(&state)) {
        const float want = std::atan2(-w->normal.x, -w->normal.z);
        lookAt_ = LookAt{want, 0.15f, 1.5f};
        wallTurned_ = true;
        events.push_back({EventKind::WallRunTurn});
        return;
    }
    TurnKind kind;
    if (Is<Air>(state)) kind = TurnKind::Air;
    else if (Is<WallClimb>(state)) kind = TurnKind::WallClimb;
    else if (HorizontalSpeed() > UuPerS(10.0f)) kind = TurnKind::Run;
    else kind = TurnKind::Stand;
    turn_ = Turn{yaw, 0.0f, kind};
    events.push_back({EventKind::Turn180});
    if (Is<Air>(state)) turnedInAir = true;
    if (const auto* c = std::get_if<WallClimb>(&state)) state = WallClimbTurned{c->normal, 0.0f};
}

/// In the game sprinting isn't a separate meter, it's the speed you've built up; so when a move
/// takes speed away, take the sprint charge down to match.
void Controller::SyncSprintCharge() {
    const Tuning& tu = tuning;
    const float frac = FClamp((HorizontalSpeed() - tu.runSpeed) / (tu.sprintSpeed - tu.runSpeed), 0.0f, 1.0f);
    sprintCharge = FMin(sprintCharge, frac);
    run_.energy = FMin(run_.energy, FMax(HorizontalSpeed() - tu.runSpeed, 0.0f));
}

/// How far up the body can move, up to `max` (the scripts' upward MovementTrace before a jump-off,
/// so a low ceiling shortens it).
float Controller::Headroom(const World& world, float max) const {
    Vec3 p = feet;
    MoveAxis(world, GetBody(), p, 1, max);
    return p.y - feet.y;
}

bool Controller::Grounded(const World& world) const {
    const float hw = tuning.halfWidth - 0.01f;
    const Aabb region = Aabb::New(feet + Vec3{-hw, -0.05f, -hw}, feet + Vec3{hw, 0.02f, hw});
    return !world.IsFree(region);
}

bool Controller::TryStand(const World& world) {
    if (!crouched) return true;
    const Body stand{tuning.halfWidth, tuning.standHeight};
    if (world.IsFree(stand.AabbAt(feet))) {
        crouched = false;
        return true;
    }
    return false;
}

/// In the air, crouch tucks the legs up (head stays put); releasing drops them.
void Controller::UpdateCoil(const Input& input, const World& world) {
    const Body stand{tuning.halfWidth, tuning.standHeight};
    const float lift = tuning.coilLift;
    // TdMove_Coil.CanDoMove: not from a plain fall (only out of a jump of some kind), and moving
    // forward along the view at CoilMinTriggerSpeed or more. Started by a crouch press in the air,
    // lasts while it's held.
    const bool canCoil = jumpedSinceGround_ && vel.Dot(ForwardOf(yaw)) >= tuning.coilMinSpeed;
    if (input.crouchPressed && input.crouchHeld && !coiled_ && !crouched && canCoil) {
        coiled_ = true;
        feet.y += lift;
    } else if (!input.crouchHeld && coiled_) {
        const Vec3 lowered = feet - Vec3::UnitY() * lift;
        if (world.IsFree(stand.AabbAt(lowered))) {
            feet = lowered;
            coiled_ = false;
        }
    } else if (!input.crouchHeld && crouched) {
        // Came off a slide still crouched: stand up by growing downward.
        const float diff = tuning.standHeight - tuning.crouchHeight;
        const Vec3 lowered = feet - Vec3::UnitY() * diff;
        if (world.IsFree(stand.AabbAt(lowered))) {
            feet = lowered;
            crouched = false;
        }
    }
}

/// Straighten tucked legs on touchdown (grow upward, crouch if there's no room).
void Controller::UncoilOnGround(const World& world) {
    if (coiled_) {
        coiled_ = false;
        const Body stand{tuning.halfWidth, tuning.standHeight};
        if (!world.IsFree(stand.AabbAt(feet))) crouched = true;
    }
}

void Controller::Substep(float dt, const Input& input, const World& world) {
    jumpBuffer_ = FMax(jumpBuffer_ - dt, 0.0f);
    rollTriggerAge_ += dt;
    regrabCooldown_ = FMax(regrabCooldown_ - dt, 0.0f);
    lowGrav_ = FMax(lowGrav_ - dt, 0.0f);
    fixtureCooldown_ = FMax(fixtureCooldown_ - dt, 0.0f);
    rumpRedo_ = FMax(rumpRedo_ - dt, 0.0f);
    vertigoRedo_ = FMax(vertigoRedo_ - dt, 0.0f);
    climbRedo_ = FMax(climbRedo_ - dt, 0.0f);
    if (dodging_ && !Is<Air>(state)) EndDodge();
    if (!dodging_) dodgeRedo_ = FMax(dodgeRedo_ - dt, 0.0f);

    if (Is<Ground>(state)) GroundState(dt, input, world);
    else if (Is<Air>(state)) AirState(dt, input, world);
    else if (const auto* s = std::get_if<Stunned>(&state)) StunnedState(dt, s->t, world);
    else if (const auto* w = std::get_if<WallRun>(&state)) WallRunState(dt, w->normal, w->t, input, world);
    else if (const auto* c = std::get_if<WallClimb>(&state)) WallClimbState(dt, c->normal, c->t, input, world);
    else if (const auto* k = std::get_if<WallClimbTurned>(&state)) WallClimbTurnedState(dt, k->normal, k->t, world);
    else UnportedState(state, dt, input, world);

    CheckBarbedWire(world);
    UpdateSoftBrace(world);

    const float y = BodyY();
    fallPeak_ = Is<Air>(state) ? FMax(fallPeak_, y) : y;
    if (!Is<Air>(state)) takeoff_ = Takeoff::Other;
    if (!Is<Ground>(state)) springboard_ = false;
}

// ---------------------------------------------------------------- ground

void Controller::GroundState(float dt, const Input& input, const World& world) {
    const Tuning& tu = tuning;
    if (!Grounded(world)) {
        state = Air{};
        airTime_ = 0.0f;
        jumpedSinceGround_ = false;
        return;
    }
    airTime_ = 0.0f;
    climbedThisAir_ = false;
    lastWallNormal_.reset();
    if (TryIntoClimb(input, world)) return;

    // On ground too steep to stand on: sliding down it.
    if (TryRumpSlide(world)) return;
    if (!crouched && TryBalance(world)) return;

    // Slide: TdMove_Slide.CanDoMove wants 350 uu/s along the way you face.
    const float forwardSpeed = vel.Dot(ForwardOf(yaw));
    if (input.crouchPressed && forwardSpeed >= tu.slideMinSpeed && !crouched) {
        crouched = true;
        slideYaw = yaw;
        state = Slide{0.0f};
        events.push_back({EventKind::Slide});
        return;
    }
    if (input.crouchHeld) crouched = true;
    else TryStand(world);
    // Walking into something knee-high: step up onto it (when the host turns it on).
    if (tu.autoStepUp && !crouched && input.moveAxis.y > 0.2f && TryAutoStepUp(world)) return;

    if (springboard_) {
        SpringboardRunIn(dt, world);
        return;
    }

    // Dodge / springboard / jump, in TdPlayerMoveManager's order (moves 33, 7, 11).
    if (jumpBuffer_ > 0.0f) {
        if (std::fabs(input.strafeRaw) > tu.dodgeStrafeThreshold && dodgeRedo_ <= 0.0f && !crouched && TryStand(world)) {
            DodgeJump(Signum(input.strafeRaw));
            Integrate(dt, world, false);
            return;
        }
        if (input.moveAxis.y > 0.3f && TrySpringboard(world)) {
            jumpBuffer_ = 0.0f;
            return;
        }
        if (TryStand(world)) {
            GroundJump();
            // Don't lose this substep's movement on takeoff.
            Integrate(dt, world, false);
            return;
        }
    } else if (input.jumpHeld && input.moveAxis.y > 0.3f && TrySpringboard(world)) {
        // Holding jump while running springboards by itself when you reach the obstacle.
        return;
    }

    // Running: Mirror's Edge's own ground movement (locomotion). The controller asks for sprint
    // or walk acceleration, CalcVelocity applies it with friction or braking, capped at
    // GroundSpeed x the move's SpeedModifier.
    const bool attacking = melee_ && (*melee_ == MeleeKind::Punch || *melee_ == MeleeKind::Crouch);
    const float speedMod = crouched ? tu.crouchSpeedModifier : attacking ? tu.meleeSpeed : 1.0f;
    const RunInput ri{ForwardOf(yaw), RightOf(yaw), input.moveAxis.y, input.moveAxis.x, frameTurn_};
    Vec3 h = Horiz(vel);
    const Vec3 accel = tu.loco.ControllerAccel(run_, ri, h, dt);
    const Vec3 h2 = tu.loco.CalcVelocity(h, accel, dt, speedMod, tu.loco.groundFriction);
    vel = Vec3{h2.x, 0.0f, h2.z};
    sprintCharge = FClamp(run_.energy / (tu.loco.groundSpeed - tu.loco.maxBase), 0.0f, 1.0f);

    // The walking edge check: a long drop ahead may stop her (TdMove_Vertigo).
    if (TryVertigo(world)) return;
    Integrate(dt, world, true);
}

/// Jump off the ground (TdMove_Jump.StartMove): remember the speed you jumped with, and add
/// JumpAddXY along the way you face if you're already moving that way.
void Controller::GroundJump() {
    const Vec3 fwd = ForwardOf(yaw);
    preJumpMomentum_ = HorizontalSpeed();
    if (fwd.Dot(vel) > UuPerS(10.0f)) vel += fwd * tuning.jumpAddForward;
    Jump(tuning.jumpSpeed);
    takeoff_ = Takeoff::Jump;
}

/// TdMove_DodgeJump.StartMove: JumpAddXY straight out to the side of where you face, plus
/// DodgeJumpInertiaConservation of your velocity, BaseJumpZ up.
void Controller::DodgeJump(float side) {
    const Vec3 dir = RightOf(yaw) * side;
    Dodge(dir, tuning.dodgeSide, tuning.dodgeUp, tuning.dodgeInertia);
    dodging_ = true;
    Event e{EventKind::Dodge};
    e.dir = dir;
    events.push_back(e);
}

/// Out of TdMove_DodgeJump; RedoMoveTime starts.
void Controller::EndDodge() {
    dodging_ = false;
    dodgeRedo_ = tuning.dodgeRedo;
}

/// ME's dodge jumps: keep a fraction of the old velocity, add a sideways launch.
void Controller::Dodge(Vec3 dir, float side, float up, float inertia) {
    lastJumpAt_ = feet;
    const Vec3 keep = Horiz(vel) * inertia;
    const Vec3 h = keep + dir * side;
    vel = Vec3{h.x, up, h.z};
    state = Air{};
    airTime_ = 0.0f;
    jumpedSinceGround_ = true;
    jumpBuffer_ = 0.0f;
}

void Controller::Jump(float speed) {
    lastJumpAt_ = feet;
    vel.y = speed;
    state = Air{};
    airTime_ = 0.0f;
    jumpedSinceGround_ = true;
    jumpBuffer_ = 0.0f;
    events.push_back({EventKind::Jump});
}

// ---------------------------------------------------------------- air

void Controller::AirState(float dt, const Input& input, const World& world) {
    // TdLadderVolume.PawnUpdate.
    if (TryIntoClimb(input, world)) return;
    // Coming down on ground too steep to stand on: the rump slide.
    if (vel.y <= 0.0f && TryRumpSlide(world)) return;
    const Tuning& tu = tuning;
    airTime_ += dt;

    if (dodging_) {
        if (vel.y < -tu.dodgeExitFallSpeed) {
            EndDodge();
        } else {
            // TdMove_DodgeJump: falling physics with no air control and nothing to catch.
            vel.y -= tu.gravity * dt;
            Integrate(dt, world, false);
            return;
        }
    }

    // Coyote jump.
    if (jumpBuffer_ > 0.0f && !jumpedSinceGround_ && airTime_ < tu.coyoteTime) {
        if (!crouched && !coiled_) GroundJump();
    }

    UpdateCoil(input, world);

    if (AirTransitions(input, world)) return;

    // Air control, as the game does it: the controller still asks for sprint acceleration, and
    // APawn::physFalling clamps it to AccelRate x AirControl. Below 10 uu/s it may make up the
    // difference; at or past GroundSpeed with AirControl under 0.05 it allows only 1 uu/s^2.
    const RunInput ri{ForwardOf(yaw), RightOf(yaw), input.moveAxis.y, input.moveAxis.x, frameTurn_};
    const Vec3 h = Horiz(vel);
    const Vec3 want = Horiz(tu.loco.SprintAccel(run_, ri, h, dt, true));
    const float speed = h.Length();
    float max = tu.airAccel;
    if (speed < 0.1f && tu.airAccel > 0.0f && dt > 0.0f) {
        max += (0.1f - speed) / dt;
    } else if (speed >= tu.loco.groundSpeed && tu.airAccel <= 0.05f * tu.loco.accelRate) {
        max = 0.01f;
    }
    const Vec3 accel = ClampLengthMax(want, max);
    const Vec3 nh = h + accel * dt;
    const float g = lowGrav_ > 0.0f ? tu.gravity * lowGravK_ : tu.gravity;
    vel = Vec3{nh.x, vel.y - g * dt, nh.z};

    Integrate(dt, world, false);
}

/// TdMove_SoftLanding: falling faster than SoftLandingZSpeedThreshold with a soft landing object
/// below, brace for it.
void Controller::UpdateSoftBrace(const World& world) {
    softBrace = Is<Air>(state) && vel.y < -tuning.softBraceSpeed && SoftBelow(world, 50.0f);
}

/// Checks for grabbing, climbing and wallrunning from the air.
bool Controller::AirTransitions(const Input& input, const World& world) {
    const Tuning& tu = tuning;
    const Body body = GetBody();
    const Vec3 fwd = ForwardOf(yaw);
    const Vec3 h = Horiz(vel);
    const float speed = h.Length();

    if (TryGrabFixture(world)) return true;

    // TdMove_Jump / TdMove_Falling bCheckForVaultOver: holding forward (the stick past 0.8),
    // vault or step up onto whatever's ahead.
    if (input.moveAxis.y > 0.8f && TryVault(input, world)) return true;

    // Wallclimb: TdMove_WallClimb's native start check. Forward past 0.8, still rising, moving
    // forward, facing the wall within WallClimbingVerticalStartAngle, the wall at least
    // MinWallHeight above the feet, and wall right in front of the face.
    if (input.moveAxis.y > 0.8f && !climbedThisAir_ && !coiled_ && jumpedSinceGround_ && vel.y > 0.0f && h.Dot(fwd) >= 0.0f) {
        if (const auto hit = TraceWall(world, body, feet, fwd, Uu(8.0f), tu.standHeight - Uu(64.0f))) {
            const Vec3 n = hit->n;
            const float gap = hit->gap;
            // The same wall carries on up to WallClimbingMinWallHeight.
            const auto upper = TraceWall(world, body, feet, -n, gap + 0.1f, tu.wallclimbMinWallHeight - 0.02f);
            const bool tall = upper && upper->n.Dot(n) > 0.9f;
            if (fwd.Dot(-n) >= std::cos(tu.wallclimbMaxAngleDeg * (kPi / 180.0f)) && tall) {
                // TdMove_WallClimb.ReachedWall: the boost grows with how far past run speed you came
                // in (less the jump's JumpAddXY) and how fast you were still rising.
                const float into = speed - tu.jumpAddForward;
                const float xy = FClamp(FMax(into - tu.runSpeed, 0.0f) / (tu.wallclimbAddXyMaxSpeed - tu.runSpeed), 0.0f, 1.0f);
                const float z = FClamp(vel.y / tu.wallclimbAddZMaxSpeed, 0.0f, 1.0f);
                const float height = tu.wallclimbAddXyHeight * xy + tu.wallclimbAddZHeight * z;
                vel = Vec3::UnitY() * std::sqrt(4.0f * height * tu.wallclimbGravity);
                feet += -n * gap;
                climbedThisAir_ = true;
                state = WallClimb{n, 0.0f};
                jumpBuffer_ = 0.0f;
                events.push_back({EventKind::WallClimbStart});
                return true;
            }
        }
    }

    // Wall directly ahead?
    if (const auto p = ProbeWall(world, body, feet, fwd, 0.35f, 0.2f, body.height - 0.1f)) {
        const float facing = fwd.Dot(-p->n);
        if (facing > 0.5f) {
            // Ledge grab.
            if (regrabCooldown_ <= 0.0f && vel.y < 2.5f) {
                const float lift = coiled_ ? tu.coilLift : 0.0f;
                const float hi = tu.hangHandsAboveFeet + tu.ledgeGrabHiExtra - lift;
                if (const auto top = FindLedge(world, body, feet, p->n, tu.mantleHi - lift, hi, tu.crouchHeight)) {
                    GrabLedge(p->n, p->gap, *top, world);
                    return true;
                }
            }
        }
    }

    // Wallrun (TdMove_WallRun.CanDoMove): at least WallRunningMinSpeed, not moving backwards
    // relative to the view, and a wall from FindWallSide/FindWallForward.
    if (speed >= tu.wallrunMinSpeed && h.Dot(fwd) >= 0.0f && input.moveAxis.y > 0.3f && vel.y > -tu.wallrunStopFallSpeed && !crouched && !coiled_) {
        if (const auto w = FindWallrun(input, world)) {
            const Vec3 n = w->n;
            feet += -n * w->gap;
            // TdMove_WallRun.StartMove. A wallrun straight out of the last one's jump-off is
            // chained: it gets no lift, and pulls harder the more of them in a row.
            const bool chained = takeoff_ == Takeoff::WallJump;
            chainWallruns_ = chained ? chainWallruns_ + 1 : 0;
            const float accel = tu.wallrunAccel * static_cast<float>(1 + chainWallruns_);
            // Entry speed is what you came in with, at least WallRunningVelocityStartLimit if
            // you're still rising.
            const float begin = vel.y > 0.0f ? FMax(speed, tu.wallrunMaxStartUp) : speed;
            // Rising: lift you to WallRunningHorisontalInitialZHeight above where you jumped (less
            // any ceiling), with the wallrun's own gravity. Falling: keep falling.
            float vy;
            if (vel.y > 0.0f && !chained) {
                const float rise = Headroom(world, tu.wallrunRiseHeight) - (feet.y - lastJumpAt_.y);
                vy = std::sqrt(2.0f * FMax(rise, 0.0f) * accel);
            } else {
                vy = vel.y;
            }
            const Vec3 along = (h - n * h.Dot(n)).NormalizeOrZero();
            vel = along * begin + Vec3::UnitY() * vy;
            state = WallRun{n, 0.0f};
            wallTurned_ = false;
            jumpBuffer_ = 0.0f;
            lastWallNormal_ = n;
            // TdMove_WallRun.StartMove: a box (the cylinder's radius, 10 uu high) from 30 uu over
            // her head, WallRunningStrafeCheckDistance (50 uu) into the wall.
            const Vec3 over = feet + Vec3::UnitY() * (Height() + Uu(30.0f) + Uu(10.0f));
            const Vec3 half{tu.halfWidth, Uu(10.0f), tu.halfWidth};
            wallrunWallAbove = world.Sweep(half, over, -n * Uu(50.0f)).has_value();
            events.push_back({EventKind::WallRunStart});
            Integrate(SUBSTEP, world, false);
            return true;
        }
    }
    return false;
}

void Controller::GrabLedge(Vec3 n, float gap, float top, const World& world) {
    const Tuning& tu = tuning;
    // Hang with full body height.
    crouched = false;
    coiled_ = false;
    feet += -n * gap;
    feet.y = top - tu.hangHandsAboveFeet;
    // Don't hang inside something below us.
    if (!world.IsFree(GetBody().AabbAt(feet))) {
        feet.y = top - tu.hangHandsAboveFeet + 0.4f;
        crouched = true;
    }
    grabSpeed = vel.y;
    vel = Vec3::Zero();
    state = LedgeHang{n, top, false};
    hangTime = 0.0f;
    jumpBuffer_ = 0.0f;
    events.push_back({EventKind::LedgeGrab});
}

void Controller::Land(float impact, const World& world) {
    const Tuning& tu = tuning;
    climbedThisAir_ = false;
    lastWallNormal_.reset();
    const float fall = fallPeak_ - BodyY();
    UncoilOnGround(world);

    if (fall >= tu.lethalFallHeight) {
        events.push_back({EventKind::Death});
        if (tu.respawnOnDeath) {
            Respawn();
            return;
        }
    }
    // TdMove_Landing.StartMove: out of a 180 in the air you're moving backwards, so no roll, and
    // LandBackwards beats a hard landing: dead stop, on your back (TdMove_LayOnGround).
    if (turnedInAir) {
        turnedInAir = false;
        vel = Vec3::Zero();
        sprintCharge = 0.0f;
        run_ = RunState{};
        crouched = false;
        state = LayOnGround{0.0f, std::nullopt, false};
        events.push_back({EventKind::LandOnBack});
        return;
    }
    // TdPawn.CanSkillRoll: crouch pressed in the last 0.2 s, and not out of a turn in the air or
    // an air kick.
    const bool airKick = melee_ && *melee_ == MeleeKind::AirKick;
    const bool canRoll = rollTriggerAge_ < tu.rollWindow && !turn_ && !airKick;
    if (canRoll && fall >= tu.rollHeight) {
        crouched = true;
        const Vec3 dir = NormalizeOr(Horiz(vel), ForwardOf(yaw));
        const float s = FMax(HorizontalSpeed(), tu.runSpeed);
        vel = dir * s;
        state = Roll{0.0f};
        rollTriggerAge_ = std::numeric_limits<float>::infinity();
        events.push_back({EventKind::Roll});
        return;
    }
    if (fall >= tu.hardLandHeight && SoftBelow(world, 0.1f)) {
        // TdMove_Landing.StartMove: a hard landing on something soft is LandOnSoftObject.
        vel = Vec3::Zero();
        sprintCharge = 0.0f;
        state = SoftLand{0.0f};
        events.push_back({EventKind::SoftLand});
        return;
    }
    if (fall >= tu.hardLandHeight) {
        // TdMove_Landing.LandHard: dead stop, no input until the animation ends.
        vel = Vec3::Zero();
        sprintCharge = 0.0f;
        state = Stunned{0.0f};
        events.push_back({EventKind::HardLand});
        return;
    }
    // TdMove_Landing.SubtractLandingSpeed: landing a plain jump caps your speed at what you jumped
    // with, less LandingSpeedReduction (floored at zero).
    if (takeoff_ == Takeoff::Jump) {
        const float cap = FMax(preJumpMomentum_ - tu.landingSpeedReduction, 0.0f);
        const Vec3 h = Horiz(vel);
        if (h.Length() > cap) {
            const Vec3 h2 = h.NormalizeOrZero() * cap;
            vel = Vec3{h2.x, vel.y, h2.z};
            SyncSprintCharge();
        }
    }
    Event e{EventKind::Land};
    e.impact = impact;
    e.fall = fall;
    events.push_back(e);
    state = Ground{};
}

void Controller::StunnedState(float dt, float t, const World& world) {
    t = t + dt;
    // LandHard zeroes the velocity and ignores move input until the animation ends.
    vel = Vec3{0.0f, FMin(vel.y, 0.0f), 0.0f};
    if (t > tuning.hardLandStun) state = Ground{};
    else state = Stunned{t};
    Integrate(dt, world, true);
}

void Controller::Integrate(float dt, const World& world, bool onGround) {
    const float stepHeight = tuning.stepHeight;
    const Body body = GetBody();
    const Vec3 d = vel * dt;
    const Vec3 start = feet;

    // Horizontal, sliding along what we run into, with step-up when on the ground.
    Vec3 a = start;
    std::vector<Vec3> hit = SlideMove(world, body, a, d);
    if (onGround && !hit.empty()) {
        Vec3 s = start;
        MoveAxis(world, body, s, 1, stepHeight);
        std::vector<Vec3> stepped = SlideMove(world, body, s, d);
        MoveAxis(world, body, s, 1, -stepHeight - 0.01f);
        const float da = Horiz(a - start).LengthSquared();
        const float ds = Horiz(s - start).LengthSquared();
        if (ds > da + 1e-6f && s.y >= start.y - 1e-3f) {
            // A step up: the mesh stays where it was and follows (OffsetMeshZ).
            OffsetMesh(start.y - s.y);
            a = s;
            hit = std::move(stepped);
        }
    }
    feet = a;
    // What we ran into takes the velocity going into it.
    for (const Vec3& n : hit) {
        const float into = vel.Dot(n);
        if (into < 0.0f) vel -= n * into;
    }

    // Vertical.
    const float vyBefore = vel.y;
    const AxisHit hy = MoveAxis(world, body, feet, 1, d.y);
    if (hy.blocked) {
        vel.y = 0.0f;
        if (d.y < 0.0f && !onGround) Land(-vyBefore, world);
    }

    // Walked into a step with no front to it (some games' stairs are only their tops) or over a
    // seam: onto its top, as the step-up would have, if there's room.
    if (onGround) {
        const float hw = body.halfWidth - 0.02f;
        // Where she is, and where she was going if something stopped her.
        const Vec3 want = start + Vec3{d.x, 0.0f, d.z} + Horiz(d).NormalizeOrZero() * 0.03f;
        const bool shortOf = Horiz(want - feet).Length() > 1e-4f;
        std::vector<Vec3> ats{feet};
        if (shortOf) ats.push_back(want);
        for (const Vec3& at : ats) {
            const auto tops = TopsBelow(world, at, hw, start.y + stepHeight, feet.y + 0.005f);
            if (tops.empty()) continue;
            const float top = tops.front();
            const Vec3 up{at.x, top, at.z};
            if (world.IsFree(body.AabbAt(up + Vec3::UnitY() * 0.001f))) {
                OffsetMesh(feet.y - top);
                feet = up;
                break;
            }
        }
    }

    // Stick to slopes/stairs going down.
    if (onGround && !Grounded(world)) {
        Vec3 s = feet;
        const AxisHit h = MoveAxis(world, body, s, 1, -stepHeight);
        if (h.blocked) {
            OffsetMesh(feet.y - s.y);
            feet = s;
        }
    }
}

/// ATdPawn::OffsetMeshZ: the mesh shifted by `d`, kept within 24 uu of where it belongs.
void Controller::OffsetMesh(float d) { meshOffset = FClamp(meshOffset + d, -Uu(24.0f), Uu(24.0f)); }

/// ATdPawn's mesh smoothing tick: the mesh's height comes back to the pawn at ten times the gap a
/// second, never slower than 1 uu a tick.
void Controller::SmoothMesh(float dt) {
    const float gap = std::fabs(meshOffset);
    if (gap > 0.0f) {
        const float step = FMin(FMax(gap * dt * 10.0f, Uu(1.0f)), gap);
        meshOffset -= Signum(meshOffset) * step;
    }
}

void Controller::UpdateView(float dt) {
    const Tuning& tu = tuning;
    // In the air with legs tucked, the head didn't move.
    const float targetEye = coiled_ ? tu.standHeight - tu.eyeFromTop - tu.coilLift : Height() - tu.eyeFromTop;
    eyeHeight_ += (targetEye - eyeHeight_) * (1.0f - std::exp(-14.0f * dt));
}

View Controller::GetView() const { return View{feet + Vec3::UnitY() * eyeHeight_, yaw, pitch, 0.0f, 90.0f}; }

Vec3 Controller::Root() const { return Vec3{feet.x, BodyY(), feet.z}; }

// ---------------------------------------------------------------- walls

/// The wall search in TdMove_WallRun.CanDoMove: the native FindWallSide when you're strafing
/// toward a wall right as you jump, otherwise FindWallForward.
std::optional<WallFound> Controller::FindWallrun(const Input& input, const World& world) const {
    const Tuning& tu = tuning;
    const Body body = GetBody();
    const Vec3 fwd = ForwardOf(yaw);
    const Vec3 h = Horiz(vel);
    const float speed = h.Length();
    // Both look for wall at MinWallHeight - 2 above the feet.
    const float top = tu.wallrunMinWallHeight - Uu(2.0f);
    const auto sameWall = [&](Vec3 n) { return lastWallNormal_ && lastWallNormal_->Dot(n) > 0.9f; };
    // Which way along the wall you'd run: the tangent on the side you face.
    const auto along = [&](Vec3 n) {
        const Vec3 t{n.z, 0.0f, -n.x};
        return t.Dot(fwd) < 0.0f ? -t : t;
    };

    // MoveActionHint left/right only counts within 30 units of LastJumpLocation.
    const bool sideHint = (feet - lastJumpAt_).Length() < Uu(30.0f) && std::fabs(input.moveAxis.x) > 0.3f;
    const auto side = [&]() -> std::optional<WallHit> {
        const Vec3 dir = RightOf(yaw) * Signum(input.moveAxis.x);
        const auto hi = TraceWall(world, body, feet, dir, tu.wallrunReach, top);
        if (!hi) return std::nullopt;
        if (std::fabs(hi->n.Dot(dir)) < std::cos(tu.wallrunStrafeStartDeg * (kPi / 180.0f)) || sameWall(hi->n)) return std::nullopt;
        if (vel.Dot(along(hi->n)) < 0.0f) return std::nullopt;
        // The same wall again just above step height, and near enough vertical between the two
        // (the direction from one hit to the other at least 0.96 up).
        const auto lo = TraceWall(world, body, feet, dir, tu.wallrunReach, tu.stepHeight + Uu(2.0f));
        if (!lo) return std::nullopt;
        const float rise = top - tu.stepHeight - Uu(2.0f);
        const float off = std::fabs((hi->at - lo->at).Dot(hi->n));
        if (lo->n.Dot(hi->n) > 0.999f && rise / std::sqrt(rise * rise + off * off) >= 0.96f) return hi;
        return std::nullopt;
    };
    const auto ahead = [&]() -> std::optional<WallHit> {
        // The reach grows from WallRunningForwardCheckDistance at SpeedMaxBase to
        // ContextMoveDistanceMultiplier times it at GroundSpeed.
        const Locomotion& l = tu.loco;
        const float frac = FMax(speed - l.maxBase, 0.0f) / (l.groundSpeed - l.maxBase);
        const float reach = ((tu.contextMoveDistanceMultiplier - 1.0f) * frac + 1.0f) * tu.wallrunReach;
        const auto mid = TraceWall(world, body, feet, fwd, reach, body.height * 0.5f);
        if (!mid) return std::nullopt;
        // Facing at most WallRunningForwardMaxStartAngle into the wall (MinStartAngle is 0).
        const float d = -mid->n.Dot(fwd);
        if (d > std::cos((90.0f - tu.wallrunMaxApproachDeg) * (kPi / 180.0f)) || sameWall(mid->n)) return std::nullopt;
        // Tall enough: the wall again up at MinWallHeight, 50 units lower the squarer you face it.
        if (!TraceWall(world, body, feet, fwd, reach, top - (1.0f - d) * Uu(50.0f))) return std::nullopt;
        if (vel.Dot(along(mid->n)) < 0.0f) return std::nullopt;
        return mid;
    };
    std::optional<WallHit> hit;
    if (sideHint) {
        hit = side();
        if (!hit) hit = ahead();
    } else {
        hit = ahead();
    }
    if (!hit) return std::nullopt;

    // Back in CanDoMove: you have to be moving into the wall.
    if (h.Dot(hit->n) >= 0.0f) return std::nullopt;
    // Falling: no wallrun if there's ground just under where the run would take you (a cylinder
    // height below the feet, half a second of travel ahead, 45 units down).
    if (vel.y < 0.0f) {
        const Vec3 start = Vec3{hit->at.x, feet.y - body.height * 0.5f, hit->at.z} + hit->n * body.halfWidth;
        const Vec3 end = start + along(hit->n) * speed * 0.5f - Vec3::UnitY() * Uu(45.0f);
        const auto steps = static_cast<std::size_t>(FMax(std::ceil((end - start).Length() / 0.05f), 1.0f));
        for (std::size_t i = 0; i <= steps; ++i) {
            // glam's lerp: self * (1 - s) + rhs * s.
            const float s = static_cast<float>(i) / static_cast<float>(steps);
            const Vec3 p = start * (1.0f - s) + end * s;
            if (!world.IsFree(Aabb::FromCenterSize(p, Vec3::Splat(Uu(2.0f))))) return std::nullopt;
        }
    }
    return hit;
}

void Controller::WallRunState(float dt, Vec3 n, float t, const Input& input, const World& world) {
    if (TryIntoClimb(input, world)) return;
    const Tuning& tu = tuning;
    t = t + dt;
    const Body body = GetBody();
    const Vec3 h = Horiz(vel);
    const Vec3 alongDir = (h - n * h.Dot(n)).NormalizeOrZero();
    float speed = h.Dot(alongDir);

    const Vec3 strafeDir = RightOf(yaw) * Signum(input.moveAxis.x);
    if (jumpBuffer_ > 0.0f && t > 0.1f && std::fabs(input.moveAxis.x) >= tu.wallrunDodgeThreshold && strafeDir.Dot(n) > 0.0f) {
        Dodge(strafeDir, tu.wallrunDodgeSide, tu.wallrunDodgeUp, tu.wallrunDodgeInertia);
        lastWallNormal_ = n;
        Event e{EventKind::WallRunDodge};
        e.dir = strafeDir;
        events.push_back(e);
        return;
    }
    const float chain = static_cast<float>(1 + chainWallruns_);
    if (jumpBuffer_ > 0.0f && t > 0.1f) {
        // TdMove_WallrunJump.StartMove. `push` is how far you look away from the wall: it pushes
        // you out harder and higher, and trades away along-wall speed. After Q (bTurned90FromWall)
        // it takes the wall normal itself: the full push-off.
        const float push = wallTurned_ ? 1.0f : FMax(ForwardOf(yaw).Dot(n), 0.0f);
        const float height = Headroom(world, tu.wallrunJumpHeight + tu.wallrunJumpHeightTurnedAdd * push);
        const float up = tu.SpeedForHeight(height) / chain;
        const float out = tu.wallrunJumpOut + tu.wallrunJumpOutLookAdd * push;
        const float keep = tu.wallrunJumpForwardMin + (1.0f - tu.wallrunJumpForwardMin) * (1.0f - push);
        const Vec3 along = h - n * h.Dot(n);
        vel = n * out + along * keep + Vec3::UnitY() * up;
        lastJumpAt_ = feet;
        state = Air{};
        airTime_ = 0.0f;
        jumpedSinceGround_ = true;
        jumpBuffer_ = 0.0f;
        takeoff_ = Takeoff::WallJump;
        events.push_back({EventKind::WallJump});
        return;
    }

    const auto probe = ProbeWall(world, body, feet, -n, 0.12f, 0.3f, 1.5f);
    const bool stillWall = probe && probe->n.Dot(n) > 0.9f;
    const bool fallingTooFast = vel.y < -tu.wallrunStopFallSpeed;
    if (fallingTooFast || !stillWall || input.crouchPressed || input.moveAxis.y < -0.3f) {
        state = Air{};
        return;
    }

    // UTdMove_WallRun's per-frame update and ATdPawn::physWallRunning: no acceleration asked for;
    // the velocity is kept along the wall with a slight drag, and you're pulled down at
    // WallRunningHorisontalAcceleration while rising and ...Deceleration while falling, both
    // x (1 + ConsequtiveWallruns).
    speed = speed - speed * tu.wallrunFriction * dt;
    if (speed < 0.1f) {
        state = Air{};
        return;
    }
    const float pull = (vel.y > 0.0f ? tu.wallrunAccel : tu.wallrunDecel) * chain;
    vel = alongDir * speed - n * 0.5f + Vec3::UnitY() * (vel.y - pull * dt);
    state = WallRun{n, t};
    Integrate(dt, world, false);
}

void Controller::WallClimbState(float dt, Vec3 n, float t, const Input& input, const World& world) {
    const Tuning& tu = tuning;
    t = t + dt;
    const Body body = GetBody();

    if (jumpBuffer_ > 0.0f && std::fabs(input.moveAxis.x) >= tu.wallclimbDodgeThreshold) {
        // Sideways along the wall face.
        Vec3 along = Vec3{n.z, 0.0f, -n.x} * Signum(input.moveAxis.x);
        if (along.Dot(RightOf(yaw) * Signum(input.moveAxis.x)) < 0.0f) along = -along;
        Dodge(along, tu.wallclimbDodgeSide, tu.wallclimbDodgeUp, tu.wallclimbDodgeInertia);
        vel += n * 0.5f;   // off the wall slightly
        lastWallNormal_ = n;
        Event e{EventKind::WallClimbDodge};
        e.dir = along;
        events.push_back(e);
        return;
    }

    const float hi = tu.hangHandsAboveFeet + tu.ledgeGrabHiExtra;
    if (const auto top = FindLedge(world, body, feet, n, 0.9f, hi, tu.crouchHeight)) {
        GrabLedge(n, 0.0f, *top, world);
        return;
    }
    const bool stillWall = ProbeWall(world, body, feet, -n, 0.12f, 0.2f, body.height - 0.1f).has_value();
    if (vel.y <= 0.0f || !stillWall) {
        state = Air{};
        return;
    }
    // UTdMove_WallClimb's per-frame update: the pawn's acceleration is just (0, 0,
    // -WallClimbingGravity), with no drag on the climb itself.
    const float vy = vel.y - tu.wallclimbGravity * dt;
    vel = -n * 0.3f + Vec3::UnitY() * vy;
    state = WallClimb{n, t};
    Integrate(dt, world, false);
}

void Controller::WallClimbTurnedState(float dt, Vec3 n, float t, const World& world) {
    const Tuning& tu = tuning;
    t = t + dt;
    if (jumpBuffer_ > 0.0f) {
        // TdMove_WallClimb180TurnJump.JumpFromWall (a ceiling cuts the height).
        vel = n * tu.wallkickOut + Vec3::UnitY() * tu.SpeedForHeight(Headroom(world, tu.wallkickHeight));
        lastJumpAt_ = feet;
        state = Air{};
        airTime_ = 0.0f;
        jumpedSinceGround_ = true;
        jumpBuffer_ = 0.0f;
        lastWallNormal_ = n;
        events.push_back({EventKind::WallKick});
        return;
    }
    if (t > tu.wallkickWindow) {
        state = Air{};
        return;
    }
    // Slowly slide down the wall while we decide.
    vel = -n * 0.3f + Vec3::UnitY() * (FMin(vel.y, 0.5f) - tu.gravity * 0.25f * dt);
    state = WallClimbTurned{n, t};
    Integrate(dt, world, false);
}

/// Melee's speed log: a quarter second of (dt, speed) samples, kept every frame. The attacks
/// themselves are ported later; none can start until then.
void Controller::MeleeTick(float dt) {
    speedLog_.push_back({dt, vel.Length()});
    float kept = 0.0f;
    size_t n = 0;
    for (auto it = speedLog_.rbegin(); it != speedLog_.rend(); ++it) {
        kept += it->first;
        n += 1;
        if (kept >= 0.25f) break;
    }
    while (speedLog_.size() > n) speedLog_.pop_front();
    if (melee_) throw NotPorted("melee_tick (an attack in progress)");
}

} // namespace runline
