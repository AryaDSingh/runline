# M3a: First-Person Arms Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** First-person arms that react to every core state, with the hands on the surfaces the core found: the ledge lip, the wall, the vault's hand plant.

**Architecture:**
- **`FRunlineArmsSolver`** (plain C++, in the Runline module) turns the core's state, contacts and events, plus the rendered camera, into two hand targets each frame. A target is a pose relative to the camera, or a point on a contact moved into Unreal's first-person render space.
- **`URunlineArmsAnimInstance`** writes the pose in C++: the reference pose, finger curls, two-bone IK per arm and the hand's orientation. There's no anim graph.
- **`ARunlineRunner`** runs the solver each tick and hands the pose to the arms. The arms ride the camera, not the hidden body.
- **The core is untouched.** The movement component only exposes what it already has: the events of every step in a frame, the interpolation alpha, and a count of view snaps.

**Tech Stack:** UE 5.8 C++ (UAnimInstance + FAnimInstanceProxy, AnimationCore's SolveTwoBoneIK, automation tests), RunlineCore (read-only, plus a test-only hook in tests/Replay.h).

**Spec:** `docs/superpowers/specs/2026-10-10-m3a-first-person-arms-design.md`

## Global Constraints

- RunlineCore is not modified (the arms are new work, not a port). The only core-tree change is a test-only callback parameter in `RunlineCore/tests/Replay.h`. (spec §3)
- Unit/axis conversion happens only through `RunlineUnits.h` (`ToUnrealPosition`, `ToUnrealDirection`, `ToUnrealRotator`). (CLAUDE.md)
- Nothing the arms do feeds back into the core; every trace still replays bit-exact. (spec §3 Timing)
- No Mirror's Edge files, no faith_anim code, no new downloaded assets: only `/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple`. (spec §2)
- Unreal at `D:\UE_5.8`. Tests: `powershell -ExecutionPolicy Bypass -File scripts/test-unreal.ps1 [-Filter X]` (52 tests before this plan) and `powershell -ExecutionPolicy Bypass -File scripts/test.ps1` (core, 80 test cases); both stay green. The test runner fails on a crash; read `build/unreal-tests.log`.
- Game-target builds don't have editor-only APIs: guard any such use with `WITH_EDITOR`. The Runline module is `/fp:precise`.
- Edit scripts go in the scratchpad as files (shell heredocs mangle `\n`).
- Merging to master is the user's decision.

## Review Focus

1. **Hanging with her back to the wall** (`LedgeHang.turned`): the lip is behind the camera. The contact must fade out by reach, never pulling an arm through her head. Test: Task 4 `Runline.Arms.TurnedHangLetsGo`.
2. **A very long frame** (alt-tab, a 2 s hitch): springs sub-step and stay finite and near rest. Test: Task 3 `Runline.Arms.LongFrameStaysStable`.
3. **Zero-length timed moves** (`GrabTransfer.dur` 0, a shimmy with `from == to`): no division by zero; the hands end at their targets. Test: Task 4 `Runline.Arms.ZeroDurationsAreFinite`.
4. **The player's FOV setting and the sprint FOV kick** (60 to 120 against the arms' 70): a contact hand still draws over its contact. Test: Task 2 `Runline.Arms.FirstPersonSpaceAtAnyFov`.
5. **A respawn or death mid-hang:** the hands cut to the new place with no sweep from the old ledge and no carried-over dip. Test: Task 3 `Runline.Arms.SnapResets`.

## File Structure

```
Unreal/Runline/Source/Runline/
  Runline.Build.cs                         modify: + "AnimationCore"
  Runner/RunlineMovementComponent.h/.cpp   modify: GetFrameEvents(), GetStepAlpha(), GetSnapCount()
  Runner/RunlineRunner.h/.cpp              modify: arms ride the camera, run the solver, ArmsTuning
  Runner/Arms/RunlineFirstPerson.h         create: world point <-> first-person render space
  Runner/Arms/RunlineArmsTypes.h/.cpp      create: FRunlineArmsTuning (USTRUCT), FRunlineHand, FRunlineArmsPose, FRunlineArmsInput (+ From)
  Runner/Arms/RunlineArmsSolver.h/.cpp     create: FRunlineArmsSolver
  Runner/Arms/RunlineArmsAnimInstance.h/.cpp  create: URunlineArmsAnimInstance + FRunlineArmsProxy
  Tests/ArmsSolverTests.cpp                create: Runline.Arms.* solver tests
  Tests/ArmsMeshTests.cpp                  create: Runline.Arms.Mesh* pose tests on the real mesh
  Tests/MovementComponentTests.cpp         modify: frame events, alpha, snap count
  Tests/RunnerTests.cpp                    modify: ArmsAndBody (arms on the camera, native anim instance)
RunlineCore/tests/Replay.h                 modify: optional per-step callback (test-only)
CLAUDE.md                                  modify: one line on the arms
```

`Runline.Build.cs` adds `PublicIncludePaths.Add("Runline/Runner/Arms")` with the other include paths.

---

### Task 1: What the arms need from the movement component

**Files:**
- Modify: `Unreal/Runline/Source/Runline/Runner/RunlineMovementComponent.h`, `.cpp`
- Modify: `RunlineCore/tests/Replay.h`
- Test: `Unreal/Runline/Source/Runline/Tests/MovementComponentTests.cpp`

**Interfaces:**
- Produces: `const TArray<runline::Event>& URunlineMovementComponent::GetFrameEvents() const` (every event of the steps the last `Advance` ran, in order); `float GetStepAlpha() const` (`Accumulator / StepSeconds`, in [0, 1)); `int32 GetSnapCount() const` (incremented by every `SnapView()`); `replay::Replay(t, worldFor, onStep)` with `onStep` of type `std::function<void(const runline::Controller&, const trace::Frame&)>`, default empty, called after each successful step.

- [ ] **Step 1: Write the failing test** (append before the final `#endif` in MovementComponentTests.cpp)

```cpp
// M3a: the arms see every event of a frame's steps (a hitch runs several), how far the render is
// past the last step, and when the view was cut.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineMovementArmsFeed, "Runline.Movement.ArmsFeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineMovementArmsFeed::RunTest(const FString&)
{
	FRunnerWorld W;
	W.M().Advance(Step);   // settle on the floor
	W.M().PressJump();
	W.M().SetJumpHeld(true);
	W.M().Advance(Step * 3.0f);   // three steps in one frame: the jump is in the first
	bool bJump = false;
	for (const runline::Event& E : W.M().GetFrameEvents()) bJump |= E.kind == runline::EventKind::Jump;
	TestTrue(TEXT("a hitch's events are all kept"), bJump);
	W.M().Advance(Step * 0.5f);
	TestEqual(TEXT("no step, no events"), W.M().GetFrameEvents().Num(), 0);
	TestEqual(TEXT("half way to the next step"), W.M().GetStepAlpha(), 0.5f, 1e-4f);
	const int32 Snaps = W.M().GetSnapCount();
	W.M().Respawn();
	TestEqual(TEXT("a respawn is a snap"), W.M().GetSnapCount(), Snaps + 1);
	return true;
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test-unreal.ps1 -Filter Runline.Movement.ArmsFeed`
Expected: the build fails: `GetFrameEvents` is not a member of `URunlineMovementComponent`.

- [ ] **Step 3: Implement**

In `RunlineMovementComponent.h`, public, after `NotPortedCount()`:

```cpp
	/// Every event of the steps the last Advance ran, in order (the arms; a hitch runs several).
	const TArray<runline::Event>& GetFrameEvents() const { return FrameEvents; }
	/// How far the render is between the last step and the next (0..1).
	float GetStepAlpha() const { return Accumulator / StepSeconds; }
	/// How many times the view has been cut (spawn, respawn, death): a change means don't blend.
	int32 GetSnapCount() const { return SnapCount; }
```

Private, after `int32 NotPorted = 0;`:

```cpp
	TArray<runline::Event> FrameEvents;
	int32 SnapCount = 0;
```

In `RunlineMovementComponent.cpp`:
- first line of `SnapView()`: `++SnapCount;`
- first line of `Advance()`: `FrameEvents.Reset();`
- in `StepOnce()`, right after `RunEffects(In);`: `for (const runline::Event& E : Controller->events) FrameEvents.Add(E);`

In `RunlineCore/tests/Replay.h`, change the signature and call the hook after the step succeeds:

```cpp
inline std::string Replay(const trace::Trace& t, const std::function<const World&(std::size_t)>& worldFor,
                          const std::function<void(const Controller&, const trace::Frame&)>& onStep = {}) {
```

and after the `try { c.Step(...) } catch` block: `if (onStep) onStep(c, f);`

- [ ] **Step 4: Run the tests**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test-unreal.ps1 -Filter Runline.Movement`
Expected: all Runline.Movement tests pass, ArmsFeed included.
Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: 80/80 test cases pass (Replay's new parameter has a default).

- [ ] **Step 5: Commit**

```bash
git add Unreal/Runline/Source/Runline/Runner/RunlineMovementComponent.h Unreal/Runline/Source/Runline/Runner/RunlineMovementComponent.cpp Unreal/Runline/Source/Runline/Tests/MovementComponentTests.cpp RunlineCore/tests/Replay.h
git commit -m "M3a: the movement component keeps a frame's events, the step alpha and a snap count"
```

---

### Task 2: Arm types and first-person space

**Files:**
- Create: `Unreal/Runline/Source/Runline/Runner/Arms/RunlineFirstPerson.h`
- Create: `Unreal/Runline/Source/Runline/Runner/Arms/RunlineArmsTypes.h`, `RunlineArmsTypes.cpp`
- Modify: `Unreal/Runline/Source/Runline/Runline.Build.cs`
- Test: `Unreal/Runline/Source/Runline/Tests/ArmsSolverTests.cpp` (create)

**Interfaces:**
- Consumes: Task 1's `GetFrameEvents`, `GetStepAlpha`, `GetSnapCount`.
- Produces:
  - `RunlineFirstPerson::Correction(float FovDeg, float FirstPersonFovDeg) -> double`, `ToFirstPerson(const FVector& P, const FTransform& Camera, float FovDeg, float FirstPersonFovDeg) -> FVector`, `ScreenOf(const FVector& P, const FTransform& Camera, float FovDeg, float FirstPersonFovDeg, bool bFirstPerson) -> FVector2D`;
  - `RunlineArms::EFingers {Relaxed, Fist, Open, Grip}`, `RunlineArms::Left = 0`, `RunlineArms::Right = 1`;
  - `FRunlineArmsTuning` (USTRUCT, fields below), `FRunlineHand`, `FRunlineArmsPose`, `FRunlineArmsInput` with `static FRunlineArmsInput From(const URunlineMovementComponent&, const FTransform& Camera, float Fov, float FirstPersonFov, float Dt)`.

- [ ] **Step 1: Write the failing test** (new file `Tests/ArmsSolverTests.cpp`)

```cpp
#include "Misc/AutomationTest.h"
#include "RunlineFirstPerson.h"
#include "RunlineArmsSolver.h"
#include "RunlineUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

// Unreal draws a first-person primitive with its sideways and up view offsets scaled by
// tan(Fov/2)/tan(FirstPersonFov/2) (FViewMatrices, SceneView.cpp). A hand put at ToFirstPerson(P)
// must draw where P does, at any FOV the player or the sprint kick sets.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineArmsFirstPersonSpace, "Runline.Arms.FirstPersonSpaceAtAnyFov",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineArmsFirstPersonSpace::RunTest(const FString&)
{
	const FTransform Camera(FRotator(-20.0, 35.0, 5.0), FVector(100.0, -40.0, 170.0));
	const FVector Points[] = {Camera.TransformPosition(FVector(40.0, 20.0, -25.0)), Camera.TransformPosition(FVector(30.0, -35.0, 20.0)), Camera.TransformPosition(FVector(80.0, 0.0, -60.0))};
	for (const float Fov : {60.0f, 90.0f, 120.0f})
		for (const FVector& P : Points)
		{
			const FVector Fp = RunlineFirstPerson::ToFirstPerson(P, Camera, Fov, 70.0f);
			const FVector2D Want = RunlineFirstPerson::ScreenOf(P, Camera, Fov, 70.0f, false);
			const FVector2D Got = RunlineFirstPerson::ScreenOf(Fp, Camera, Fov, 70.0f, true);
			TestTrue(*FString::Printf(TEXT("fov %.0f: draws over the point"), Fov), Got.Equals(Want, 1e-6));
			TestEqual(TEXT("same depth"), Camera.InverseTransformPositionNoScale(Fp).X, Camera.InverseTransformPositionNoScale(P).X, 1e-6);
		}
	const FVector P = Points[0];
	TestTrue(TEXT("equal FOVs: no change"), RunlineFirstPerson::ToFirstPerson(P, Camera, 70.0f, 70.0f).Equals(P, 1e-6));
	return true;
}

#endif
```

- [ ] **Step 2: Run it to verify it fails**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test-unreal.ps1 -Filter Runline.Arms`
Expected: the build fails: `RunlineFirstPerson.h` not found.

- [ ] **Step 3: Implement**

`Runline.Build.cs`: add `"AnimationCore"` to `PublicDependencyModuleNames`, and `PublicIncludePaths.Add("Runline/Runner/Arms");` after `PublicIncludePaths.Add("Runline/Runner");`.

`Runner/Arms/RunlineFirstPerson.h`:

```cpp
#pragma once
#include "CoreMinimal.h"

/// Unreal's first-person rendering (UCameraComponent's first-person FOV and scale) draws a
/// first-person primitive with its sideways and up view-space offsets multiplied by
/// tan(Fov/2) / tan(FirstPersonFov/2), then scaled towards the camera (FViewMatrices,
/// SceneView.cpp). The scale keeps a point's place on screen; the FOV correction moves it. These
/// undo the correction, so a hand put there draws over the world point it reaches for.
namespace RunlineFirstPerson
{
	/// The engine's factor on first-person sideways and up offsets (horizontal FOVs, degrees).
	inline double Correction(float FovDeg, float FirstPersonFovDeg)
	{
		return FMath::Tan(FMath::DegreesToRadians(FovDeg * 0.5)) / FMath::Tan(FMath::DegreesToRadians(FirstPersonFovDeg * 0.5));
	}

	/// Where a first-person primitive must be to draw over world point P (same depth).
	inline FVector ToFirstPerson(const FVector& P, const FTransform& Camera, float FovDeg, float FirstPersonFovDeg)
	{
		FVector V = Camera.InverseTransformPositionNoScale(P);
		const double K = 1.0 / Correction(FovDeg, FirstPersonFovDeg);
		V.Y *= K;
		V.Z *= K;
		return Camera.TransformPositionNoScale(V);
	}

	/// Where on screen a point draws, as the tangents of its angles off the view axis (right, up):
	/// a world point, or (bFirstPerson) a first-person one.
	inline FVector2D ScreenOf(const FVector& P, const FTransform& Camera, float FovDeg, float FirstPersonFovDeg, bool bFirstPerson)
	{
		const FVector V = Camera.InverseTransformPositionNoScale(P);
		const double K = bFirstPerson ? Correction(FovDeg, FirstPersonFovDeg) : 1.0;
		return FVector2D(V.Y * K / V.X, V.Z * K / V.X);
	}
}
```

