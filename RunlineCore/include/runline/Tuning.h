#pragma once
// Every number that affects feel. Port of faith_move::tuning (generated field by field from
// tuning.rs, then reviewed). Values marked `ME:` are Mirror's Edge's own (read from TdGame.u);
// `guess` ones live in native code or animation data.
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "runline/Locomotion.h"

namespace runline {

/// Unreal units (cm, or cm/s) to metres.
constexpr float Uu(float v) { return v / 100.0f; }
/// std::f32::consts::TAU and PI.
inline constexpr float kTau = 6.28318530717958647692f;
inline constexpr float kPi = 3.14159265358979323846f;

/// How long each attack's clips play (seconds, at the rate the move plays them). Set from the
/// animations when they're loaded; these stand in without them.
struct MeleeClips {
    /// TdMove_Melee: MeleeStart, MeleeHit, MeleeMissed (Left), at 1.5 x.
    float punchStart = 0.25f;
    float punchHit = 0.45f;
    float punchMissed = 0.45f;
    /// TdMove_MeleeCrouch: MeleeCrouchStart, MeleeCrouchHit.
    float crouchStart = 0.4f;
    float crouchHit = 0.5f;
    /// TdMove_MeleeAir: MeleeInAir, MeleeInAirStill, MeleeFromAbove, MeleeInAirHit.
    float air = 0.8f;
    float airStill = 0.8f;
    float airFromAbove = 0.8f;
    float airHit = 0.6f;
    /// TdMove_MeleeSlide: MeleeSlide. TdMove_MeleeWallrun: MeleeWallRunLeft.
    float slide = 0.9f;
    float wallrun = 0.7f;
    /// TdMove_MeleeVault: MeleeVaultOver.
    float vaultKick = 0.8f;
};

struct Tuning {
    // ---------------------------------------------------------------- body
    /// ME: TdPawn CollisionCylinder.CollisionRadius = 30
    float halfWidth = Uu(30.0f);
    /// ME: CollisionCylinder.CollisionHeight = 90 (half-height) -> 1.8 m
    float standHeight = Uu(180.0f);
    /// ME: TdMove.ShrinkCollision: crouching and sliding the cylinder is 122 units tall.
    float crouchHeight = Uu(122.0f);
    /// ME: TdPawn.BaseEyeHeight = 76 above the cylinder centre -> eye at 1.66 m
    float eyeFromTop = Uu(180.0f - (90.0f + 76.0f));
    /// ME: TdPawn.MaxWallStepHeight = 35
    float stepHeight = Uu(35.0f);

    // ---------------------------------------------------------------- ground
    /// ME: TdPawn.SpeedMaxBaseVelocity = 400: running speed; above it is sprint
    float runSpeed = Uu(400.0f);
    /// ME: TdPawn.GroundSpeed = 720: full sprint
    float sprintSpeed = Uu(720.0f);
    /// ME: the ground movement itself (sprint/walk acceleration, CalcVelocity), ported from
    /// the game's native code: see locomotion.rs
    Locomotion loco = {};
    /// ME: TdMove_Crouch.SpeedModifier = 0.2 (x GroundSpeed)
    float crouchSpeedModifier = 0.2f;
    /// ME: TdPawn.AccelRate = 6144
    float groundAccel = Uu(6144.0f);
    /// guess
    float groundDecel = 22.0f;

    // ---------------------------------------------------------------- air
    /// ME: the gravity Faith actually falls under. DefaultGame.ini sets the
    /// world's DefaultGravityZ = -800, but its own comment on TdPawn says
    /// "1600 is the downward speed after falling 780 cm", i.e.
    /// 1600^2 / (2 x 780) ~ 1641 uu/s^2: the pawn falls at about twice the world
    /// value (the doubling happens in native code). We use the measured one.
    float gravity = Uu(1600.0f * 1600.0f / (2.0f * 780.0f));
    /// ME: TdMove_Jump.BaseJumpZ = 630
    float jumpSpeed = Uu(630.0f);
    /// ME: TdMove_Jump.JumpAddXY = 100: extra forward speed on takeoff
    float jumpAddForward = Uu(100.0f);
    /// ME: TdPawn.AirControl (0.025) x AccelRate (6144)
    float airAccel = 0.025f * Uu(6144.0f);
    /// guess
    float coyoteTime = 0.12f;
    /// ME: TdPlayerController.JumpTapTime = 0.15
    float jumpBuffer = 0.15f;
    /// ME: TdMove_Coil.TotalHeightBoost = 60: how far the legs tuck up
    float coilLift = Uu(60.0f);
    /// ME: TdMove_Coil.CoilMinTriggerSpeed = 100: forward speed (along the view) needed to coil
    float coilMinSpeed = Uu(100.0f);

