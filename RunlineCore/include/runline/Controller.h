#pragma once
// The movement state machine. Port of faith_move::controller.
//
// Call Controller::Step once per frame with the frame's input. It substeps internally at 120 Hz
// so feel doesn't change with frame rate.
//
// Ported (M1c/M1d): ground and air, jumps and dodges, landings, wallrun, wallclimb and its turn,
// ledge hang, shimmy, pull-up and grab transfer, slide, roll, vault and springboard. What's left
// (attacks, fixtures, ladders, rump slide, balance, vertigo) is stubbed in src/ControllerStubs.cpp:
// checks answer "nothing here" and states throw NotPorted, so a replay that needs one fails loudly.
#include <deque>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include "runline/Locomotion.h"
#include "runline/Move.h"
#include "runline/Probes.h"
#include "runline/Tuning.h"
#include "runline/Vault.h"
#include "runline/World.h"

namespace runline {

/// The subset of glam::Vec2 the controller uses, in glam's operation order.
struct Vec2 {
    float x = 0, y = 0;
    friend bool operator==(Vec2 a, Vec2 b) = default;
    float LengthSquared() const { return (x * x) + (y * y); }
    /// glam's clamp_length_max: max * (self / sqrt(length_sq)) when longer than max.
    Vec2 ClampLengthMax(float max) const {
        const float lengthSq = LengthSquared();
        if (lengthSq > max * max) {
            const float len = std::sqrt(lengthSq);
            return {max * (x / len), max * (y / len)};
        }
        return *this;
    }
};

/// One frame of player input. Buttons are "pressed this frame" edges plus "held" levels; `look`
/// is the mouse/stick delta in radians.
struct Input {
    /// x = strafe right, y = forward. Length is clamped to 1.
    Vec2 moveAxis{};
    /// x = yaw delta (positive turns left), y = pitch delta (positive looks up).
    Vec2 look{};
    bool jumpPressed = false;
    bool jumpHeld = false;
    bool crouchPressed = false;
    bool crouchHeld = false;
    bool turnPressed = false;
    /// Kick / punch.
    bool meleePressed = false;
    /// The takedown (TdMOVE_Disarm) on whoever's in front of her.
    bool takedownPressed = false;
    /// The strafe axis before the move vector is clamped (PlayerInput.aStrafe): a key is the full
    /// +-1 even with W held. 0 = take it from `moveAxis`.
    float strafeRaw = 0.0f;
};

/// Every faith_move Event variant, in source order.
enum class EventKind {
    Jump, Land, HardLand, Roll, Slide, WallRunStart, WallJump, WallClimbStart, WallKick, LedgeGrab,
    PullUp, Vault, Mantle, Turn180, WallRunTurn, LandOnBack, ShimmyCorner, SlideEnd, BackRoll, Barge,
    AirBarge, AirBargeImpact, Vertigo, SwingToSwing, ClimbStart, ClimbStep, ClimbExit, ClimbLetGo,
    ClimbSlide, ClimbSlideEnd, AirBargeLand, DoorOpened, Stumble, SoftLand, Dodge, WallRunDodge,
    WallClimbDodge, Death, SpringBoard, BalanceStart, BalanceFall, Takedown, TakedownDone, StepUp,
    RumpSlide, GrabTransfer, ZipStart, ZipBrace, ZipEnd, SwingStart, SwingJump, Melee, MeleeOutcome,
    MeleeHit, Taunt,
};
/// The Rust variant's name (what the trace patch records).
const char* EventName(EventKind k);

/// An event with the payloads the core uses (Land's impact and fall, Dodge's direction).
struct Event {
    EventKind kind;
    /// Land: downward speed (m/s) and metres fallen from the top of the arc.
    float impact = 0.0f;
    float fall = 0.0f;
    /// Dodge, WallRunDodge, WallClimbDodge.
    Vec3 dir{};
};

/// The camera: eye position and look angles.
struct View {
    Vec3 eye{};
    float yaw = 0.0f;
    float pitch = 0.0f;
    /// Radians, positive tilts the head to the right.
    float roll = 0.0f;
    float fovDeg = 90.0f;
};

// The states. Each is a struct with the Rust variant's payload. M1c has the ones the core can
// reach; later milestones add the rest.
struct Ground {};
struct Air {};
struct Slide { float t = 0.0f; };
struct Roll { float t = 0.0f; };
struct Stunned { float t = 0.0f; };
struct SoftLand { float t = 0.0f; };
struct LayOnGround { float t = 0.0f; std::optional<float> gettingUp; bool backRoll = false; };
struct WallRun { Vec3 normal{}; float t = 0.0f; };
struct WallClimb { Vec3 normal{}; float t = 0.0f; };
struct WallClimbTurned { Vec3 normal{}; float t = 0.0f; };
struct LedgeHang { Vec3 normal{}; float ledgeY = 0.0f; bool turned = false; };
/// Reaching up from one ledge to another above (TdMove_GrabTransfer, `grabtransfer`).
struct GrabTransfer { float t = 0.0f; Vec3 from{}; Vec3 to{}; float dur = 0.0f; Vec3 normal{}; float ledgeY = 0.0f; };

enum class TraverseKind {
    Vault,
    Mantle,
    PullUp,
    /// Foot plant on an obstacle, then launch (TdMove_SpringBoard).
    SpringBoard,
};
/// A scripted move from `from` to `to` over `dur` seconds (the pull-up, the springboard).
struct Traverse {
    TraverseKind kind = TraverseKind::PullUp;
    Vec3 from{};
    Vec3 to{};
    float t = 0.0f;
    float dur = 0.0f;
    float arc = 0.0f;
    Vec3 exitVel{};
};

/// A springboard you've committed to but haven't reached yet: TdMove_SpringBoard runs you in to
/// the step before the two foot plants.
struct SpringRunIn {
    /// A point on the step's front face, and its outward normal.
    Vec3 face{};
    Vec3 n{};
    /// Where the feet go on top of the tall part, and the launch velocity.
    Vec3 plant{};
    Vec3 launch{};
    float speed = 0.0f;
    float t = 0.0f;
};

/// A shimmy along a ledge (TdMove_Grab.StartShimmy / StartShimmyAroundCorner).
struct Shimmy {
    /// +1 to the right as you face the wall, -1 to the left.
    float dir = 0.0f;
    /// Seconds into this step (or the corner).
    float t = 0.0f;
    Vec3 from{};
    Vec3 to{};
    /// Going round an outside corner onto the face with this normal.
    std::optional<Vec3> corner;
    float yawFrom = 0.0f;
};

using State = std::variant<Ground, Air, Slide, Roll, Stunned, SoftLand, LayOnGround, WallRun, WallClimb, WallClimbTurned, LedgeHang, Traverse, Vault, GrabTransfer>;
/// State::name(): what the HUD (and the trace) calls it.
const char* StateName(const State& s);

/// A move or check this milestone hasn't ported, reached by a replay.
struct NotPorted : std::logic_error {
    explicit NotPorted(const std::string& what) : std::logic_error("not ported: " + what) {}
};

/// How the current time in the air began, for the rules that depend on it.
enum class Takeoff {
    /// Walked off something, or any other move ended in the air.
    Other,
    /// A plain jump (TdMove_Jump): the landing hands back its forward boost.
    Jump,
    /// A wallrun jump-off (TdMove_WallrunJump): a wallrun straight after it is a chained one.
    WallJump,
};

/// Which clip turns you round.
enum class TurnKind { Run, Stand, Air, WallClimb, Swing };

/// A 180 turn: without the game's animations it eases round to the right over turn180_time.
struct Turn {
    float from = 0.0f;
    /// Seconds since the turn started.
    float t = 0.0f;
    TurnKind kind = TurnKind::Stand;
};

/// TdMove.SetLookAtTargetAngle: each frame the view moves min(1, dt / interp) of the way to the
/// target, for `left` more seconds or until the move ends.
struct LookAt {
    float yaw = 0.0f;
    float interp = 0.0f;
    float left = 0.0f;
};

/// TdPawn.AgainstWallState: which hands are up against a wall right in front of you.
struct AgainstWall {
    std::optional<Vec3> left;
    std::optional<Vec3> right;
};

/// Which attack is playing (only its kind matters to the core: punches slow the run, an air kick
/// rules out the landing roll). Attacks themselves are ported later.
enum class MeleeKind { Punch, Crouch, AirKick, SlideKick, WallRunKick, VaultKick };

/// A wall found by TraceWall, as the wallrun search returns it.
using WallFound = WallHit;

class Controller {
public:
    Controller(Tuning tuning, Vec3 spawn, float spawnYaw);