`Runner/Arms/RunlineArmsTypes.h`:

```cpp
#pragma once
#include "CoreMinimal.h"
#include "RunlineCoreBridge.h"
#include <optional>
#include "RunlineArmsTypes.generated.h"

class URunlineMovementComponent;

namespace RunlineArms
{
	enum class EFingers : uint8 { Relaxed, Fist, Open, Grip };
	constexpr int32 Left = 0;
	constexpr int32 Right = 1;
}

/// Every number the arms use (spec M3a §3). Camera space is X forward, Y right, Z up, in cm; poses
/// are given for the right hand and mirrored (Y) for the left.
USTRUCT(BlueprintType)
struct RUNLINE_API FRunlineArmsTuning
{
	GENERATED_BODY()

	/// The ready pose, and the shoulder the reach is measured from.
	UPROPERTY(EditAnywhere, Category = "Arms") FVector Ready = FVector(32.0, 17.0, -24.0);
	UPROPERTY(EditAnywhere, Category = "Arms") FVector Shoulder = FVector(-4.0, 18.0, -20.0);
	/// A contact is fully held within Reach of the shoulder and let go by ReachFade (cm).
	UPROPERTY(EditAnywhere, Category = "Arms") float Reach = 55.0f;
	UPROPERTY(EditAnywhere, Category = "Arms") float ReachFade = 70.0f;
	/// Cross-fade between states (s).
	UPROPERTY(EditAnywhere, Category = "Arms") float BlendTime = 0.2f;
	/// The run pump: one arm cycle per StrideMetres travelled, full at SprintSpeed (m/s).
	UPROPERTY(EditAnywhere, Category = "Arms") float StrideMetres = 2.6f;
	UPROPERTY(EditAnywhere, Category = "Arms") float SprintSpeed = 7.0f;
	UPROPERTY(EditAnywhere, Category = "Arms") float PumpForward = 7.0f;
	UPROPERTY(EditAnywhere, Category = "Arms") float PumpUp = 3.0f;
	/// Sway: cm per rad/s of look, at most SwayMax; spring rates (rad/s).
	UPROPERTY(EditAnywhere, Category = "Arms") float SwayPerRadPerSec = 1.5f;
	UPROPERTY(EditAnywhere, Category = "Arms") float SwayMax = 6.0f;
	UPROPERTY(EditAnywhere, Category = "Arms") float SwayOmega = 14.0f;
	UPROPERTY(EditAnywhere, Category = "Arms") float DipOmega = 12.0f;
	/// Kicks: cm/s of dip per m/s of landing impact; jump and springboard kicks (cm/s).
	UPROPERTY(EditAnywhere, Category = "Arms") float LandKick = 6.0f;
	UPROPERTY(EditAnywhere, Category = "Arms") float JumpKick = 40.0f;
	UPROPERTY(EditAnywhere, Category = "Arms") float SpringBoardKick = 60.0f;
	UPROPERTY(EditAnywhere, Category = "Arms") float StunnedDrop = 12.0f;
	/// In the air the hands rise and spread, fully at AirFullFallSpeed (m/s down).
	UPROPERTY(EditAnywhere, Category = "Arms") float AirRise = 8.0f;
	UPROPERTY(EditAnywhere, Category = "Arms") float AirSpread = 5.0f;
	UPROPERTY(EditAnywhere, Category = "Arms") float AirFullFallSpeed = 8.0f;
	/// Looking down past PitchDropStart (deg), the hands drop by up to PitchDrop (cm) at -90.
	UPROPERTY(EditAnywhere, Category = "Arms") float PitchDropStart = 45.0f;
	UPROPERTY(EditAnywhere, Category = "Arms") float PitchDrop = 10.0f;
	/// Hands on a lip: apart by HandSpacing, GripDepth over the edge; HandLift when moving hand over hand.
	UPROPERTY(EditAnywhere, Category = "Arms") float HandSpacing = 40.0f;
	UPROPERTY(EditAnywhere, Category = "Arms") float GripDepth = 1.5f;
	UPROPERTY(EditAnywhere, Category = "Arms") float HandLift = 6.0f;
	/// Wallrun: the wall hand plants over WallReachTime (s), ahead and below the eye.
	UPROPERTY(EditAnywhere, Category = "Arms") float WallReachTime = 0.12f;
	UPROPERTY(EditAnywhere, Category = "Arms") float WallHandForward = 25.0f;
	UPROPERTY(EditAnywhere, Category = "Arms") float WallHandBelowEye = 20.0f;
	/// Wallclimb: each hand reaches ClimbReach up per ClimbCycle (s).
	UPROPERTY(EditAnywhere, Category = "Arms") float ClimbCycle = 0.5f;
	UPROPERTY(EditAnywhere, Category = "Arms") float ClimbReach = 30.0f;
	UPROPERTY(EditAnywhere, Category = "Arms") float ClimbBelowEye = 10.0f;
	/// Turning off a wallclimb: the hands let go over TurnOffTime (s).
	UPROPERTY(EditAnywhere, Category = "Arms") float TurnOffTime = 0.2f;
	/// Slide (the left hand leads), roll, lying down; getting up pushes GetUpPush ahead of the feet.
	UPROPERTY(EditAnywhere, Category = "Arms") float SlideEaseIn = 0.15f;
	UPROPERTY(EditAnywhere, Category = "Arms") FVector SlideLead = FVector(42.0, 12.0, -34.0);
	UPROPERTY(EditAnywhere, Category = "Arms") FVector SlideTrail = FVector(12.0, 22.0, -30.0);
	UPROPERTY(EditAnywhere, Category = "Arms") FVector RollTuck = FVector(22.0, 8.0, -16.0);
	UPROPERTY(EditAnywhere, Category = "Arms") FVector LieDown = FVector(10.0, 22.0, -60.0);
	UPROPERTY(EditAnywhere, Category = "Arms") float GetUpPush = 30.0f;
	/// Vault: the plant lets go over this fraction of the way down; pull-up lets go after this
	/// fraction of the move.
	UPROPERTY(EditAnywhere, Category = "Arms") float VaultRelease = 0.3f;
	UPROPERTY(EditAnywhere, Category = "Arms") float PullUpRelease = 0.85f;
	/// Springs step at most this long (s).
	UPROPERTY(EditAnywhere, Category = "Arms") float MaxSubStep = 1.0f / 120.0f;
	/// The arms mesh's head bone sits this far from the camera (camera space, cm).
	UPROPERTY(EditAnywhere, Category = "Arms") FVector MeshOffset = FVector(-6.0, 0.0, -4.0);
};

/// One hand's goal this frame (world space, cm). Position is already in first-person space
/// (RunlineFirstPerson::ToFirstPerson), so a hand on a contact draws over it.
struct FRunlineHand
{
	FVector Position = FVector::ZeroVector;
	/// Where the fingers point and the way the palm faces (unit vectors, world).
	FVector FingerDir = FVector::ForwardVector;
	FVector PalmDir = -FVector::UpVector;
	RunlineArms::EFingers Fingers = RunlineArms::EFingers::Relaxed;
	/// How much of the hand is on its state's target rather than the ready pose (0 when a contact
	/// is out of reach).
	float Weight = 0.0f;
	/// The contact the hand reaches for, in true world space (tests, debug drawing).
	TOptional<FVector> Contact;
};

struct FRunlineArmsPose
{
	FRunlineHand Hands[2];
	/// The camera the pose was solved for (the elbows are placed from it).
	FTransform Camera;
};

/// What the solver reads each render frame.
struct FRunlineArmsInput
{
	runline::State State = runline::Ground{};
	std::optional<runline::Shimmy> Shimmy;
	runline::Vec3 Feet{};
	runline::Vec3 Vel{};
	runline::Tuning CoreTuning{};
	/// The rendered camera (world, cm) and its FOV; the arms' first-person FOV (degrees).
	FTransform Camera;
	float Fov = 90.0f;
	float FirstPersonFov = 70.0f;
	/// The core's step, and how far the render is past the last one (0..1).
	float StepSeconds = 1.0f / 60.0f;
	float StepAlpha = 0.0f;
	/// Game seconds since the last solve.
	float Dt = 0.0f;
	/// Every event of the steps run since the last solve.
	TArray<runline::Event> Events;
	/// URunlineMovementComponent::GetSnapCount: a change means the view was cut.
	int32 SnapCount = 0;

	static FRunlineArmsInput From(const URunlineMovementComponent& M, const FTransform& Camera, float Fov, float FirstPersonFov, float Dt);
};
```

`Runner/Arms/RunlineArmsTypes.cpp`:

```cpp
#include "RunlineArmsTypes.h"
#include "RunlineMovementComponent.h"

FRunlineArmsInput FRunlineArmsInput::From(const URunlineMovementComponent& M, const FTransform& Camera, float Fov, float FirstPersonFov, float Dt)
{
	const runline::Controller& C = M.Core();
	FRunlineArmsInput In;
	In.State = C.state;
	In.Shimmy = C.shimmy;
	In.Feet = C.feet;
	In.Vel = C.vel;
	In.CoreTuning = C.tuning;
	In.Camera = Camera;
	In.Fov = Fov;
	In.FirstPersonFov = FirstPersonFov;
	In.StepSeconds = URunlineMovementComponent::StepSeconds;
	In.StepAlpha = M.GetStepAlpha();
	In.Dt = Dt;
	In.Events = M.GetFrameEvents();
	In.SnapCount = M.GetSnapCount();
	return In;
}
```

Also create `Runner/Arms/RunlineArmsSolver.h` with only the class shell, so the test's include resolves (Task 3 fills it in):

```cpp
#pragma once
#include "RunlineArmsTypes.h"

class RUNLINE_API FRunlineArmsSolver
{
public:
	FRunlineArmsTuning Tuning;
};
```

- [ ] **Step 4: Run the test**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test-unreal.ps1 -Filter Runline.Arms`
Expected: `Runline.Arms.FirstPersonSpaceAtAnyFov` passes.

- [ ] **Step 5: Commit**

```bash
git add Unreal/Runline/Source/Runline/Runline.Build.cs Unreal/Runline/Source/Runline/Runner/Arms Unreal/Runline/Source/Runline/Tests/ArmsSolverTests.cpp
git commit -m "M3a: arm types, tuning and first-person render space"
```

---

### Task 3: The solver: ready pose, motion, blends, snaps

**Files:**
- Modify: `Unreal/Runline/Source/Runline/Runner/Arms/RunlineArmsSolver.h`
- Create: `Unreal/Runline/Source/Runline/Runner/Arms/RunlineArmsSolver.cpp`
- Test: `Unreal/Runline/Source/Runline/Tests/ArmsSolverTests.cpp`

**Interfaces:**
- Consumes: Task 2's types and `RunlineFirstPerson::ToFirstPerson`.
- Produces: `FRunlineArmsPose FRunlineArmsSolver::Solve(const FRunlineArmsInput&)`, `void Reset()`, `float GetPumpPhase() const`, `float GetDip() const`; the private hooks Task 4 and Task 5 fill in: `void Goals(const FRunlineArmsInput&, FGoal Out[2]) const` and `FGoal ContactGoal(...) const`.

- [ ] **Step 1: Write the failing tests** (append to ArmsSolverTests.cpp, inside `#if`, before the existing test; add the helpers at the top of the `#if` block)