    // ---------------------------------------------------------------- wallrun
    /// ME: TdMove_WallRun.WallRunningMinSpeed = 200
    float wallrunMinSpeed = Uu(200.0f);
    /// ME: WallRunningVelocityStartLimit = 300: StartMove's floor on the entry speed when
    /// you hit the wall still rising (it doesn't limit starting).
    float wallrunMaxStartUp = Uu(300.0f);
    /// ME: WallRunningHorisontalInitialZHeight = 170: the wallrun lifts you this high
    float wallrunRiseHeight = Uu(170.0f);
    /// ME: WallRunningVelocityStopLimit = -500: wallrun ends once falling this fast
    float wallrunStopFallSpeed = Uu(500.0f);
    /// ME: WallRunningHorisontalAcceleration = 820. Despite the name, the game's native wallrun
    /// update (0x12073c0) uses it as the wallrun's gravity while you're rising (and StartMove
    /// launches you with sqrt(2 x rise x this)). x (1 + chained wallruns).
    float wallrunAccel = Uu(820.0f);
    /// ME: WallRunningHorisontalDeceleration = 500: the wallrun's gravity once you're falling.
    float wallrunDecel = Uu(500.0f);
    /// ME: WallRunningHorisontalFriction = 0.05: the wallrun move's FrictionModifier, a drag on
    /// your speed along the wall (per second).
    float wallrunFriction = 0.05f;
    /// ME: WallRunningForwardMaxStartAngle = 57 deg: how steeply you may face into the wall
    /// (from along it) and still wallrun (FindWallForward); past it it's a wallclimb.
    float wallrunMaxApproachDeg = 57.0f;
    /// ME: WallRunningStrafeStartAngle = 60 deg: FindWallSide takes a wall up to this far
    /// off square to your side.
    float wallrunStrafeStartDeg = 60.0f;
    /// ME: WallRunningForwardCheckDistance / StrafeCheckDistance = 50
    float wallrunReach = Uu(50.0f);
    /// ME: TdPhysicsMove.ContextMoveDistanceMultiplier = 1.8: FindWallForward looks this much
    /// further ahead at full sprint (scaled from SpeedMaxBase up to GroundSpeed).
    float contextMoveDistanceMultiplier = 1.8f;
    /// ME: WallRunningMinWallHeight = 192
    float wallrunMinWallHeight = Uu(192.0f);
    /// ME: TdMove_WallrunJump.WallRunningPushAwaySpeedNoob = 120
    float wallrunJumpOut = Uu(120.0f);
    /// ME: WallRunningPushAwaySpeedProAdd = 400: extra push when looking away from the wall
    float wallrunJumpOutLookAdd = Uu(400.0f);
    /// ME: WallRunningPushForwardSpeedMin = 0.1: fraction of the along-wall speed kept when
    /// jumping off looking straight out (looking along the wall keeps all of it)
    float wallrunJumpForwardMin = 0.1f;
    /// ME: WallRunningJumpOffZHeightForward = 100
    float wallrunJumpHeight = Uu(100.0f);
    /// ME: WallRunningJumpOffZHeightMaxAddTurned = 60
    float wallrunJumpHeightTurnedAdd = Uu(60.0f);
    /// guess: camera tilt away from the wall
    float wallrunRollDeg = 12.0f;