    Tuning tuning;
    Vec3 feet{};
    Vec3 vel{};
    float yaw = 0.0f;
    float pitch = 0.0f;
    State state = Air{};
    /// Turned round in the air this jump (TdMove_180TurnInAir): you'll land on your back.
    bool turnedInAir = false;
    /// The body's yaw while sliding (the pawn's rotation: the view turns separately).
    float slideYaw = 0.0f;
    /// Falling toward a soft landing object fast enough to brace (TdMove_SoftLanding).
    bool softBrace = false;
    bool crouched = false;
    /// 0..1, how far into the sprint build-up we are.
    float sprintCharge = 0.0f;
    Vec3 spawn{};
    float spawnYaw = 0.0f;
    std::vector<Event> events;
    /// Hands up against the wall in front of you (walking or crouched).
    AgainstWall againstWall;
    /// The wall goes on above her head where a wallrun started.
    bool wallrunWallAbove = true;
    /// Her vertical speed as she caught the last ledge (m/s).
    float grabSpeed = 0.0f;
    /// How far her mesh (and the camera) is held below or above her feet after a step.
    float meshOffset = 0.0f;
    /// Seconds since grabbing the current ledge.
    float hangTime = 0.0f;
    /// Shimmying along (or round the corner of) the ledge you hang from.
    std::optional<Shimmy> shimmy;