```cpp
namespace
{
	constexpr float Frame = 1.0f / 60.0f;
	using RunlineArms::Left;
	using RunlineArms::Right;

	/// A runner at Feet facing core yaw 0 (Unreal +X), the camera at her standing eye.
	FRunlineArmsInput At(const runline::State& S, runline::Vec3 Feet, double PitchDeg = 0.0)
	{
		FRunlineArmsInput In;
		In.State = S;
		In.Feet = Feet;
		const runline::Tuning& Tu = In.CoreTuning;
		const runline::Vec3 Eye{Feet.x, Feet.y + Tu.standHeight - Tu.eyeFromTop, Feet.z};
		In.Camera = FTransform(FRotator(PitchDeg, 0.0, 0.0), RunlineUnits::ToUnrealPosition(Eye));
		In.Dt = Frame;
		return In;
	}
	FRunlineArmsPose Settle(FRunlineArmsSolver& S, const FRunlineArmsInput& In, int32 Frames = 90)
	{
		FRunlineArmsPose P;
		for (int32 I = 0; I < Frames; ++I) P = S.Solve(In);
		return P;
	}
	FVector Local(const FRunlineArmsPose& P, int32 Hand) { return P.Camera.InverseTransformPositionNoScale(P.Hands[Hand].Position); }
	FVector Mirror(FVector V, int32 Hand) { V.Y *= Hand == Right ? 1.0 : -1.0; return V; }
	bool Finite(const FRunlineArmsPose& P)
	{
		for (const FRunlineHand& H : P.Hands)
			if (H.Position.ContainsNaN() || H.FingerDir.ContainsNaN() || H.PalmDir.ContainsNaN() || !FMath::IsFinite(H.Weight)) return false;
		return true;
	}
	runline::Event Ev(runline::EventKind K, float Impact = 0.0f) { runline::Event E{K}; E.impact = Impact; return E; }
}

// Standing still: both hands at the ready pose, mirrored, and they stay put.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineArmsReady, "Runline.Arms.ReadyPoseStandingStill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineArmsReady::RunTest(const FString&)
{
	FRunlineArmsSolver S;
	const FRunlineArmsInput In = At(runline::Ground{}, {0.0f, 0.0f, 0.0f});
	const FRunlineArmsPose A = Settle(S, In);
	const FRunlineArmsPose B = S.Solve(In);
	for (const int32 H : {Left, Right})
	{
		TestTrue(TEXT("at the ready pose"), Local(A, H).Equals(Mirror(S.Tuning.Ready, H), 0.01));
		TestTrue(TEXT("still"), A.Hands[H].Position.Equals(B.Hands[H].Position, 1e-4));
	}
	return true;
}

// The run pump's phase is distance travelled over the stride, so it can't drift from her steps.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineArmsPump, "Runline.Arms.PumpFollowsDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineArmsPump::RunTest(const FString&)
{
	FRunlineArmsSolver S;
	FRunlineArmsInput In = At(runline::Ground{}, {0.0f, 0.0f, 0.0f});
	In.Vel = {0.0f, 0.0f, -5.0f};
	for (int32 I = 0; I < 37; ++I) S.Solve(In);
	const float Metres = 5.0f * 37.0f * Frame;
	const float Want = FMath::Fmod(Metres / S.Tuning.StrideMetres, 1.0f) * UE_TWO_PI;
	TestEqual(TEXT("phase = distance / stride"), S.GetPumpPhase(), Want, 1e-3f);
	const FRunlineArmsPose P = S.Solve(In);
	TestTrue(TEXT("the hands are out of step"), !FMath::IsNearlyEqual(Local(P, Left).X, Local(P, Right).X, 0.5));
	return true;
}

// A landing dips the arms by its impact, and they come back.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineArmsLand, "Runline.Arms.LandingDips",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineArmsLand::RunTest(const FString&)
{
	FRunlineArmsSolver S;
	FRunlineArmsInput In = At(runline::Ground{}, {0.0f, 0.0f, 0.0f});
	Settle(S, In);
	In.Events = {Ev(runline::EventKind::Land, 8.0f)};
	S.Solve(In);
	In.Events.Reset();
	float Lowest = 0.0f;
	for (int32 I = 0; I < 20; ++I) { S.Solve(In); Lowest = FMath::Min(Lowest, S.GetDip()); }
	TestTrue(TEXT("dipped"), Lowest < -1.0f);
	Settle(S, In, 120);
	TestEqual(TEXT("back at rest"), S.GetDip(), 0.0f, 0.1f);
	return true;
}

// Review Focus 2: a 2-second frame stays finite and near rest (the springs sub-step).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineArmsLongFrame, "Runline.Arms.LongFrameStaysStable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineArmsLongFrame::RunTest(const FString&)
{
	FRunlineArmsSolver S;
	FRunlineArmsInput In = At(runline::Ground{}, {0.0f, 0.0f, 0.0f});
	Settle(S, In);
	In.Events = {Ev(runline::EventKind::Land, 12.0f)};
	In.Dt = 2.0f;
	const FRunlineArmsPose P = S.Solve(In);
	TestTrue(TEXT("finite"), Finite(P));
	TestTrue(TEXT("near rest after 2 s"), FMath::Abs(S.GetDip()) < 0.5f);
	return true;
}

// Review Focus 5: a snap (respawn, death) resets springs and cuts straight to the new pose.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineArmsSnap, "Runline.Arms.SnapResets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineArmsSnap::RunTest(const FString&)
{
	FRunlineArmsSolver S;
	FRunlineArmsInput In = At(runline::Roll{}, {0.0f, 0.0f, 0.0f});
	Settle(S, In);
	In.Events = {Ev(runline::EventKind::Land, 8.0f)};
	S.Solve(In);
	In.Events.Reset();
	FRunlineArmsInput After = At(runline::Ground{}, {30.0f, 0.0f, 30.0f});
	After.SnapCount = In.SnapCount + 1;
	const FRunlineArmsPose P = S.Solve(After);
	TestEqual(TEXT("no dip carried over"), S.GetDip(), 0.0f, 1e-6f);
	TestTrue(TEXT("no blend from the old pose"), Local(P, Right).Equals(S.Tuning.Ready, 0.01));
	return true;
}

// A state change cross-fades: no frame jumps, and the new pose is reached.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineArmsBlend, "Runline.Arms.StateChangeBlends",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineArmsBlend::RunTest(const FString&)
{
	FRunlineArmsSolver S;
	const FRunlineArmsPose Before = Settle(S, At(runline::Ground{}, {0.0f, 0.0f, 0.0f}));
	const FRunlineArmsPose First = S.Solve(At(runline::Roll{}, {0.0f, 0.0f, 0.0f}));
	TestTrue(TEXT("the first frame barely moves"), FVector::Dist(Local(First, Right), Local(Before, Right)) < 3.0);
	const FRunlineArmsPose End = Settle(S, At(runline::Roll{}, {0.0f, 0.0f, 0.0f}), 30);
	TestTrue(TEXT("tucked"), Local(End, Right).Equals(S.Tuning.RollTuck, 0.01));
	TestTrue(TEXT("fists"), End.Hands[Right].Fingers == RunlineArms::EFingers::Fist);
	return true;
}

// Looking straight down the hands drop out of the way; at +-89 deg everything stays finite.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineArmsPitch, "Runline.Arms.SteepPitchKeepsHandsLow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineArmsPitch::RunTest(const FString&)
{
	FRunlineArmsSolver S;
	const FRunlineArmsPose Down = Settle(S, At(runline::Ground{}, {0.0f, 0.0f, 0.0f}, -89.0));
	TestTrue(TEXT("finite looking down"), Finite(Down));
	TestTrue(TEXT("dropped"), Local(Down, Right).Z < S.Tuning.Ready.Z - S.Tuning.PitchDrop * 0.9);
	S.Reset();
	TestTrue(TEXT("finite looking up"), Finite(Settle(S, At(runline::Ground{}, {0.0f, 0.0f, 0.0f}, 89.0))));
	return true;
}

// Bad input (a NaN from anywhere) never reaches the mesh: the ready pose instead.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineArmsNaN, "Runline.Arms.NonFiniteFallsBackToReady",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineArmsNaN::RunTest(const FString&)
{
	FRunlineArmsSolver S;
	FRunlineArmsInput In = At(runline::Ground{}, {0.0f, 0.0f, 0.0f});
	In.Vel = {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
	const FRunlineArmsPose P = S.Solve(In);
	TestTrue(TEXT("finite"), Finite(P));
	TestTrue(TEXT("ready"), Local(P, Right).Equals(S.Tuning.Ready, 0.01));
	return true;
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test-unreal.ps1 -Filter Runline.Arms`
Expected: the build fails: `Solve` is not a member of `FRunlineArmsSolver`.

- [ ] **Step 3: Implement**

`RunlineArmsSolver.h` (replacing the shell):

```cpp
#pragma once
#include "RunlineArmsTypes.h"

/// M3a: the first-person arms, solved from the core's state each render frame (spec
/// 2026-10-10-m3a). Each hand gets a target: a pose relative to the camera (running, sliding), or
/// a point on a surface the core found (the ledge lip, the wall, the vault's hand plant), reached
/// by URunlineArmsAnimInstance's IK. Cosmetic: nothing here feeds back into the core.
class RUNLINE_API FRunlineArmsSolver
{
public:
	FRunlineArmsTuning Tuning;

	FRunlineArmsPose Solve(const FRunlineArmsInput& In);
	/// Springs, pump and blends back to rest: the next solve carries nothing over.
	void Reset();

	float GetPumpPhase() const { return Phase; }
	float GetDip() const { return Dip; }

private:
	/// One hand's goal before the shared motion and the cross-fade (camera space).
	struct FGoal
	{
		FVector Pos = FVector::ZeroVector;
		FVector Finger = FVector::ForwardVector;
		FVector Palm = -FVector::UpVector;
		RunlineArms::EFingers Fingers = RunlineArms::EFingers::Relaxed;
		/// Over the ready pose.
		float Weight = 0.0f;
		/// World-anchored: no sway or dip on top.
		bool bContact = false;
		TOptional<FVector> Contact;
	};

	void StepMotion(const FRunlineArmsInput& In);
	void Goals(const FRunlineArmsInput& In, FGoal Out[2]) const;
	/// The ready pose, dropped when looking steeply down (camera space).
	FVector ReadyPos(int32 Hand, const FRunlineArmsInput& In) const;
	/// The ready pose plus the run pump at Amp (0..1).
	FVector RunPos(int32 Hand, float Amp, const FRunlineArmsInput& In) const;
	/// A hand on world point At, moved into first-person space, faded out past reach.
	FGoal ContactGoal(const FRunlineArmsInput& In, int32 Hand, const FVector& At, const FVector& Finger, const FVector& Palm, RunlineArms::EFingers Fingers, float Weight) const;
	/// Which behaviour the state picks (a change starts a cross-fade).
	static int32 Key(const FRunlineArmsInput& In);

	float SwayY = 0.0f, SwayYVel = 0.0f, SwayZ = 0.0f, SwayZVel = 0.0f;
	float Dip = 0.0f, DipVel = 0.0f;
	float Swing = 0.0f, SwingVel = 0.0f;
	float Distance = 0.0f, Phase = 0.0f;
	TOptional<FQuat> PrevCamera;
	int32 LastKey = INDEX_NONE;
	float BlendT = 0.0f;
	FVector BlendFrom[2] = {FVector::ZeroVector, FVector::ZeroVector};
	FVector LastOut[2] = {FVector::ZeroVector, FVector::ZeroVector};
	TOptional<int32> LastSnap;
};
```

`RunlineArmsSolver.cpp`:

```cpp
#include "RunlineArmsSolver.h"
#include "RunlineFirstPerson.h"
#include "RunlineUnits.h"

namespace
{
	using RunlineArms::EFingers;
	constexpr int32 L = RunlineArms::Left;
	constexpr int32 R = RunlineArms::Right;

	float Side(int32 Hand) { return Hand == R ? 1.0f : -1.0f; }
	float Smooth(float X)
	{
		X = FMath::Clamp(X, 0.0f, 1.0f);
		return X * X * (3.0f - 2.0f * X);
	}
	/// A right-hand camera-space pose for the given hand.
	FVector ForHand(FVector P, int32 Hand)
	{
		P.Y *= Side(Hand);
		return P;
	}
	/// A critically damped spring, one semi-implicit Euler step of H seconds.
	void Spring(float& X, float& V, float Target, float Omega, float H)
	{
		V += (Omega * Omega * (Target - X) - 2.0f * Omega * V) * H;
		X += V * H;
	}
	bool Finite(const FVector& V) { return !V.ContainsNaN() && V.GetAbsMax() < 1e7; }
}

void FRunlineArmsSolver::Reset()
{
	SwayY = SwayYVel = SwayZ = SwayZVel = 0.0f;
	Dip = DipVel = Swing = SwingVel = 0.0f;
	Distance = Phase = 0.0f;
	PrevCamera.Reset();
	LastKey = INDEX_NONE;
	BlendT = 0.0f;
}

void FRunlineArmsSolver::StepMotion(const FRunlineArmsInput& In)
{
	const FRunlineArmsTuning& T = Tuning;
	// Look rate (rad/s) from the camera's turn since the last solve: the arms lag it.
	float YawRate = 0.0f, PitchRate = 0.0f;
	if (PrevCamera && In.Dt > 0.0f)
	{
		const FRotator Turn = (PrevCamera->Inverse() * In.Camera.GetRotation()).Rotator();
		YawRate = FMath::DegreesToRadians(float(Turn.Yaw)) / In.Dt;
		PitchRate = FMath::DegreesToRadians(float(Turn.Pitch)) / In.Dt;
	}
	PrevCamera = In.Camera.GetRotation();
	const float SwayYTarget = FMath::Clamp(-YawRate * T.SwayPerRadPerSec, -T.SwayMax, T.SwayMax);
	const float SwayZTarget = FMath::Clamp(-PitchRate * T.SwayPerRadPerSec, -T.SwayMax, T.SwayMax);
	for (const runline::Event& E : In.Events)
	{
		switch (E.kind)
		{
		case runline::EventKind::Land:
		case runline::EventKind::HardLand:
		case runline::EventKind::SoftLand: DipVel -= T.LandKick * E.impact; break;
		case runline::EventKind::Jump:
		case runline::EventKind::WallJump: DipVel += T.JumpKick; break;
		case runline::EventKind::SpringBoard: SwingVel += T.SpringBoardKick; break;
		default: break;
		}
	}
	const float DipRest = std::holds_alternative<runline::Stunned>(In.State) ? -T.StunnedDrop : 0.0f;
	// The pump's phase is distance travelled (m) over the stride: in step with her feet.
	const float Speed = FMath::Sqrt(In.Vel.x * In.Vel.x + In.Vel.z * In.Vel.z);
	Distance = FMath::Fmod(Distance + Speed * In.Dt, T.StrideMetres);
	Phase = Distance / T.StrideMetres * UE_TWO_PI;
	for (float Left = In.Dt; Left > 0.0f;)
	{
		const float H = FMath::Min(Left, T.MaxSubStep);
		Spring(SwayY, SwayYVel, SwayYTarget, T.SwayOmega, H);
		Spring(SwayZ, SwayZVel, SwayZTarget, T.SwayOmega, H);
		Spring(Dip, DipVel, DipRest, T.DipOmega, H);
		Spring(Swing, SwingVel, 0.0f, T.DipOmega, H);
		Left -= H;
	}
}

FVector FRunlineArmsSolver::ReadyPos(int32 Hand, const FRunlineArmsInput& In) const
{
	const FRunlineArmsTuning& T = Tuning;
	FVector P = ForHand(T.Ready, Hand);
	const float Pitch = float(In.Camera.Rotator().Pitch);
	const float Down = FMath::Clamp((-Pitch - T.PitchDropStart) / (90.0f - T.PitchDropStart), 0.0f, 1.0f);
	P.Z -= T.PitchDrop * Down;
	P.X -= T.PitchDrop * 0.5f * Down;
	return P;
}

FVector FRunlineArmsSolver::RunPos(int32 Hand, float Amp, const FRunlineArmsInput& In) const
{
	const float A = Phase + (Hand == R ? UE_PI : 0.0f);
	return ReadyPos(Hand, In) + FVector(Tuning.PumpForward * Amp * FMath::Sin(A), 0.0, Tuning.PumpUp * Amp * FMath::Sin(2.0f * A));
}

FRunlineArmsSolver::FGoal FRunlineArmsSolver::ContactGoal(const FRunlineArmsInput& In, int32 Hand, const FVector& At, const FVector& Finger, const FVector& Palm, EFingers Fingers, float Weight) const
{
	FGoal G;
	G.bContact = true;
	G.Contact = At;
	G.Fingers = Fingers;
	G.Pos = In.Camera.InverseTransformPositionNoScale(RunlineFirstPerson::ToFirstPerson(At, In.Camera, In.Fov, In.FirstPersonFov));
	G.Finger = In.Camera.InverseTransformVectorNoScale(Finger).GetSafeNormal();
	G.Palm = In.Camera.InverseTransformVectorNoScale(Palm).GetSafeNormal();
	const float Dist = float(FVector::Dist(G.Pos, ForHand(Tuning.Shoulder, Hand)));
	const float Held = 1.0f - FMath::Clamp((Dist - Tuning.Reach) / (Tuning.ReachFade - Tuning.Reach), 0.0f, 1.0f);
	G.Weight = FMath::Clamp(Weight, 0.0f, 1.0f) * Held;
	return G;
}

int32 FRunlineArmsSolver::Key(const FRunlineArmsInput& In)
{
	int32 K = int32(In.State.index()) * 8;
	if (const auto* Tr = std::get_if<runline::Traverse>(&In.State)) K += int32(Tr->kind);
	return K;
}

void FRunlineArmsSolver::Goals(const FRunlineArmsInput& In, FGoal Out[2]) const
{
	const FRunlineArmsTuning& T = Tuning;
	const float Speed = FMath::Sqrt(In.Vel.x * In.Vel.x + In.Vel.z * In.Vel.z);
	const float Amp = FMath::Clamp(Speed / T.SprintSpeed, 0.0f, 1.0f);
	for (int32 H = 0; H < 2; ++H)
	{
		Out[H] = FGoal{};
		Out[H].Pos = ReadyPos(H, In);
		Out[H].Weight = 1.0f;
	}
	const auto Pose = [&](int32 H, const FVector& Pos, EFingers F, float Weight)
	{
		Out[H].Pos = Pos;
		Out[H].Fingers = F;
		Out[H].Weight = Weight;
	};
	const runline::State& S = In.State;
	if (std::holds_alternative<runline::Ground>(S) || std::holds_alternative<runline::SoftLand>(S) || std::holds_alternative<runline::Stunned>(S))
	{
		for (int32 H = 0; H < 2; ++H) Pose(H, RunPos(H, Amp, In), EFingers::Relaxed, 1.0f);
	}
	else if (std::holds_alternative<runline::Air>(S))
	{
		const float F = FMath::Clamp(-In.Vel.y / T.AirFullFallSpeed, 0.0f, 1.0f);
		for (int32 H = 0; H < 2; ++H)
			Pose(H, ReadyPos(H, In) + FVector(0.0, Side(H) * T.AirSpread * F, T.AirRise * F), F > 0.5f ? EFingers::Open : EFingers::Relaxed, 1.0f);
	}
	else if (std::holds_alternative<runline::Roll>(S))
	{
		for (int32 H = 0; H < 2; ++H) Pose(H, ForHand(T.RollTuck, H), EFingers::Fist, 1.0f);
	}
	// Task 4 adds the contact states and Task 5 the rest; anything else holds the ready pose.
}

FRunlineArmsPose FRunlineArmsSolver::Solve(const FRunlineArmsInput& In)
{
	if (!LastSnap || *LastSnap != In.SnapCount)
	{
		Reset();
		LastSnap = In.SnapCount;
	}
	StepMotion(In);
	FGoal G[2];
	Goals(In, G);
	const int32 K = Key(In);
	if (K != LastKey)
	{
		const bool bFirst = LastKey == INDEX_NONE;
		LastKey = K;
		BlendFrom[0] = LastOut[0];
		BlendFrom[1] = LastOut[1];
		BlendT = bFirst ? Tuning.BlendTime : 0.0f;
	}
	else BlendT += In.Dt;
	const float Blend = Tuning.BlendTime > 0.0f ? Smooth(BlendT / Tuning.BlendTime) : 1.0f;
	const FVector Motion(Swing, SwayY, SwayZ + Dip);
	FRunlineArmsPose Pose;
	Pose.Camera = In.Camera;
	bool bFinite = true;
	for (int32 H = 0; H < 2; ++H)
	{
		const FVector Ready = ReadyPos(H, In) + Motion;
		const FVector Goal = G[H].bContact ? G[H].Pos : G[H].Pos + Motion;
		const FVector P = FMath::Lerp(BlendFrom[H], FMath::Lerp(Ready, Goal, double(G[H].Weight)), double(Blend));
		LastOut[H] = P;
		FRunlineHand& Hand = Pose.Hands[H];
		Hand.Position = In.Camera.TransformPositionNoScale(P);
		Hand.FingerDir = In.Camera.TransformVectorNoScale(FMath::Lerp(FVector::ForwardVector, G[H].Finger, double(G[H].Weight)).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector));
		Hand.PalmDir = In.Camera.TransformVectorNoScale(FMath::Lerp(-FVector::UpVector, G[H].Palm, double(G[H].Weight)).GetSafeNormal(UE_SMALL_NUMBER, -FVector::UpVector));
		Hand.Fingers = G[H].Fingers;
		Hand.Weight = G[H].Weight;
		Hand.Contact = G[H].Contact;
		bFinite &= Finite(P) && Finite(Hand.FingerDir) && Finite(Hand.PalmDir) && FMath::IsFinite(Hand.Weight);
	}
	if (bFinite) return Pose;
	// Something upstream wasn't a number: hold the ready pose, start again from rest.
	Reset();
	LastSnap = In.SnapCount;
	for (int32 H = 0; H < 2; ++H)
	{
		LastOut[H] = ReadyPos(H, In);
		Pose.Hands[H] = FRunlineHand{};
		Pose.Hands[H].Position = In.Camera.TransformPositionNoScale(LastOut[H]);
		Pose.Hands[H].FingerDir = In.Camera.GetRotation().GetForwardVector();
		Pose.Hands[H].PalmDir = -In.Camera.GetRotation().GetUpVector();
	}
	return Pose;
}
```