    // ---------------------------------------------------------------- wallclimb
    /// ME: TdMove_WallClimb.WallClimbingGravity = 800: the climb's gravity, exactly (the native
    /// update, 0x12071f0, sets the pawn's acceleration to -this). WallClimbingVerticalFriction = 6
    /// only applies to sideways motion along the wall.
    float wallclimbGravity = Uu(800.0f);
    /// ME: TdMove_WallClimb.ReachedWall: the climb boost is sqrt(4 x height x WallClimbingGravity),
    /// where height = AddOnSpeed2DHeight (60) x how far your run speed was from
    /// SpeedMaxBaseVelocity (400) to AddOnSpeed2DMaxLimit (650), plus AddOnSpeedZHeight (130)
    /// x your upward speed over AddOnSpeedZMaxLimit (320).
    float wallclimbAddXyHeight = Uu(60.0f);
    float wallclimbAddXyMaxSpeed = Uu(650.0f);
    float wallclimbAddZHeight = Uu(130.0f);
    float wallclimbAddZMaxSpeed = Uu(320.0f);
    /// ME: WallClimbingVerticalStartAngle = 33 deg: must face the wall within this
    float wallclimbMaxAngleDeg = 33.0f;
    /// ME: MinWallHeight = 180
    float wallclimbMinWallHeight = Uu(180.0f);
    /// ME: TdMove_WallClimb180TurnJump.JumpOffZHeight = 250
    float wallkickHeight = Uu(250.0f);
    /// ME: TdMove_WallClimb180TurnJump.JumpPushAwaySpeed = 400
    float wallkickOut = Uu(400.0f);
    /// ME: TdMove_WallClimb180TurnJump.JumpTimeWindow = 0.6: how long after turning you can kick off
    float wallkickWindow = 0.6f;

    // ---------------------------------------------------------------- ledges
    // (Vaults and step-ups use the game's own VaultTypes table: see vault.rs.)
    /// ME: StepUpHighMaxHeight = 148: the lowest ledge you grab from the air
    float mantleHi = Uu(148.0f);
    /// ME: TdMove_Grab.GrabDesiredLedgeOffset.Z = 92.8 above cylinder centre
    /// -> ledge top is 1.83 m above the feet while hanging
    float hangHandsAboveFeet = Uu(90.0f + 92.8f);
    /// ME: TdMove_IntoGrab.HangFoldedUpperDeltaDistance = 35: highest reachable above hang
    float ledgeGrabHiExtra = Uu(35.0f);
    /// guess (animation-driven in ME)
    float pullupTime = 0.55f;
    /// guess: how long a fresh grab holds before you can pull up (the grab
    /// animation landing on the lip)
    float hangSettleTime = 0.35f;
    /// ME: one hand-over-hand step of `HangStrafeLeft`/`HangStrafeRight` (the clip is 1.07 s
    /// and carries the body 60 units; TdMove_Grab.ShimmyVelocity is 60 units/s).
    float shimmyStep = Uu(60.0f);
    float shimmyStepTime = 1.07f;
    /// ME: TdMove_Grab.DisableShimmyTime = 0.6: no shimmying this soon after grabbing.
    float shimmyDelay = 0.6f;
    /// ME: `HangCornerOutSideLeft`/`Right` length: shimmying round an outside corner.
    float shimmyCornerTime = 1.37f;
    /// ME: TdMove_GrabJump.GrabJumpOffZHeight = 160
    float hangJumpHeight = Uu(160.0f);
    /// ME: GrabJumpPushAwayMaxSpeed = 400
    float hangJumpOut = Uu(400.0f);

    // ---------------------------------------------------------------- slide
    /// ME: TdMove_Slide.CanDoMove: Velocity . facing >= 350 (forward speed, not total)
    float slideMinSpeed = Uu(350.0f);
    /// ME: TdMove_Slide.StartMove sets a 0.5 s timer; letting go of crouch earlier only ends the
    /// slide once it fires
    float slideMinTime = 0.5f;
    /// ME: TdMove_Slide.StopMove halves the velocity, however the slide ends (jumping out included)
    float slideExitKeep = 0.5f;
    /// ME: SlideAbortSpeed = 250: the native abort check (0x11f8640) ends the slide below this
    /// (total speed), or when you pull back (MoveActionHint down)
    float slideAbortSpeed = Uu(250.0f);
    /// ME: TdMove_Slide.FrictionModifier = 0.1: the walking physics (ATdPawn s1fc, 0x12bef70) brakes
    /// the slide with GroundFriction x this (x the slope term, 0 on flat ground). Move input is
    /// ignored for the whole slide (DisableMovementTime = -1), so it only ever brakes.
    float slideFrictionModifier = 0.1f;
    /// ME: the slide's native tick (UTdMove_Slide 0x11fdde0): each frame the body turns toward
    /// the view by dt x this x the angle between them (0.2 per second)...
    float slideLookTurn = 0.2f;
    /// ...and A/D (MoveActionHint left/right) turn it at 2000 units/s (11 degrees/s).
    float slideStrafeTurn = 2000.0f / 65536.0f * kTau;