    /// Advance by `dt` seconds.
    void Step(float dt, const Input& input, const World& world);
    void Respawn();
    float Height() const;
    Body GetBody() const;
    float HorizontalSpeed() const;
    Vec3 Forward() const;
    /// The un-embellished camera: eye position and look angles.
    View GetView() const;
    /// Where the feet would be with the legs straight: feet minus the coil lift while tucked.
    Vec3 Root() const;
    bool IsCoiled() const { return coiled_; }

private:
    float airTime_ = 0.0f;
    bool jumpedSinceGround_ = false;
    float jumpBuffer_ = 0.0f;
    /// Seconds since the last crouch press that counted as a roll trigger (TdPawn.RollTriggerTime).
    float rollTriggerAge_;
    Takeoff takeoff_ = Takeoff::Other;
    /// Horizontal speed when the last plain jump started (TdMove_Jump.PreJumpMomentum).
    float preJumpMomentum_ = 0.0f;
    /// Feet position at the last jump of any kind (TdPawn.LastJumpLocation).
    Vec3 lastJumpAt_{};
    /// Wallruns in a row, each started from the previous one's jump-off.
    unsigned chainWallruns_ = 0;
    bool climbedThisAir_ = false;
    std::optional<Vec3> lastWallNormal_;
    float regrabCooldown_ = 0.0f;
    std::optional<Turn> turn_;
    float yawRate_ = 0.0f;
    /// Sprint energy and the controller's stop state (locomotion).
    RunState run_{};
    /// How far the view turned this frame (radians): sprinting sheds speed for it.
    float frameTurn_ = 0.0f;
    float eyeHeight_ = 0.0f;
    /// Highest point of the current fall (ME judges landings by fall height).
    float fallPeak_ = 0.0f;
    /// Legs tucked in the air (coil).
    bool coiled_ = false;
    /// The attack playing right now, if any (its kind; attacks are ported later).
    std::optional<MeleeKind> melee_;
    /// Recent (dt, speed) samples, a quarter second of them (melee momentum uses it).
    std::deque<std::pair<float, float>> speedLog_;
    /// Knocked back off an air kick: no air control until she lands.
    bool meleeNoInput_ = false;
    /// Lighter gravity just after leaving a swing pole, and the GravityModifier meanwhile.
    float lowGrav_ = 0.0f;
    float lowGravK_ = 1.0f;
    /// In TdMove_DodgeJump.
    bool dodging_ = false;
    std::optional<LookAt> lookAt_;
    /// TdMove_WallRun.bTurned90FromWall: Q pressed on this wallrun.
    bool wallTurned_ = false;
    /// TdMove_DodgeJump.RedoMoveTime: no new dodge until this runs out.
    float dodgeRedo_ = 0.0f;
    /// Can't re-grab a zipline or pole straight after letting go.
    float fixtureCooldown_ = 0.0f;
    float rumpRedo_ = 0.0f;
    float vertigoRedo_ = 0.0f;
    float climbRedo_ = 0.0f;
    /// A committed springboard run-in.
    std::optional<SpringRunIn> springboard_;