Note on `ReadyPos` with a NaN camera: the NaN fallback assumes the camera is finite (it comes from the movement component's already-guarded view). The NaN test feeds a NaN velocity, which reaches `Distance`, `Phase` and the pump; `Reset()` clears them.

- [ ] **Step 4: Run the tests**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test-unreal.ps1 -Filter Runline.Arms`
Expected: all 9 Runline.Arms tests pass (1 from Task 2, 8 new).

- [ ] **Step 5: Commit**

```bash
git add Unreal/Runline/Source/Runline/Runner/Arms Unreal/Runline/Source/Runline/Tests/ArmsSolverTests.cpp
git commit -m "M3a: arms solver: ready pose, run pump, sway, landing dip, blends, snaps"
```

---

### Task 4: Contact states: hands on what the core found

**Files:**
- Modify: `Unreal/Runline/Source/Runline/Runner/Arms/RunlineArmsSolver.cpp` (`Goals`)
- Test: `Unreal/Runline/Source/Runline/Tests/ArmsSolverTests.cpp`

**Interfaces:**
- Consumes: Task 3's `Goals`, `ContactGoal`, `RunPos`.
- Produces: `Goals` handles `LedgeHang` (with and without `In.Shimmy`), `GrabTransfer`, `WallRun`, `WallClimb`, `WallClimbTurned`, `Traverse` (PullUp, Mantle) and `Vault`.

The test scene: she faces Unreal +X (core yaw 0 faces core -Z). A wall in front of her is a face at core z = 0 with normal (0, 0, 1). A wall on her right (Unreal +Y) has core normal (-1, 0, 0), since `ToCoreDirection((0,-1,0))` is (-1, 0, 0).

- [ ] **Step 1: Write the failing tests** (append inside the `#if`)

```cpp
namespace
{
	/// The lip she hangs from: the wall face at core z = 0, the ledge top at y = Ledge.
	constexpr float LedgeTop = 2.0f;
	runline::Vec3 HangFeet(const runline::Tuning& Tu, float X = 0.0f) { return {X, LedgeTop - Tu.hangHandsAboveFeet, Tu.halfWidth}; }
	/// Hanging, the camera is at her standing eye above the feet, facing the wall (as At).
	FRunlineArmsInput Hanging(const runline::State& S, runline::Vec3 Feet) { return At(S, Feet); }
	FVector LipPoint(float CoreX) { return RunlineUnits::ToUnrealPosition({CoreX, LedgeTop, 0.0f}); }
	const FVector LipDir = FVector(0.0, 1.0, 0.0);   // the lip runs along Unreal Y (core X)
}

// The hands grip the lip itself, either side of her, and draw over it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineArmsHang, "Runline.Arms.LedgeHangHandsOnTheLip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineArmsHang::RunTest(const FString&)
{
	FRunlineArmsSolver S;
	const runline::Tuning Tu;
	const FRunlineArmsInput In = Hanging(runline::LedgeHang{{0.0f, 0.0f, 1.0f}, LedgeTop, false}, HangFeet(Tu));
	const FRunlineArmsPose P = Settle(S, In);
	const double EyeY = In.Camera.GetLocation().Y;
	for (const int32 H : {Left, Right})
	{
		const FRunlineHand& Hd = P.Hands[H];
		TestTrue(TEXT("has a contact"), Hd.Contact.IsSet());
		if (!Hd.Contact) return false;
		TestTrue(TEXT("on the lip (2 cm)"), FMath::PointDistToLine(*Hd.Contact, LipDir, LipPoint(0.0f)) <= 2.0);
		TestTrue(TEXT("held"), Hd.Weight > 0.99f);
		TestTrue(TEXT("drawn over it"), Hd.Position.Equals(RunlineFirstPerson::ToFirstPerson(*Hd.Contact, In.Camera, In.Fov, In.FirstPersonFov), 0.5));
		TestTrue(TEXT("gripping"), Hd.Fingers == RunlineArms::EFingers::Grip);
	}
	TestTrue(TEXT("either side of her eye"), P.Hands[Left].Contact->Y < EyeY && EyeY < P.Hands[Right].Contact->Y);
	return true;
}

// Review Focus 1: hanging with her back to the wall, the lip is behind the camera: let go by reach.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineArmsTurnedHang, "Runline.Arms.TurnedHangLetsGo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineArmsTurnedHang::RunTest(const FString&)
{
	FRunlineArmsSolver S;
	const runline::Tuning Tu;
	FRunlineArmsInput In = Hanging(runline::LedgeHang{{0.0f, 0.0f, 1.0f}, LedgeTop, true}, HangFeet(Tu));
	In.Camera.SetRotation(FRotator(0.0, 180.0, 0.0).Quaternion());
	const FRunlineArmsPose P = Settle(S, In);
	for (const int32 H : {Left, Right})
	{
		TestEqual(TEXT("let go"), P.Hands[H].Weight, 0.0f, 1e-4f);
		TestTrue(TEXT("in front of the camera"), Local(P, H).X > 10.0);
	}
	return true;
}

// Shimmying right: the leading (right) hand goes first, then the left; both end on the new spot.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineArmsShimmy, "Runline.Arms.ShimmyHandOverHand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineArmsShimmy::RunTest(const FString&)
{
	FRunlineArmsSolver S;
	const runline::Tuning Tu;
	const runline::LedgeHang Hang{{0.0f, 0.0f, 1.0f}, LedgeTop, false};
	const FRunlineArmsPose Start = Settle(S, Hanging(Hang, HangFeet(Tu)));
	runline::Shimmy Sh;
	Sh.dir = 1.0f;
	Sh.from = HangFeet(Tu);
	Sh.to = HangFeet(Tu, Tu.shimmyStep);
	Sh.t = Tu.shimmyStepTime * 0.4f;
	FRunlineArmsInput In = Hanging(Hang, Sh.from);
	In.Shimmy = Sh;
	const FRunlineArmsPose Mid = S.Solve(In);
	TestTrue(TEXT("the right hand moved first"), FVector::Dist(*Mid.Hands[Right].Contact, *Start.Hands[Right].Contact) > 20.0);
	TestTrue(TEXT("the left hasn't yet"), FVector::Dist(*Mid.Hands[Left].Contact, *Start.Hands[Left].Contact) < 0.5);
	Sh.t = Tu.shimmyStepTime;
	In.Shimmy = Sh;
	const FRunlineArmsPose End = S.Solve(In);
	const FRunlineArmsPose Want = Settle(S, Hanging(Hang, Sh.to));
	for (const int32 H : {Left, Right})
		TestTrue(TEXT("both on the new spot"), End.Hands[H].Contact->Equals(*Want.Hands[H].Contact, 0.01));
	return true;
}

// Grab transfer: the right hand reaches the higher lip first, the left follows; both end there.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineArmsTransfer, "Runline.Arms.GrabTransferReachesUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineArmsTransfer::RunTest(const FString&)
{
	FRunlineArmsSolver S;
	const runline::Tuning Tu;
	const runline::Vec3 From = HangFeet(Tu);
	const runline::Vec3 To{From.x, From.y + 1.0f, From.z};
	runline::GrabTransfer G{0.0f, From, To, 0.6f, {0.0f, 0.0f, 1.0f}, LedgeTop + 1.0f};
	G.t = 0.3f;
	const FRunlineArmsPose Mid = S.Solve(Hanging(G, From));
	TestTrue(TEXT("right hand higher than the left mid-reach"), Mid.Hands[Right].Contact->Z > Mid.Hands[Left].Contact->Z + 20.0);
	G.t = 0.6f;
	const FRunlineArmsPose End = S.Solve(Hanging(G, To));
	for (const int32 H : {Left, Right})
		TestEqual(TEXT("on the upper lip"), End.Hands[H].Contact->Z, RunlineUnits::ToUnrealPosition({0.0f, LedgeTop + 1.0f, 0.0f}).Z, 0.1);
	return true;
}

// Review Focus 3: zero-length timed moves are finite and end on their targets.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineArmsZeroDur, "Runline.Arms.ZeroDurationsAreFinite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineArmsZeroDur::RunTest(const FString&)
{
	FRunlineArmsSolver S;
	const runline::Tuning Tu;
	const runline::Vec3 Feet = HangFeet(Tu);
	const FRunlineArmsPose P = Settle(S, Hanging(runline::GrabTransfer{0.0f, Feet, Feet, 0.0f, {0.0f, 0.0f, 1.0f}, LedgeTop}, Feet));
	TestTrue(TEXT("transfer of 0 s: finite"), Finite(P));
	runline::Shimmy Sh;
	Sh.dir = -1.0f;
	Sh.from = Sh.to = Feet;
	FRunlineArmsInput In = Hanging(runline::LedgeHang{{0.0f, 0.0f, 1.0f}, LedgeTop, false}, Feet);
	In.Shimmy = Sh;
	TestTrue(TEXT("shimmy of no length: finite"), Finite(Settle(S, In)));
	return true;
}

// Wallrun with the wall on her right: the right hand plants on the wall plane, the left pumps.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineArmsWallRun, "Runline.Arms.WallRunHandOnTheWall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineArmsWallRun::RunTest(const FString&)
{
	FRunlineArmsSolver S;
	const runline::Tuning Tu;
	const runline::Vec3 Feet{0.0f, 0.0f, 0.0f};
	const FVector WallPoint = RunlineUnits::ToUnrealPosition({Feet.x + Tu.halfWidth, 0.0f, 0.0f});   // the face, on her right
	const FVector N(0.0, -1.0, 0.0);
	runline::WallRun Wr{{-1.0f, 0.0f, 0.0f}, 0.0f};
	const FRunlineArmsPose First = S.Solve(At(Wr, Feet));
	TestTrue(TEXT("reaching, not planted, at the start"), First.Hands[Right].Weight < 0.2f);
	Wr.t = 0.5f;
	const FRunlineArmsPose P = Settle(S, At(Wr, Feet));
	TestTrue(TEXT("right hand planted"), P.Hands[Right].Weight > 0.99f);
	TestTrue(TEXT("on the wall plane"), FMath::Abs(FVector::DotProduct(*P.Hands[Right].Contact - WallPoint, N)) < 0.1);
	TestFalse(TEXT("the left hand has no contact"), P.Hands[Left].Contact.IsSet());
	TestTrue(TEXT("palm to the wall"), FVector::DotProduct(P.Hands[Right].PalmDir, -N) > 0.95);
	return true;
}

// Wallclimb: both hands on the wall, at different heights (hand over hand).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineArmsClimb, "Runline.Arms.WallClimbAlternates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineArmsClimb::RunTest(const FString&)
{
	FRunlineArmsSolver S;
	const runline::Tuning Tu;
	const runline::Vec3 Feet{0.0f, 0.0f, Tu.halfWidth};
	const FVector N(-1.0, 0.0, 0.0);   // core (0,0,1): towards her, Unreal -X
	const FVector Wall = RunlineUnits::ToUnrealPosition({0.0f, 0.0f, 0.0f});
	const FRunlineArmsPose P = Settle(S, At(runline::WallClimb{{0.0f, 0.0f, 1.0f}, 0.2f}, Feet));
	for (const int32 H : {Left, Right})
		TestTrue(TEXT("on the wall plane"), P.Hands[H].Contact && FMath::Abs(FVector::DotProduct(*P.Hands[H].Contact - Wall, N)) < 0.1);
	TestTrue(TEXT("hand over hand"), FMath::Abs(P.Hands[Left].Contact->Z - P.Hands[Right].Contact->Z) > 5.0);
	const FRunlineArmsPose Off = Settle(S, At(runline::WallClimbTurned{{0.0f, 0.0f, 1.0f}, 0.3f}, Feet));
	TestEqual(TEXT("turned off: let go"), Off.Hands[Right].Weight, 0.0f, 1e-4f);
	return true;
}

// Pull-up: both hands on the top edge until near the end, then off.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineArmsPullUp, "Runline.Arms.PullUpHandsOnTheEdge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineArmsPullUp::RunTest(const FString&)
{
	FRunlineArmsSolver S;
	const runline::Tuning Tu;
	const runline::Vec3 From = HangFeet(Tu);
	const runline::Vec3 To{0.0f, LedgeTop + 0.01f, -0.4f};
	runline::Traverse Tr{runline::TraverseKind::PullUp, From, To, 0.2f, 1.0f, 0.15f, {}};
	const FRunlineArmsPose P = Settle(S, Hanging(Tr, From));
	const FVector Edge = RunlineUnits::ToUnrealPosition({0.0f, LedgeTop, 0.0f});
	for (const int32 H : {Left, Right})
	{
		TestTrue(TEXT("on the edge"), P.Hands[H].Contact && FMath::PointDistToLine(*P.Hands[H].Contact, LipDir, Edge) <= 2.0);
		TestTrue(TEXT("held"), P.Hands[H].Weight > 0.99f);
	}
	Tr.t = 1.0f;
	TestEqual(TEXT("let go at the end"), S.Solve(Hanging(Tr, To)).Hands[Right].Weight, 0.0f, 1e-4f);
	return true;
}

// Vault: the hand plants on the obstacle's top (Vault.hand less the VaultType's handplant offset
// and the 90-unit half height), through the way over; off on the way down. Step-ups plant nothing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineArmsVault, "Runline.Arms.VaultHandPlant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineArmsVault::RunTest(const FString&)
{
	FRunlineArmsSolver S;
	// VaultOver (index 3): timeUp 0, timeOver 0.35, timeDown 0.3; handplant (0, 5), ledge (0, 25).
	const runline::Vec3 Ledge{0.0f, 1.0f, -1.0f};
	runline::Vault V;
	V.kind = 3;
	V.from = {0.0f, 0.0f, -0.2f};
	V.hand = {Ledge.x, Ledge.y + runline::Uu(5.0f) - runline::Uu(90.0f), Ledge.z};
	V.over = {Ledge.x, Ledge.y + runline::Uu(25.0f) - runline::Uu(90.0f), Ledge.z};
	V.end = {0.0f, 0.0f, -2.0f};
	V.t = 0.2f;
	const FRunlineArmsPose Over = Settle(S, At(V, V.Position(V.t)));
	const FVector Top = RunlineUnits::ToUnrealPosition(Ledge);
	TestTrue(TEXT("planted"), Over.Hands[Right].Contact.IsSet() && Over.Hands[Right].Weight > 0.99f);
	TestTrue(TEXT("on the obstacle's top"), Over.Hands[Right].Contact && FMath::Abs(Over.Hands[Right].Contact->Z - Top.Z) < 0.1 && FVector::Dist2D(*Over.Hands[Right].Contact, Top) <= S.Tuning.HandSpacing * 0.2 + 0.1);
	V.t = 0.35f + 0.3f * 0.5f;
	TestEqual(TEXT("let go on the way down"), Settle(S, At(V, V.Position(V.t))).Hands[Right].Weight, 0.0f, 1e-4f);
	V.kind = 1;   // stepuprightleg88: a step-up, no hands
	V.t = 0.1f;
	TestFalse(TEXT("a step-up plants nothing"), Settle(S, At(V, V.Position(V.t))).Hands[Right].Contact.IsSet());
	return true;
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test-unreal.ps1 -Filter Runline.Arms`
Expected: the new tests fail ("has a contact", "planted" and so on: these states still hold the ready pose). The Task 3 tests still pass.

- [ ] **Step 3: Implement** (in `Goals`, before the "Task 4 adds" comment; add these helpers to the anonymous namespace at the top of the file)

```cpp
	FVector W(runline::Vec3 P) { return RunlineUnits::ToUnrealPosition(P); }
	FVector Dir(runline::Vec3 V) { return RunlineUnits::ToUnrealDirection(V).GetSafeNormal(); }
	/// The wall face nearest the feet: the feet moved halfWidth against the normal (the core keeps
	/// her body against the wall), at height Y.
	runline::Vec3 FaceAt(runline::Vec3 Feet, runline::Vec3 N, float HalfWidth, float Y) { return {Feet.x - N.x * HalfWidth, Y, Feet.z - N.z * HalfWidth}; }
	/// Horizontal unit vector, or Fallback if there is none.
	FVector Flat(FVector V, const FVector& Fallback)
	{
		V.Z = 0.0;
		return V.GetSafeNormal(UE_SMALL_NUMBER, Fallback);
	}
```

and in `Goals`:

```cpp
	const runline::Tuning& Core = In.CoreTuning;
	const float Hw = Core.halfWidth;
	const float Late = In.StepAlpha * In.StepSeconds;   // the render is this far past the last step
	const FVector Up = FVector::UpVector;
	const FVector Eye = In.Camera.GetLocation();
	const FVector Facing = Flat(In.Camera.GetRotation().GetForwardVector(), FVector::ForwardVector);
	/// A hand on a lip at At (N: the wall's normal, out towards her): either side, just over the edge.
	const auto LipHand = [&](const FVector& At, const FVector& N, int32 H)
	{
		const FVector Right = FVector::CrossProduct(Up, -N).GetSafeNormal();
		return At + Right * (Side(H) * T.HandSpacing * 0.5f) - N * T.GripDepth;
	};
	const auto OnPlane = [](FVector P, const FVector& Point, const FVector& N) { return P - N * FVector::DotProduct(P - Point, N); };
	const auto Fraction = [](float Num, float Den) { return Den > 0.0f ? FMath::Clamp(Num / Den, 0.0f, 1.0f) : 1.0f; };

	if (const auto* Lh = std::get_if<runline::LedgeHang>(&S))
	{
		const FVector N = Dir(Lh->normal);
		if (In.Shimmy)
		{
			// Hand over hand: the leading hand moves in the first half of the step, the other in the second.
			const runline::Shimmy& Sh = *In.Shimmy;
			const runline::Vec3 ToNormal = Sh.corner ? *Sh.corner : Lh->normal;
			const FVector ToN = Dir(ToNormal);
			const float U = Fraction(Sh.t + Late, Sh.corner ? Core.shimmyCornerTime : Core.shimmyStepTime);
			const int32 Lead = Sh.dir > 0.0f ? R : L;
			for (int32 H = 0; H < 2; ++H)
			{
				const float K = H == Lead ? Smooth(U * 2.0f) : Smooth(U * 2.0f - 1.0f);
				const FVector A = LipHand(W(FaceAt(Sh.from, Lh->normal, Hw, Lh->ledgeY)), N, H);
				const FVector B = LipHand(W(FaceAt(Sh.to, ToNormal, Hw, Lh->ledgeY)), ToN, H);
				const FVector Nk = FMath::Lerp(N, ToN, double(K)).GetSafeNormal(UE_SMALL_NUMBER, N);
				Out[H] = ContactGoal(In, H, FMath::Lerp(A, B, double(K)) + Up * (T.HandLift * FMath::Sin(UE_PI * K)), -Nk, -Up, EFingers::Grip, 1.0f);
			}
		}
		else
		{
			const FVector Lip = W(FaceAt(In.Feet, Lh->normal, Hw, Lh->ledgeY));
			for (int32 H = 0; H < 2; ++H) Out[H] = ContactGoal(In, H, LipHand(Lip, N, H), -N, -Up, EFingers::Grip, 1.0f);
		}
	}
	else if (const auto* Gt = std::get_if<runline::GrabTransfer>(&S))
	{
		// The right hand reaches up first, the left follows.
		const FVector N = Dir(Gt->normal);
		const FVector From = W(FaceAt(Gt->from, Gt->normal, Hw, Gt->from.y + Core.hangHandsAboveFeet));
		const FVector To = W(FaceAt(Gt->to, Gt->normal, Hw, Gt->ledgeY));
		const float U = Fraction(Gt->t + Late, Gt->dur);
		for (int32 H = 0; H < 2; ++H)
		{
			const float K = H == R ? Smooth(U / 0.6f) : Smooth((U - 0.4f) / 0.6f);
			const FVector P = FMath::Lerp(LipHand(From, N, H), LipHand(To, N, H), double(K)) + Up * (T.HandLift * FMath::Sin(UE_PI * K));
			Out[H] = ContactGoal(In, H, P, -N, -Up, EFingers::Grip, 1.0f);
		}
	}
	else if (const auto* Wr = std::get_if<runline::WallRun>(&S))
	{
		// The wall-side hand plants ahead of her on the wall; the other pumps.
		const FVector N = Dir(Wr->normal);
		const FVector Wall = W(FaceAt(In.Feet, Wr->normal, Hw, In.Feet.y));
		const FVector Along = (Facing - N * FVector::DotProduct(Facing, N)).GetSafeNormal(UE_SMALL_NUMBER, Facing);
		const int32 H = FVector::DotProduct(In.Camera.GetRotation().GetRightVector(), -N) > 0.0 ? R : L;
		const FVector P = OnPlane(Eye + Along * T.WallHandForward - Up * T.WallHandBelowEye, Wall, N);
		Out[H] = ContactGoal(In, H, P, (Along + Up * 0.3).GetSafeNormal(), -N, EFingers::Open, Smooth((Wr->t + Late) / T.WallReachTime));
		Pose(1 - H, RunPos(1 - H, Amp, In), EFingers::Relaxed, 1.0f);
	}
	else if (std::holds_alternative<runline::WallClimb>(S) || std::holds_alternative<runline::WallClimbTurned>(S))
	{
		// Hand over hand up the wall; turning off it, both let go.
		const auto* Wc = std::get_if<runline::WallClimb>(&S);
		const auto* Wt = std::get_if<runline::WallClimbTurned>(&S);
		const runline::Vec3 Normal = Wc ? Wc->normal : Wt->normal;
		const float Time = (Wc ? Wc->t : Wt->t) + Late;
		const float Weight = Wc ? 1.0f : 1.0f - Smooth(Time / T.TurnOffTime);
		const FVector N = Dir(Normal);
		const FVector Wall = W(FaceAt(In.Feet, Normal, Hw, In.Feet.y));
		const FVector Right = FVector::CrossProduct(Up, -N).GetSafeNormal();
		for (int32 H = 0; H < 2; ++H)
		{
			const float Ph = FMath::Frac(Time / T.ClimbCycle + 0.5f * H);
			const float Lift = T.ClimbReach * (1.0f - FMath::Abs(2.0f * Ph - 1.0f));
			const FVector P = OnPlane(Eye + Right * (Side(H) * T.HandSpacing * 0.5f) + Up * (Lift - T.ClimbBelowEye), Wall, N);
			Out[H] = ContactGoal(In, H, P, Up, -N, EFingers::Open, Weight);
		}
	}
	else if (const auto* Tr = std::get_if<runline::Traverse>(&S); Tr && Tr->kind != runline::TraverseKind::SpringBoard)
	{
		// Pull-up and mantle: both hands on the top edge, pressing as she rises; off near the end.
		const FVector From = W(Tr->from), To = W(Tr->to);
		const FVector D = Flat(To - From, Facing);
		FVector Edge = From + D * (Hw * RunlineUnits::CmPerMetre);
		Edge.Z = To.Z - 1.0;
		const float U = Fraction(Tr->t + Late, Tr->dur);
		const float Weight = U < T.PullUpRelease ? 1.0f : 1.0f - Fraction(U - T.PullUpRelease, 1.0f - T.PullUpRelease);
		for (int32 H = 0; H < 2; ++H) Out[H] = ContactGoal(In, H, LipHand(Edge, -D, H), D, -Up, EFingers::Open, Weight);
	}
	else if (const auto* V = std::get_if<runline::Vault>(&S))
	{
		// The hand plant (the vaults, not the step-ups): Vault.hand is her feet at the plant, so the
		// top is that less the VaultType's handplant offset (along, up; Unreal units = cm) and the
		// 90-unit half height (vault.rs `at`).
		const runline::VaultType& K = V->Kind();
		if (V->kind >= 2)
		{
			const FVector D = Flat(W(V->over) - W(V->from), Facing);
			FVector Top = W(V->hand) - D * K.handplant.first;
			Top.Z += 90.0 - K.handplant.second;
			const FVector Right = FVector::CrossProduct(Up, D).GetSafeNormal();
			const float Tt = V->t + Late;
			const float Weight = Tt < K.timeUp ? Fraction(Tt, K.timeUp)
				: Tt < K.timeUp + K.timeOver ? 1.0f
				: 1.0f - Fraction(Tt - K.timeUp - K.timeOver, K.timeDown * T.VaultRelease);
			const bool bBoth = V->kind >= 4;   // the high vaults
			Out[R] = ContactGoal(In, R, Top + Right * (bBoth ? T.HandSpacing * 0.5f : T.HandSpacing * 0.2f), D, -Up, EFingers::Open, Weight);
			if (bBoth) Out[L] = ContactGoal(In, L, Top - Right * (T.HandSpacing * 0.5f), D, -Up, EFingers::Open, Weight);
		}
	}
```

`Pose`, `Amp` and `T` are already defined in `Goals` (Task 3). Add `#include <cmath>` if `FMath::Frac` is missing (it isn't in 5.8; `FMath::Frac` exists).

- [ ] **Step 4: Run the tests**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test-unreal.ps1 -Filter Runline.Arms`
Expected: all 18 Runline.Arms tests pass (9 before, 9 new).

If a "held" assertion fails because the contact is beyond `Reach`, look at the logged distance first. The tuning's `Reach` is a guess until Task 6 measures Manny's arm. Raise `Reach`/`ReachFade` only if Task 6's measured arm length allows it; otherwise ledger a ruling that moves the test's camera.

- [ ] **Step 5: Commit**

```bash
git add Unreal/Runline/Source/Runline/Runner/Arms/RunlineArmsSolver.cpp Unreal/Runline/Source/Runline/Tests/ArmsSolverTests.cpp
git commit -m "M3a: arms on the core's contacts: ledge, shimmy, grab transfer, wall, pull-up, vault"
```

---

### Task 5: The remaining states, and every transition

**Files:**
- Modify: `Unreal/Runline/Source/Runline/Runner/Arms/RunlineArmsSolver.cpp` (`Goals`)
- Test: `Unreal/Runline/Source/Runline/Tests/ArmsSolverTests.cpp`

**Interfaces:**
- Consumes: Task 3 and 4's `Goals`.
- Produces: `Goals` handles `Slide`, `LayOnGround` and `Traverse` (SpringBoard). Every `runline::State` alternative now has behaviour.

- [ ] **Step 1: Write the failing tests** (append inside the `#if`)

```cpp
// Sliding: the left hand leads forward and low, easing in over SlideEaseIn.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineArmsSlide, "Runline.Arms.SlideBraces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineArmsSlide::RunTest(const FString&)
{
	FRunlineArmsSolver S;
	const FRunlineArmsPose Start = Settle(S, At(runline::Slide{0.0f}, {0.0f, 0.0f, 0.0f}));
	TestTrue(TEXT("not yet braced at t = 0"), Start.Hands[Left].Weight < 0.05f);
	const FRunlineArmsPose P = Settle(S, At(runline::Slide{0.5f}, {0.0f, 0.0f, 0.0f}));
	TestTrue(TEXT("left hand leads"), Local(P, Left).Equals(Mirror(S.Tuning.SlideLead, Left), 0.01));
	TestTrue(TEXT("right hand trails"), Local(P, Right).Equals(S.Tuning.SlideTrail, 0.01));
	return true;
}

// Getting up off the ground, the hands push on the ground ahead of her feet.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineArmsGetUp, "Runline.Arms.GetUpPushesOnTheGround",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineArmsGetUp::RunTest(const FString&)
{
	FRunlineArmsSolver S;
	runline::LayOnGround Lay;
	const FRunlineArmsPose Lying = Settle(S, At(Lay, {0.0f, 0.0f, 0.0f}, -30.0));
	TestFalse(TEXT("lying: no contact"), Lying.Hands[Right].Contact.IsSet());
	Lay.gettingUp = 0.1f;
	const FRunlineArmsPose P = Settle(S, At(Lay, {0.0f, 0.0f, 0.0f}, -30.0));
	for (const int32 H : {Left, Right})
		TestTrue(TEXT("on the ground"), P.Hands[H].Contact && FMath::Abs(P.Hands[H].Contact->Z - 1.0) < 0.1);
	return true;
}

// Every state blends into every other with no frame-to-frame jump over 12 cm (camera space): the
// largest pose change (about 80 cm) over BlendTime 0.2 s peaks at 1.5 * 80 / 12 = 10 cm a frame.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineArmsEveryTransition, "Runline.Arms.EveryTransitionIsSmooth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineArmsEveryTransition::RunTest(const FString&)
{
	const runline::Tuning Tu;
	const runline::Vec3 Feet = HangFeet(Tu);
	runline::Vault V;
	V.kind = 3;
	V.from = Feet;
	V.hand = {0.0f, Feet.y + 0.2f, Feet.z - 0.6f};
	V.over = {0.0f, Feet.y + 0.4f, Feet.z - 0.6f};
	V.end = {0.0f, Feet.y, Feet.z - 1.5f};
	V.t = 0.1f;
	runline::LayOnGround Up;
	Up.gettingUp = 0.1f;
	const runline::State States[] = {
		runline::Ground{}, runline::Air{}, runline::Slide{0.3f}, runline::Roll{0.1f}, runline::Stunned{0.1f},
		runline::SoftLand{0.1f}, runline::LayOnGround{}, Up, runline::WallRun{{-1.0f, 0.0f, 0.0f}, 0.3f},
		runline::WallClimb{{0.0f, 0.0f, 1.0f}, 0.2f}, runline::WallClimbTurned{{0.0f, 0.0f, 1.0f}, 0.05f},
		runline::LedgeHang{{0.0f, 0.0f, 1.0f}, LedgeTop, false},
		runline::GrabTransfer{0.2f, Feet, {Feet.x, Feet.y + 1.0f, Feet.z}, 0.6f, {0.0f, 0.0f, 1.0f}, LedgeTop + 1.0f},
		runline::Traverse{runline::TraverseKind::PullUp, Feet, {0.0f, LedgeTop + 0.01f, -0.4f}, 0.2f, 1.0f, 0.15f, {}},
		runline::Traverse{runline::TraverseKind::SpringBoard, Feet, {0.0f, Feet.y + 0.5f, -1.0f}, 0.1f, 0.3f, 0.05f, {}},
		V,
	};
	TestTrue(TEXT("a sample of every core state"), int32(UE_ARRAY_COUNT(States)) >= int32(std::variant_size_v<runline::State>));
	double Worst = 0.0;
	for (const runline::State& A : States)
		for (const runline::State& B : States)
		{
			FRunlineArmsSolver S;
			FRunlineArmsPose Prev = Settle(S, At(A, Feet), 30);
			for (int32 I = 0; I < 30; ++I)
			{
				const FRunlineArmsPose P = S.Solve(At(B, Feet));
				TestTrue(TEXT("finite"), Finite(P));
				for (const int32 H : {Left, Right}) Worst = FMath::Max(Worst, FVector::Dist(Local(P, H), Local(Prev, H)));
				Prev = P;
			}
		}
	AddInfo(FString::Printf(TEXT("largest frame step: %.2f cm"), Worst));
	TestTrue(TEXT("no jumps"), Worst <= 12.0);
	return true;
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test-unreal.ps1 -Filter Runline.Arms`
Expected: SlideBraces fails ("left hand leads": the slide still holds the ready pose), and GetUpPushesOnTheGround fails ("on the ground"). EveryTransitionIsSmooth may already pass; it is the guard for Step 3.

- [ ] **Step 3: Implement** (in `Goals`, in the same `if` chain)

```cpp
	else if (const auto* Sl = std::get_if<runline::Slide>(&S))
	{
		// The left hand braces forward and low, the right trails, easing in.
		const float E = Smooth((Sl->t + Late) / T.SlideEaseIn);
		Pose(L, ForHand(T.SlideLead, L), EFingers::Open, E);
		Pose(R, ForHand(T.SlideTrail, R), EFingers::Relaxed, E);
	}
	else if (const auto* Lay = std::get_if<runline::LayOnGround>(&S))
	{
		if (Lay->gettingUp)
		{
			// Pushing up: both hands on the ground ahead of her feet.
			const FVector Right = FVector::CrossProduct(Up, Facing);
			const FVector Ground = W(In.Feet);
			for (int32 H = 0; H < 2; ++H)
			{
				FVector P = Ground + Facing * T.GetUpPush + Right * (Side(H) * T.HandSpacing * 0.5f);
				P.Z = Ground.Z + 1.0;
				Out[H] = ContactGoal(In, H, P, Facing, -Up, EFingers::Open, 1.0f);
			}
		}
		else
			for (int32 H = 0; H < 2; ++H) Pose(H, ForHand(T.LieDown, H), EFingers::Relaxed, 1.0f);
	}
	else if (const auto* Sb = std::get_if<runline::Traverse>(&S))
	{
		// The springboard (the only Traverse left): a foot plant, the arms run on; the
		// SpringBoard event swings them forward (StepMotion).
		(void)Sb;
		for (int32 H = 0; H < 2; ++H) Pose(H, RunPos(H, Amp, In), EFingers::Relaxed, 1.0f);
	}
```

The Roll branch from Task 3 stays. Move the "anything else holds the ready pose" comment to the end of the chain.

- [ ] **Step 4: Run the tests**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test-unreal.ps1 -Filter Runline.Arms`
Expected: all 21 Runline.Arms tests pass; the log shows the largest frame step.

- [ ] **Step 5: Commit**

```bash
git add Unreal/Runline/Source/Runline/Runner/Arms/RunlineArmsSolver.cpp Unreal/Runline/Source/Runline/Tests/ArmsSolverTests.cpp
git commit -m "M3a: arms for slide, getting up and the springboard; every transition smooth"
```

---

### Task 6: The anim instance: the pose on the real mesh

**Files:**
- Create: `Unreal/Runline/Source/Runline/Runner/Arms/RunlineArmsAnimInstance.h`, `.cpp`
- Test: `Unreal/Runline/Source/Runline/Tests/ArmsMeshTests.cpp` (create)

**Interfaces:**
- Consumes: Task 2's `FRunlineArmsPose`, `FRunlineHand`, `RunlineArms::EFingers`.
- Produces: `URunlineArmsAnimInstance` (native, no graph) with `void SetPose(const FRunlineArmsPose&)`. Bone names (SKM_Manny_Simple): `upperarm_l/r`, `lowerarm_l/r`, `hand_l/r`, `index|middle|ring|pinky_01..03_l/r`.

- [ ] **Step 1: Write the failing tests** (`Tests/ArmsMeshTests.cpp`)

```cpp
#include "Misc/AutomationTest.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "RunlineArmsAnimInstance.h"
#include "RunlineTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/// Manny with the arms anim instance, standing at the origin facing +Y (its own forward).
	struct FArmsMesh
	{
		FRunlineTestWorld TW;
		USkeletalMeshComponent* Mesh = nullptr;
		URunlineArmsAnimInstance* Anim = nullptr;
		FArmsMesh()
		{
			AActor* A = TW.World->SpawnActor<AActor>();
			Mesh = NewObject<USkeletalMeshComponent>(A);
			A->SetRootComponent(Mesh);
			Mesh->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple")));
			Mesh->SetAnimInstanceClass(URunlineArmsAnimInstance::StaticClass());
			Mesh->RegisterComponent();
			Anim = Cast<URunlineArmsAnimInstance>(Mesh->GetAnimInstance());
		}
		/// Evaluate once on the game thread.
		void Evaluate()
		{
			Mesh->TickAnimation(0.0f, false);
			Mesh->RefreshBoneTransforms();
			Mesh->FinalizeBoneTransform();
		}
		FVector Bone(const TCHAR* Name) const { return Mesh->GetBoneLocation(Name); }
		/// A pose with both hands at given points, palms down, fingers forward (+Y).
		FRunlineArmsPose Pose(const FVector& LeftAt, const FVector& RightAt, RunlineArms::EFingers F) const
		{
			FRunlineArmsPose P;
			P.Camera = FTransform(FRotator(0.0, 90.0, 0.0), Bone(TEXT("head")));
			const FVector Ats[2] = {LeftAt, RightAt};
			for (int32 H = 0; H < 2; ++H)
			{
				P.Hands[H].Position = Ats[H];
				P.Hands[H].FingerDir = FVector(0.0, 1.0, 0.0);
				P.Hands[H].PalmDir = FVector(0.0, 0.0, -1.0);
				P.Hands[H].Fingers = F;
				P.Hands[H].Weight = 1.0f;
			}
			return P;
		}
	};
}

// The hands reach the solver's targets, within arm's reach.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineArmsMeshReach, "Runline.Arms.MeshReachesTargets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineArmsMeshReach::RunTest(const FString&)
{
	FArmsMesh M;
	TestNotNull(TEXT("anim instance"), M.Anim);
	if (!M.Anim) return false;
	M.Evaluate();
	const FVector LS = M.Bone(TEXT("upperarm_l")), RS = M.Bone(TEXT("upperarm_r"));
	const double Upper = FVector::Dist(RS, M.Bone(TEXT("lowerarm_r"))), Lower = FVector::Dist(M.Bone(TEXT("lowerarm_r")), M.Bone(TEXT("hand_r")));
	AddInfo(FString::Printf(TEXT("Manny arm: upper %.1f + lower %.1f = %.1f cm"), Upper, Lower, Upper + Lower));
	const FVector LT = LS + FVector(5.0, 25.0, -25.0), RT = RS + FVector(-5.0, 25.0, -25.0);
	M.Anim->SetPose(M.Pose(LT, RT, RunlineArms::EFingers::Relaxed));
	M.Evaluate();
	TestTrue(TEXT("left hand on target"), M.Bone(TEXT("hand_l")).Equals(LT, 1.0));
	TestTrue(TEXT("right hand on target"), M.Bone(TEXT("hand_r")).Equals(RT, 1.0));
	return true;
}

// Out of reach the arm straightens towards the target and never stretches.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineArmsMeshOverreach, "Runline.Arms.MeshNeverOverextends",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineArmsMeshOverreach::RunTest(const FString&)
{
	FArmsMesh M;
	M.Evaluate();
	const double Arm = FVector::Dist(M.Bone(TEXT("upperarm_r")), M.Bone(TEXT("lowerarm_r"))) + FVector::Dist(M.Bone(TEXT("lowerarm_r")), M.Bone(TEXT("hand_r")));
	M.Anim->SetPose(M.Pose(M.Bone(TEXT("upperarm_l")) + FVector(0.0, 300.0, 0.0), M.Bone(TEXT("upperarm_r")) + FVector(0.0, 300.0, 0.0), RunlineArms::EFingers::Relaxed));
	M.Evaluate();
	TestTrue(TEXT("no stretch"), FVector::Dist(M.Bone(TEXT("upperarm_r")), M.Bone(TEXT("hand_r"))) <= Arm + 0.5);
	return true;
}

// The hand turns to the asked palm direction, and a fist curls the fingers towards the palm.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineArmsMeshHand, "Runline.Arms.MeshHandShapes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineArmsMeshHand::RunTest(const FString&)
{
	FArmsMesh M;
	M.Evaluate();
	const FVector RT = M.Bone(TEXT("upperarm_r")) + FVector(-5.0, 25.0, -25.0);
	const FVector LT = M.Bone(TEXT("upperarm_l")) + FVector(5.0, 25.0, -25.0);
	const auto TipToPalm = [&]()
	{
		const FVector Hand = M.Bone(TEXT("hand_r"));
		const FVector Palm = Hand + FVector(0.0, 0.0, -1.0) * 5.0 + (M.Bone(TEXT("middle_01_r")) - Hand) * 0.5;
		return FVector::Dist(M.Bone(TEXT("index_03_r")), Palm);
	};
	M.Anim->SetPose(M.Pose(LT, RT, RunlineArms::EFingers::Open));
	M.Evaluate();
	const double Open = TipToPalm();
	const FVector Finger = (M.Bone(TEXT("middle_01_r")) - M.Bone(TEXT("hand_r"))).GetSafeNormal();
	TestTrue(TEXT("fingers point where asked"), FVector::DotProduct(Finger, FVector(0.0, 1.0, 0.0)) > 0.9);
	const FVector Across = (M.Bone(TEXT("index_01_r")) - M.Bone(TEXT("pinky_01_r"))).GetSafeNormal();
	TestTrue(TEXT("palm faces where asked"), FVector::DotProduct(FVector::CrossProduct(Finger, Across).GetSafeNormal(), FVector(0.0, 0.0, -1.0)) > 0.9);
	M.Anim->SetPose(M.Pose(LT, RT, RunlineArms::EFingers::Fist));
	M.Evaluate();
	TestTrue(TEXT("a fist curls towards the palm"), TipToPalm() < Open - 2.0);
	return true;
}

// No pose yet (the first frame): the reference pose, no crash.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineArmsMeshNoPose, "Runline.Arms.MeshWithoutAPose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineArmsMeshNoPose::RunTest(const FString&)
{
	FArmsMesh M;
	M.Evaluate();
	TestFalse(TEXT("bones are numbers"), M.Bone(TEXT("hand_r")).ContainsNaN());
	return true;
}

#endif
```

- [ ] **Step 2: Run them to verify they fail**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test-unreal.ps1 -Filter Runline.Arms.Mesh`
Expected: the build fails: `RunlineArmsAnimInstance.h` not found.

- [ ] **Step 3: Implement**

`RunlineArmsAnimInstance.h`:

```cpp
#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "RunlineArmsTypes.h"
#include "RunlineArmsAnimInstance.generated.h"

/// Builds the arms' pose on the worker thread, in C++ (no anim graph): the reference pose, the
/// finger shapes, then two-bone IK per arm onto the solver's hands, each hand turned to its finger
/// and palm directions. The hand and finger frames are measured from the reference pose, not
/// assumed, so another skeleton with the same bone names works too.
USTRUCT()
struct FRunlineArmsProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

	FRunlineArmsProxy() = default;
	explicit FRunlineArmsProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	TOptional<FRunlineArmsPose> Pose;
};

/// The first-person arms (M3a). The runner gives it each frame's FRunlineArmsPose.
UCLASS(Transient, NotBlueprintable)
class RUNLINE_API URunlineArmsAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/// This frame's hands (game thread); the proxy takes them before it evaluates.
	void SetPose(const FRunlineArmsPose& InPose) { Pending = InPose; }
	const TOptional<FRunlineArmsPose>& GetPendingPose() const { return Pending; }

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FRunlineArmsProxy(this); }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override { delete static_cast<FRunlineArmsProxy*>(InProxy); }

private:
	TOptional<FRunlineArmsPose> Pending;
};
```

`RunlineArmsAnimInstance.cpp`:

```cpp
#include "RunlineArmsAnimInstance.h"
#include "Animation/AnimNodeBase.h"
#include "BonePose.h"
#include "TwoBoneIK.h"

namespace
{
	using RunlineArms::EFingers;
	const TCHAR* const Chains[2][3] = {{TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l")}, {TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r")}};
	const TCHAR* const Fingers[4] = {TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky")};
	const TCHAR* const Sides[2] = {TEXT("l"), TEXT("r")};

	float CurlDegrees(EFingers F)
	{
		switch (F)
		{
		case EFingers::Fist: return 85.0f;
		case EFingers::Grip: return 60.0f;
		case EFingers::Open: return 0.0f;
		default: return 15.0f;
		}
	}

	FCompactPoseBoneIndex Find(const FBoneContainer& Bones, const FString& Name)
	{
		const int32 I = Bones.GetPoseBoneIndexForBoneName(FName(*Name));
		return I == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(I));
	}

	/// One arm's bones and its hand's frame in the reference pose (component space).
	struct FArm
	{
		FCompactPoseBoneIndex Upper{INDEX_NONE}, Lower{INDEX_NONE}, Hand{INDEX_NONE};
		TArray<FCompactPoseBoneIndex, TInlineAllocator<12>> FingerBones;
		FQuat HandRot = FQuat::Identity;
		FVector Finger = FVector::ForwardVector, Palm = -FVector::UpVector, Across = FVector::RightVector;
		bool bValid = false;
	};
}

void FRunlineArmsProxy::PreUpdate(UAnimInstance* Instance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(Instance, DeltaSeconds);
	Pose = CastChecked<URunlineArmsAnimInstance>(Instance)->GetPendingPose();
}

bool FRunlineArmsProxy::Evaluate(FPoseContext& Output)
{
	Output.ResetToRefPose();
	if (!Pose) return true;
	FCompactPose& Local = Output.Pose;
	const FBoneContainer& Bones = Local.GetBoneContainer();

	// The reference pose's arms, measured.
	FCSPose<FCompactPose> Ref;
	Ref.InitPose(Local);
	FArm Arms[2];
	for (int32 H = 0; H < 2; ++H)
	{
		FArm& A = Arms[H];
		A.Upper = Find(Bones, Chains[H][0]);
		A.Lower = Find(Bones, Chains[H][1]);
		A.Hand = Find(Bones, Chains[H][2]);
		const FCompactPoseBoneIndex Middle = Find(Bones, FString::Printf(TEXT("middle_01_%s"), Sides[H]));
		const FCompactPoseBoneIndex Index = Find(Bones, FString::Printf(TEXT("index_01_%s"), Sides[H]));
		const FCompactPoseBoneIndex Pinky = Find(Bones, FString::Printf(TEXT("pinky_01_%s"), Sides[H]));
		if (A.Upper == INDEX_NONE || A.Lower == INDEX_NONE || A.Hand == INDEX_NONE || Middle == INDEX_NONE || Index == INDEX_NONE || Pinky == INDEX_NONE) continue;
		const FTransform& HandT = Ref.GetComponentSpaceTransform(A.Hand);
		A.HandRot = HandT.GetRotation();
		A.Finger = (Ref.GetComponentSpaceTransform(Middle).GetLocation() - HandT.GetLocation()).GetSafeNormal();
		A.Across = (Ref.GetComponentSpaceTransform(Index).GetLocation() - Ref.GetComponentSpaceTransform(Pinky).GetLocation()).GetSafeNormal();
		// Palm: fingers forward, thumb side across; the left hand is the mirror image.
		A.Palm = (FVector::CrossProduct(A.Finger, A.Across) * (H == RunlineArms::Right ? 1.0 : -1.0)).GetSafeNormal();
		for (const TCHAR* F : Fingers)
			for (int32 J = 1; J <= 3; ++J)
			{
				const FCompactPoseBoneIndex B = Find(Bones, FString::Printf(TEXT("%s_0%d_%s"), F, J, Sides[H]));
				if (B != INDEX_NONE) A.FingerBones.Add(B);
			}
		A.bValid = true;
	}

	// 1. Finger shapes (local space): each finger bone turns about the hand's across axis, the way
	//    that brings the fingers towards the palm.
	for (int32 H = 0; H < 2; ++H)
	{
		const FArm& A = Arms[H];
		if (!A.bValid) continue;
		const float Sign = FVector::DotProduct(FQuat(A.Across, 0.3).RotateVector(A.Finger), A.Palm) > 0.0 ? 1.0f : -1.0f;
		const float Angle = FMath::DegreesToRadians(CurlDegrees(Pose->Hands[H].Fingers)) * Sign;
		for (const FCompactPoseBoneIndex B : A.FingerBones)
		{
			const FVector Axis = Ref.GetComponentSpaceTransform(B).InverseTransformVectorNoScale(A.Across).GetSafeNormal();
			Local[B].SetRotation(Local[B].GetRotation() * FQuat(Axis, Angle));
		}
	}

	// 2. Two-bone IK per arm (component space), then the hand's orientation.
	FCSPose<FCompactPose> CS;
	CS.InitPose(Local);
	const FTransform& ToWorld = GetComponentTransform();
	for (int32 H = 0; H < 2; ++H)
	{
		const FArm& A = Arms[H];
		if (!A.bValid) continue;
		const FRunlineHand& Hand = Pose->Hands[H];
		const double Side = H == RunlineArms::Right ? 1.0 : -1.0;
		const FVector Effector = ToWorld.InverseTransformPosition(Hand.Position);
		// Elbows out and down from the camera, as a runner holds them.
		const FVector Pole = ToWorld.InverseTransformPosition(Pose->Camera.TransformPositionNoScale(FVector(-10.0, Side * 45.0, -45.0)));
		FTransform Upper = CS.GetComponentSpaceTransform(A.Upper);
		FTransform Lower = CS.GetComponentSpaceTransform(A.Lower);
		FTransform End = CS.GetComponentSpaceTransform(A.Hand);
		AnimationCore::SolveTwoBoneIK(Upper, Lower, End, Pole, Effector, false, 1.0, 1.0);
		const FVector F = ToWorld.InverseTransformVectorNoScale(Hand.FingerDir).GetSafeNormal(UE_SMALL_NUMBER, A.Finger);
		const FVector P = ToWorld.InverseTransformVectorNoScale(Hand.PalmDir).GetSafeNormal(UE_SMALL_NUMBER, A.Palm);
		const FQuat Want = FRotationMatrix::MakeFromXZ(F, P).ToQuat();
		const FQuat Had = FRotationMatrix::MakeFromXZ(A.Finger, A.Palm).ToQuat();
		End.SetRotation(Want * Had.Inverse() * A.HandRot);
		const FBoneTransform Set[3] = {FBoneTransform(A.Upper, Upper), FBoneTransform(A.Lower, Lower), FBoneTransform(A.Hand, End)};
		CS.SafeSetCSBoneTransforms(Set);
	}
	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(CS), Local);
	return true;
}
```

If `Mesh->FinalizeBoneTransform()` is not callable from the test (protected), drop it from `Evaluate()`: `RefreshBoneTransforms()` without a tick function evaluates and finalizes on the game thread. Ledger a ruling if you have to change the helper.

- [ ] **Step 4: Run the tests**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test-unreal.ps1 -Filter Runline.Arms`
Expected: all 25 Runline.Arms tests pass. The log's "Manny arm" line gives the real arm length. If it is shorter than `FRunlineArmsTuning::Reach` (55), set `Reach` to the measured length and `ReachFade` to 1.25× it, rerun all Runline.Arms tests, and ledger a ruling.

- [ ] **Step 5: Commit**

```bash
git add Unreal/Runline/Source/Runline/Runner/Arms/RunlineArmsAnimInstance.h Unreal/Runline/Source/Runline/Runner/Arms/RunlineArmsAnimInstance.cpp Unreal/Runline/Source/Runline/Tests/ArmsMeshTests.cpp
git commit -m "M3a: native arms anim instance: finger shapes, two-bone IK, hand orientation"
```

---

### Task 7: The runner's arms, replayed traces, playtest

**Files:**
- Modify: `Unreal/Runline/Source/Runline/Runner/RunlineRunner.h`, `.cpp`
- Modify: `Unreal/Runline/Source/Runline/Tests/RunnerTests.cpp` (`Runline.Runner.ArmsAndBody`)
- Test: `Unreal/Runline/Source/Runline/Tests/ArmsSolverTests.cpp` (trace replay)
- Modify: `CLAUDE.md`

**Interfaces:**
- Consumes: everything above.
- Produces: `ARunlineRunner::ArmsTuning` (`UPROPERTY(EditAnywhere, Category = "Runline|Arms") FRunlineArmsTuning`), `void UpdateArms(float DeltaSeconds)`, `void PlaceArms()`, `USkeletalMeshComponent* GetArmsMesh() const`, `UCameraComponent* GetCamera() const`.

- [ ] **Step 1: Write the failing tests**

Replace the body of `Runline.Runner.ArmsAndBody` in RunnerTests.cpp, keeping the test's name, and add `#include "RunlineArmsAnimInstance.h"` and `#include "Camera/CameraComponent.h"`:

```cpp
// M3a: the body still runs ABP_Unarmed (hidden from her, it casts her shadow); the arms ride the
// camera, run the native arms anim instance, and show only her arms.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineRunnerArms, "Runline.Runner.ArmsAndBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineRunnerArms::RunTest(const FString&)
{
	FRunlineTestWorld TW;
	TW.AddBox(runline::Aabb::New({-50.0f, -1.0f, -50.0f}, {50.0f, 0.0f, 50.0f}), 0);
	ARunlineRunner* R = TW.World->SpawnActor<ARunlineRunner>();
	TestNotNull(TEXT("runner"), R);
	if (!R) return false;
	USkeletalMeshComponent* Arms = R->GetArmsMesh();
	TestTrue(TEXT("body runs ABP_Unarmed"), R->GetMesh()->GetAnimClass() && R->GetMesh()->GetAnimClass()->GetName().Contains(TEXT("ABP_Unarmed")));
	TestTrue(TEXT("she doesn't see the body"), R->GetMesh()->bOwnerNoSee);
	TestTrue(TEXT("only she sees the arms"), Arms->bOnlyOwnerSee);
	TestTrue(TEXT("the arms ride the camera"), Arms->GetAttachParent() == R->GetCamera());
	URunlineArmsAnimInstance* Anim = Cast<URunlineArmsAnimInstance>(Arms->GetAnimInstance());
	TestNotNull(TEXT("the arms run the native anim instance"), Anim);
	TestTrue(TEXT("no head or legs"), Arms->IsBoneHiddenByName(TEXT("neck_01")) && Arms->IsBoneHiddenByName(TEXT("thigh_l")) && Arms->IsBoneHiddenByName(TEXT("thigh_r")));
	R->Runline().SpawnAt({0.0f, 0.0f, 0.0f}, 0.0f);
	R->Runline().Advance(1.0f / 60.0f);
	R->Tick(1.0f / 60.0f);
	TestTrue(TEXT("a pose each tick"), Anim && Anim->GetPendingPose().IsSet());
	if (Anim && Anim->GetPendingPose())
	{
		const FVector Hand = Anim->GetPendingPose()->Hands[RunlineArms::Right].Position;
		TestTrue(TEXT("the hand is in front of the camera"), R->GetCamera()->GetComponentTransform().InverseTransformPositionNoScale(Hand).X > 10.0);
	}
	const FVector Head = Arms->GetBoneLocation(TEXT("head"));
	TestTrue(TEXT("the arms' head bone sits at the camera"), FVector::Dist(Head, R->GetCamera()->GetComponentLocation()) < 20.0);
	return true;
}
```

Append to ArmsSolverTests.cpp, and add `#include "Replay.h"`, `#include <fstream>`, `#include "HAL/FileManager.h"` at the top. Include them as UnrealWorldTests.cpp does; copy its THIRD_PARTY_INCLUDES guards if it has them.

```cpp
// Every box trace, replayed with the arms solving each step: finite, no frame jumps (except at a
// cut), and the core still bit-exact: the arms never feed back.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineArmsTraces, "Runline.Arms.TracesStaySmooth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineArmsTraces::RunTest(const FString&)
{
	const FString Dir = FString(RUNLINE_GOLDEN_DIR) / TEXT("traces");
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.trace")), true, false);
	Files.Sort();
	int32 Replayed = 0, Failed = 0;
	double Worst = 0.0;
	FString WorstWhere;
	for (const FString& File : Files)
	{
		std::ifstream InFile(TCHAR_TO_UTF8(*(Dir / File)));
		const trace::Trace T = trace::Parse(InFile, TCHAR_TO_UTF8(*File));
		bool bMesh = false;
		for (const auto& Wd : T.worlds) bMesh |= !Wd.tris.empty();
		if (bMesh) continue;
		std::vector<runline::BoxWorld> Worlds;
		for (const auto& Wd : T.worlds)
		{
			runline::BoxWorld B;
			for (const runline::Aabb& Box : Wd.boxes) B.Add(Box);
			Worlds.push_back(B);
		}
		FRunlineArmsSolver S;
		int32 Snap = 0, Frame = 0;
		TOptional<FVector> Prev[2];
		bool bFinite = true;
		const std::string R = replay::Replay(T, [&](std::size_t I) -> const runline::World& { return Worlds[I]; },
			[&](const runline::Controller& C, const trace::Frame& F)
			{
				FRunlineArmsInput In;
				In.State = C.state;
				In.Shimmy = C.shimmy;
				In.Feet = C.feet;
				In.Vel = C.vel;
				In.CoreTuning = C.tuning;
				const runline::View V = C.GetView();
				In.Camera = FTransform(RunlineUnits::ToUnrealRotator(V.yaw, V.pitch, V.roll), RunlineUnits::ToUnrealPosition(V.eye));
				In.Fov = V.fovDeg;
				In.Dt = F.in.dt;
				// A cut (the trace teleported her, or she died): the view jumps, so do the arms.
				bool bCut = F.set.has_value();
				for (const runline::Event& E : C.events)
				{
					In.Events.Add(E);
					bCut |= E.kind == runline::EventKind::Death;
				}
				if (bCut) ++Snap;
				In.SnapCount = Snap;
				const FRunlineArmsPose P = S.Solve(In);
				for (int32 H = 0; H < 2; ++H)
				{
					const FVector L = P.Camera.InverseTransformPositionNoScale(P.Hands[H].Position);
					bFinite &= !L.ContainsNaN();
					if (Prev[H] && !bCut)
					{
						const double Step = FVector::Dist(L, *Prev[H]);
						if (Step > Worst) { Worst = Step; WorstWhere = FString::Printf(TEXT("%s frame %d (%hs)"), *File, Frame, runline::StateName(C.state)); }
					}
					Prev[H] = L;
				}
				++Frame;
			});
		if (!R.empty() && ++Failed <= 10) AddError(FString::Printf(TEXT("%s: %hs"), *File, R.c_str()));
		TestTrue(*FString::Printf(TEXT("%s: finite"), *File), bFinite);
		++Replayed;
	}
	AddInfo(FString::Printf(TEXT("%d traces; largest frame step %.2f cm at %s"), Replayed, Worst, *WorstWhere));
	TestTrue(TEXT("traces found"), Replayed > 100);
	TestEqual(TEXT("still bit-exact"), Failed, 0);
	TestTrue(TEXT("no jumps"), Worst <= 12.0);
	return true;
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test-unreal.ps1 -Filter Runline.Runner.ArmsAndBody`
Expected: the build fails: `GetArmsMesh` is not a member of `ARunlineRunner`.

- [ ] **Step 3: Implement**

`RunlineRunner.h`: add `#include "RunlineArmsSolver.h"`. Public:

```cpp
	/// The first-person arms' numbers (M3a); the solver takes them each tick.
	UPROPERTY(EditAnywhere, Category = "Runline|Arms")
	FRunlineArmsTuning ArmsTuning;
	USkeletalMeshComponent* GetArmsMesh() const { return FirstPersonMesh; }
	UCameraComponent* GetCamera() const { return Camera; }
	/// Solves this frame's arms and hands them to the arms mesh (Tick).
	void UpdateArms(float DeltaSeconds);
```

Private: `void PlaceArms();` and `FRunlineArmsSolver Arms;`. Update `FirstPersonMesh`'s doc comment to: "The first-person arms (M3a): Manny riding the camera, posed by URunlineArmsAnimInstance from the arms solver; head and legs hidden."

`RunlineRunner.cpp` constructor: drop the `ArmsAnim` finder and replace the FirstPersonMesh block with:

```cpp
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonMesh"));
	FirstPersonMesh->SetupAttachment(Camera);
	FirstPersonMesh->SetSkeletalMesh(Manny.Object);
	FirstPersonMesh->SetAnimInstanceClass(URunlineArmsAnimInstance::StaticClass());
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->SetCastShadow(false);   // the body casts her shadow
	FirstPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));
```

Add `#include "RunlineArmsAnimInstance.h"`. Call `PlaceArms()` at the end of the constructor, after the block above, so the editor's preview and tests see it too:

```cpp
void ARunlineRunner::PlaceArms()
{
	// Manny faces +Y; turned to the camera's forward, with its head bone at the camera (less
	// MeshOffset), the shoulders sit where hers would. The head and legs are hidden.
	const USkinnedAsset* Asset = FirstPersonMesh->GetSkinnedAsset();
	if (!Asset) return;
	const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();
	FTransform Head = FTransform::Identity;
	for (int32 I = Ref.FindBoneIndex(TEXT("head")); I != INDEX_NONE; I = Ref.GetParentIndex(I)) Head = Head * Ref.GetRefBonePose()[I];
	const FQuat Turn = FRotator(0.0, -90.0, 0.0).Quaternion();
	FirstPersonMesh->SetRelativeLocationAndRotation(ArmsTuning.MeshOffset - Turn.RotateVector(Head.GetLocation()), Turn);
	for (const TCHAR* Bone : {TEXT("neck_01"), TEXT("thigh_l"), TEXT("thigh_r")}) FirstPersonMesh->HideBoneByName(Bone, PBO_None);
}
```

`HideBoneByName` needs a registered component with bone data. If it does nothing in the constructor (the test shows that), call `PlaceArms()` from `BeginPlay()` instead (before `FindCourse()`) and ledger it. In `BeginPlay()` also add `FirstPersonMesh->PrimaryComponentTick.AddPrerequisite(this, PrimaryActorTick);`, so the arms evaluate after the runner has set this frame's pose.

`Tick`: after `UpdateBodyPlacement();` add `UpdateArms(DeltaSeconds);`, and:

```cpp
void ARunlineRunner::UpdateArms(float DeltaSeconds)
{
	URunlineArmsAnimInstance* Anim = Cast<URunlineArmsAnimInstance>(FirstPersonMesh->GetAnimInstance());
	if (!Anim) return;
	Arms.Tuning = ArmsTuning;
	const URunlineMovementComponent& M = Runline();
	// Game seconds, like the core (the actor doesn't tick while paused, so the arms freeze).
	Anim->SetPose(Arms.Solve(FRunlineArmsInput::From(M, Camera->GetComponentTransform(), Camera->FieldOfView, Camera->FirstPersonFieldOfView, DeltaSeconds * M.GameSpeed)));
}
```

`CLAUDE.md`, under Conventions, add: `- First-person arms (M3a): FRunlineArmsSolver (Runner/Arms) turns the core's state and contacts into hand targets; URunlineArmsAnimInstance poses Manny in C++ (no anim graph). Tuning: ARunlineRunner::ArmsTuning.`

- [ ] **Step 4: Run all the tests**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test-unreal.ps1`
Expected: every Runline.* test passes: 79 (52 before; Runline.Movement.ArmsFeed and 26 Runline.Arms tests new; Runline.Runner.ArmsAndBody rewritten). The TracesStaySmooth log names the largest frame step. A jump over 12 cm at a real moment (not a cut) is a bug: find the state change it comes from with superpowers:systematic-debugging, not by raising the limit.
Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: 80/80.

- [ ] **Step 5: Look at it**

Run: `& 'D:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' "$PWD\Unreal\Runline\Runline.uproject" -game -windowed -ResX=1600 -ResY=900 '?shot=8?autorun=1'`
Expected: a screenshot under `Unreal/Runline/Saved/Screenshots/`. Read it: both arms visible in the lower part of the view, no T-pose, no head or torso in view. If the arms are missing or wrong, fix `MeshOffset`/`Ready` and rerun.

- [ ] **Step 6: Commit**

```bash
git add Unreal/Runline/Source/Runline/Runner/RunlineRunner.h Unreal/Runline/Source/Runline/Runner/RunlineRunner.cpp Unreal/Runline/Source/Runline/Tests/RunnerTests.cpp Unreal/Runline/Source/Runline/Tests/ArmsSolverTests.cpp CLAUDE.md
git commit -m "M3a: the runner's arms ride the camera and follow every move; traces replay smooth and bit-exact"
```

- [ ] **Step 7: Playtest checklist for the user** (not automated; record the answers in the final message)

1. Standing, jogging and sprinting: does the pump look right, and does it match the head bob?
2. Mouse flicks: do the arms lag a little and settle?
3. Jump, land, hard landing (stunned): do the arms dip with the impact?
4. Slide: the left hand braces forward.
5. Roll: fists tucked.
6. Wallrun left and right: the wall-side hand on the wall.
7. Wallclimb, and turning off it.
8. Ledge grab, hang, shimmy both ways, round a corner, grab transfer up, pull up.
9. Vault over and onto, the high vaults (both hands), step-ups (no hands).
10. Respawn and die mid-move: no sweep.
11. Look straight down and up: the arms stay out of the way.
12. Settings FOV 60 and 120: the hands still land on the ledge and wall.

---

## Self-review notes

- Spec coverage:
  - §3 solver, anim instance, tuning, attachment and events: Tasks 1, 2, 3, 6 and 7.
  - §3 Contacts: Task 4, including shimmy via `Controller::shimmy`.
  - §3 Timing (the interpolation alpha): Tasks 1 and 4 (`Late`).
  - §4 every state: Tasks 3, 4 and 5, with the transition matrix covering each variant.
  - §5 edge cases: pitch (Task 3), snaps (Tasks 1 and 3), game speed and pause (Task 7 `UpdateArms`), hitch events (Task 1).
  - §6 tests 1–4: solver tests in Tasks 3–5, mesh tests in Task 6, replay in Task 7, playtest in Task 7 Step 7.
- Known guesses, each with a step that checks it:
  - the `Reach` tuning (Task 6 measures the arm);
  - `MeshOffset` and `Ready` (Task 7's screenshot);
  - the timing of `HideBoneByName` (Task 7, Step 3 fallback);
  - `FinalizeBoneTransform` access (Task 6, Step 3 note).