    // ---------------------------------------------------------------- landing
    /// ME: TdMove_Landing.SkillRollLandingHeight = 200: falls this big can be rolled
    float rollHeight = Uu(200.0f);
    /// ME: HardLandingHeight = 530: falls this big hurt unless you roll
    float hardLandHeight = Uu(530.0f);
    /// ME: TdPawn.FallingUncontrolledHeight = 1000: past this you're done
    float lethalFallHeight = Uu(1000.0f);
    /// Falling below this height (m) is death: the maps' floor. None: no floor (a host game
    /// with its own world, where heights can be anything).
    std::optional<float> killY = -40.0f;
    /// Dying puts you back at the spawn point. Off, Death is only reported (the host decides).
    bool respawnOnDeath = true;
    /// ME: TdPawn.CanSkillRoll: RollTriggerTime + 0.2 > now, i.e. crouch pressed in the last
    /// 0.2 s before touching down
    float rollWindow = 0.2f;
    /// ME: TdPawn.Tick only takes a new roll press 0.6 s after the last one, so mashing crouch
    /// on the way down doesn't work
    float rollRetrigger = 0.6f;
    /// ME: TdMove_Landing.LandingSpeedReduction = 65 uu/s. After a plain jump, a normal landing
    /// caps your horizontal speed at the speed you jumped with minus this (SubtractLandingSpeed),
    /// so the jump's forward boost is handed back. Dodges and wall jumps don't pay it.
    float landingSpeedReduction = Uu(65.0f);
    /// ME: how long a hard landing holds you. TdMove_Landing.LandHard zeroes your velocity and
    /// ignores move and look input until `FallingLandHard` (2 s, blend out 0.2) ends
    /// (OnCustomAnimEnd -> walking).
    float hardLandStun = 1.8f;
    /// ME: `JumpTurnLanding` (1.13 s): falling onto your back after a 180 in the air.
    float layOnGroundFallTime = 1.13f;
    /// ME: `JumpTurnLandingStand` (1.33 s): getting up off your back.
    float layOnGroundGetUpTime = 1.33f;
    /// ME: TdMove_LayOnGround.GetUpBack: `EvadeRoll` (1 s), rolling you back about 3.6 m (its
    /// root motion) into a crouch.
    float backRollTime = 1.0f;
    float backRollDistance = Uu(359.0f);
    /// ME: TdMove_Barge: BargeKickThresholdSpeed 250 (faster: shoulder through; slower: kick),
    /// BargeMinTraceDistance 90, BargeTraceTime 0.5, BargeAddOnSpeed 200, BargeMaxSpeed 500,
    /// BargeAnimTime 0.3.
    float bargeKickThreshold = Uu(250.0f);
    float bargeMinTrace = Uu(90.0f);
    float bargeTraceTime = 0.5f;
    float bargeAddSpeed = Uu(200.0f);
    float bargeMaxSpeed = Uu(500.0f);
    float bargeAnimTime = 0.3f;
    /// ME: `MeleeKickObject` (the slow barge): move input ignored 0.6 s; the kick lands about
    /// 0.36 s in (BargeHitNotify, with the clip's impact footstep).
    float bargeKickTime = 0.6f;
    float bargeKickHit = 0.36f;
    /// ME: TdMove_Stumble with the barbed wire in front: `StumbleFwd` (1 s, about 2.6 m of root
    /// motion); behind you: `GetHitStumbleBwd`, cut off after 0.5 s (about 1.25 m back).
    float stumbleFwdTime = 1.0f;
    float stumbleFwdDistance = Uu(256.0f);
    float stumbleBwdTime = 0.5f;
    float stumbleBwdDistance = Uu(125.0f);
    /// ME: TdMove_Landing.LandOnSoftObject: `FallingLandSoftLanding` (1.5 s), input ignored.
    float softLandTime = 1.5f;
    /// ME: TdPhysicsMove.SoftLandingZSpeedThreshold = -400: brace for a soft landing once
    /// falling faster than this toward one (TdMove_SoftLanding).
    float softBraceSpeed = Uu(400.0f);
    /// guess: how long a landing roll lasts (the game plays `fallinglandroll`)
    float rollTime = 0.5f;