    float BodyY() const;
    void SyncSprintCharge();
    float Headroom(const World& world, float max) const;
    bool Grounded(const World& world) const;
    bool TryStand(const World& world);
    void UpdateCoil(const Input& input, const World& world);
    void UncoilOnGround(const World& world);
    void Substep(float dt, const Input& input, const World& world);
    void GroundState(float dt, const Input& input, const World& world);
    void GroundJump();
    void DodgeJump(float side);
    void EndDodge();
    void Dodge(Vec3 dir, float side, float up, float inertia);
    void Jump(float speed);
    void AirState(float dt, const Input& input, const World& world);
    void UpdateSoftBrace(const World& world);
    bool AirTransitions(const Input& input, const World& world);
    void GrabLedge(Vec3 n, float gap, float top, const World& world);
    void Land(float impact, const World& world);
    bool SoftBelow(const World& world, float depth) const;
    void StunnedState(float dt, float t, const World& world);
    void WallRunState(float dt, Vec3 n, float t, const Input& input, const World& world);
    void WallClimbState(float dt, Vec3 n, float t, const Input& input, const World& world);
    void WallClimbTurnedState(float dt, Vec3 n, float t, const World& world);
    std::optional<WallFound> FindWallrun(const Input& input, const World& world) const;
    void HangState(float dt, Vec3 n, float ledgeY, const Input& input, const World& world);
    void ShimmyUpdate(float dt, Vec3 n, float ledgeY, const Input& input, const World& world);
    void TraverseState(float dt, Traverse tr, const World& world);
    void MoveBy(Vec3 d, const World& world);
    void SlideState(float dt, float t, const Input& input, const World& world);
    void RollState(float dt, float t, const World& world);
    void VaultState(float dt, Vault v, const World& world);
    void GrabTransferState(float dt, GrabTransfer g);
    void CheckBarbedWire(const World& world);
    void Integrate(float dt, const World& world, bool onGround);
    void OffsetMesh(float d);
    void SmoothMesh(float dt);
    void UpdateView(float dt);
    AgainstWall CheckAgainstWall(const World& world) const;
    bool CanTurn() const;
    void StartTurn();
    void MeleeTick(float dt);

    // Not ported yet (src/ControllerStubs.cpp).
    void VertigoInput(Input& input) const;
    void MeleePressedAgain();
    void StartMelee();
    bool TryTakedown(const World& world);
    bool TryIntoClimb(const Input& input, const World& world);
    bool TryRumpSlide(const World& world);
    bool TryBalance(const World& world);
    bool TryAutoStepUp(const World& world);
    bool TrySpringboard(const World& world);
    void SpringboardRunIn(float dt, const World& world);
    bool TryVertigo(const World& world);
    bool TryGrabFixture(const World& world);
    bool TryGrabTransfer(Vec3 n, float ledgeY, const World& world);
    bool TryVault(const Input& input, const World& world);
    void UnportedState(const State& s, float dt, const Input& input, const World& world);
};

} // namespace runline