    // ---------------------------------------------------------------- dodge jumps
    /// ME: TdPlayerController.SetInputHint bMoveActionMax: |aStrafe| > 0.96 (a key is 1.0)
    float dodgeStrafeThreshold = 0.96f;
    /// ME: TdMove_DodgeJump.ExitToFallingZSpeed = -190: the falling move takes over below it
    float dodgeExitFallSpeed = Uu(190.0f);
    /// ME: TdMove_DodgeJump.RedoMoveTime = 0.3
    float dodgeRedo = 0.3f;
    /// ME: TdMove_DodgeJump.BaseJumpZ = 300
    float dodgeUp = Uu(300.0f);
    /// ME: TdMove_DodgeJump.JumpAddXY = 600: sideways launch speed
    float dodgeSide = Uu(600.0f);
    /// ME: TdMove_DodgeJump.DodgeJumpInertiaConservation = 0.3: fraction of old velocity kept
    float dodgeInertia = 0.3f;
    /// ME: TdPlayerController.WallRunningDodgeJumpThreshold = 0.8
    float wallrunDodgeThreshold = 0.6f;
    /// ME: TdMove_WallrunDodgeJump BaseJumpZ 300 / JumpAddXY 600 / inertia 0.3
    float wallrunDodgeUp = Uu(300.0f);
    float wallrunDodgeSide = Uu(600.0f);
    float wallrunDodgeInertia = 0.3f;
    /// ME: TdPlayerController.WallClimbingDodgeJumpThreshold = 0.8
    float wallclimbDodgeThreshold = 0.6f;
    /// ME: TdMove_WallClimbDodgeJump BaseJumpZ 700 / JumpAddXY 150 / inertia 1.0
    float wallclimbDodgeUp = Uu(700.0f);
    float wallclimbDodgeSide = Uu(150.0f);
    float wallclimbDodgeInertia = 1.0f;

    // ---------------------------------------------------------------- turning
    /// Without the game's animations: how long a 180 takes (the clips take 0.17-0.6 s).
    float turn180Time = 0.2f;
    /// The game's turn clips' root yaw (set from the animations when the game is installed).
    // turn_curves: Option<std::sync::Arc<crate::controller::TurnCurves>>, None by default; ported with its feature.

    // ---------------------------------------------------------------- springboard
    /// A springboard (TdMove_SpringBoard.CanDoMove) takes two things in a row: a low step,
    /// IntermediateFootPlantHeight = 64 (+/-20) above the floor, and behind it something
    /// SpringBoardMinHeight = 80 to MaxHeight = 148 high whose front is
    /// IntermediateFootPlantDistance = 112 (+/-20) behind the step's. One foot on each, then launch.
    float springboardStepHeight = Uu(64.0f);
    float springboardStepSpacing = Uu(112.0f);
    /// ME: the +/-20 uu slack on both of the above
    float springboardSlack = Uu(20.0f);
    float springboardLo = Uu(80.0f);
    float springboardHi = Uu(148.0f);
    /// ME: CheckDistanceTime = 1.0: jump when you'd reach the step within this many seconds
    float springboardCheckTime = 1.0f;
    /// ME: StartMove runs you in (at 1.2 x your speed, ignoring input) to 120 uu before the
    /// step, then takes the two steps
    float springboardRunIn = Uu(120.0f);
    /// ME: SpringBoardJumpZ = 950
    float springboardUp = Uu(950.0f);
    /// ME: SpringBoardJumpXYAdd = -100, SpringBoardJumpXYMin = 400
    float springboardXyAdd = Uu(-100.0f);
    float springboardXyMin = Uu(400.0f);
    /// ME: StepTime1 + StepTime2 = 0.2 + 0.2: onto the step, then onto the top
    float springboardStepTime = 0.4f;

    // ---------------------------------------------------------------- balance
    /// ME: TdMove_Balance.SpeedModifier = 0.34 (x GroundSpeed)
    float balanceSpeedModifier = 0.34f;
    /// The top speed on a beam: GroundSpeed x SpeedModifier.
    float balanceSpeed = 0.34f * Uu(720.0f);
    /// ME: GravityInfluence 0.3, ControlInfluence 1.5, SpeedInfluence 2.5,
    /// CameraInfluence 0.3: how the lean grows and how you counter it.
    float balanceGravity = 0.3f;
    float balanceControl = 1.5f;
    float balanceSpeedInfluence = 2.5f;
    float balanceCamera = 0.3f;
    /// ME: TdMove_Balance.TimeToCounter = 0.8: time at the edge before you fall
    float balanceCounterTime = 0.8f;
    /// UTdMove_Balance tick: back under this lean you're out of danger (0.9)
    float balanceRecover = 0.9f;

    // ---------------------------------------------------------------- zipline
    /// ME: TdMove_ZipLine.HangOffset.Z = -90: the pawn hangs this far below the
    /// cable, from its centre (so feet are 0.9 + 0.9 m down).
    float zipHang = Uu(90.0f + 90.0f);
    /// ME: MinZipVelocity = 300 / MinZipAcceleration = 400
    float zipMinSpeed = Uu(300.0f);
    float zipAccel = Uu(400.0f);
    /// guess
    float zipMaxSpeed = 13.0f;
    /// ME: TdMove_IntoZipLine.ZVelocityFallLimit = -600: can't catch it falling faster
    float zipMaxFall = Uu(600.0f);
    /// guess: how close the hands must pass to grab
    float grabReach = 0.5f;

    // ---------------------------------------------------------------- swing pole
    /// ME: TdMove_Swing.SwingPendulumLength = 120
    float swingLength = Uu(120.0f);
    /// ME: MaxSwingVelocity = 4.25 (rad/s at the bottom)
    float swingMaxRate = 4.25f;
    /// ME: ExitVelocityModifier = 600
    float swingExitSpeed = Uu(600.0f);
    /// ME: SwingExitGravityModifier = 0.75 for SwingExitGravityModifierTime = 0.7 s
    float swingExitGravity = 0.75f;
    float swingExitGravityTime = 0.7f;

    // ---------------------------------------------------------------- melee
    /// ME: the attack animations' lengths (MeleeStart + MeleeMissed ~ 0.7 s);
    /// the app sets these from the real animations.
    MeleeClips meleeClips = {};
    /// The takedown clips' lengths (takedown::TAKEDOWN_ANIMS): each takedown lasts its clip.
    std::array<float, 4> takedownClips = {2.53f, 2.10f, 2.03f, 1.97f};
    /// TdMove_AirBarge's clips: AirBargeIdle, AirBargeImpact, AirBargeLand.
    std::array<float, 3> airBargeClips = {1.0f, 0.6f, 0.8f};
    /// The ladder clips' root motion (ExitAtTop, LadderEnterTop), when the host has them.
    // climb_curves: Option<std::sync::Arc<crate::climb::ClimbCurves>>, None by default; ported with its feature.
    /// TdMove_AutoStepUp: walking into something 35-48 uu high steps up onto it. Mirror's Edge
    /// ships it switched off (its levels ramp their stairs); for hosts whose stairs aren't.
    bool autoStepUp = false;
    /// The tallest step it takes (StepUpHighMaxHeight, 48 uu). Hosts whose world is built of
    /// half-metre steps raise it a hair over them.
    float autoStepUpMax = Uu(48.0f);
    /// ME: TdMove_MeleeCrouch.SpeedModifier = 0.2 / guess for standing
    float meleeSpeed = 0.4f;

    /// Launch speed needed to rise `height` metres under normal gravity.
    float SpeedForHeight(float height) const { return std::sqrt(2.0f * gravity * FMax(height, 0.0f)); }
};

/// Every float and bool field under its Rust name (bools as 0/1), for the golden check.
std::vector<std::pair<std::string, float>> TuningFields(const Tuning& t);

} // namespace runline
