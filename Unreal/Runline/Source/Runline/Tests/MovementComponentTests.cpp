#include "Misc/AutomationTest.h"
#include "RunlineTestRunner.h"
#include "RunlineTestWorld.h"
#include "Components/CapsuleComponent.h"
#include "RunlineSettings.h"
THIRD_PARTY_INCLUDES_START
#include "runline/Camera.h"
#include "runline/Look.h"
THIRD_PARTY_INCLUDES_END
#include <bit>
#include <vector>

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr float Step = URunlineMovementComponent::StepSeconds;

	/// A world with a 100 m floor and one runner standing at the origin, facing -Z (core).
	struct FRunnerWorld
	{
		FRunlineTestWorld TW;
		ARunlineTestRunner* Runner = nullptr;
		/// Each step's input and the controller right after it.
		std::vector<runline::Input> Inputs;
		std::vector<runline::Controller> After;

		FRunnerWorld()
		{
			TW.AddBox(runline::Aabb::New({-50.0f, -1.0f, -50.0f}, {50.0f, 0.0f, 50.0f}), 0);
			Runner = TW.World->SpawnActor<ARunlineTestRunner>();
			Runner->Runline().SpawnAt({0.0f, 0.0f, 0.0f}, 0.0f);
			Runner->Runline().OnStep = [this](const runline::Controller& C, const runline::Input& In)
			{
				Inputs.push_back(In);
				After.push_back(C);
			};
		}
		URunlineMovementComponent& M() const { return Runner->Runline(); }
		int32 Count(runline::EventKind K) const
		{
			int32 N = 0;
			for (const runline::Controller& C : After)
				for (const runline::Event& E : C.events) N += E.kind == K ? 1 : 0;
			return N;
		}
	};

	/// Sets the player's settings for one test and puts them back after.
	struct FSettingsScope
	{
		float Fov, Motion;
		FSettingsScope(float InFov, float InMotion)
		{
			URunlineSettings& S = URunlineSettings::Get();
			Fov = S.FieldOfView; Motion = S.CameraMotion;
			S.FieldOfView = InFov; S.CameraMotion = InMotion;
		}
		~FSettingsScope()
		{
			URunlineSettings& S = URunlineSettings::Get();
			S.FieldOfView = Fov; S.CameraMotion = Motion;
		}
	};

	bool Same(float A, float B) { return std::bit_cast<uint32>(A) == std::bit_cast<uint32>(B); }
	bool Same(runline::Vec3 A, runline::Vec3 B) { return Same(A.x, B.x) && Same(A.y, B.y) && Same(A.z, B.z); }
	bool SameCore(const runline::Controller& A, const runline::Controller& B)
	{
		return Same(A.feet, B.feet) && Same(A.vel, B.vel) && Same(A.yaw, B.yaw) && Same(A.pitch, B.pitch) && A.state.index() == B.state.index();
	}
}

// Stepping through the component is stepping the core: same input, same world, same result,
// and the capsule sits on the stepped feet.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineMovementMatches, "Runline.Movement.MatchesCoreDirect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineMovementMatches::RunTest(const FString&)
{
	FRunnerWorld RW;
	runline::BoxWorld Ref;
	Ref.Add(runline::Aabb::New({-50.0f, -1.0f, -50.0f}, {50.0f, 0.0f, 50.0f}));
	runline::Controller Bare(runline::Tuning{}, {0.0f, 0.0f, 0.0f}, 0.0f);
	Bare.Respawn();
	runline::Input In;
	In.moveAxis = {0.0f, 1.0f};
	RW.M().SetMove(FVector2D(0.0, 1.0));
	for (int32 I = 0; I < 120; ++I)
	{
		RW.M().Advance(Step);
		Bare.Step(Step, In, Ref);
	}
	TestEqual(TEXT("120 steps"), RW.M().StepCount(), int64(120));
	TestTrue(TEXT("same core state as stepping it directly"), SameCore(RW.M().Core(), Bare));
	TestTrue(TEXT("she ran"), RW.M().Core().feet.z < -3.0f);
	const UCapsuleComponent* Cap = RW.Runner->GetCapsuleComponent();
	const FVector Want = RunlineUnits::ToUnrealPosition(Bare.feet) + FVector(0, 0, Cap->GetScaledCapsuleHalfHeight());
	TestTrue(TEXT("capsule on the stepped feet"), Cap->GetComponentLocation().Equals(Want, 1e-6));
	TestEqual(TEXT("capsule radius is the body's half width"), double(Cap->GetScaledCapsuleRadius()), double(Bare.tuning.halfWidth) * 100.0, 1e-4);
	return true;
}

// Review Focus 1: 30, 60 and 144 fps give the same steps, look included (applied once).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineMovementFrameRates, "Runline.Movement.FrameRates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineMovementFrameRates::RunTest(const FString&)
{
	const float Rates[] = {1.0f / 30.0f, 1.0f / 60.0f, 1.0f / 144.0f};
	std::vector<std::vector<runline::Controller>> Runs;
	for (const float Dt : Rates)
	{
		FRunnerWorld RW;
		RW.M().SetMove(FVector2D(0.0, 1.0));
		RW.M().SetJumpHeld(true);
		RW.M().AddLook(FVector2D(0.1, 0.0));
		for (int32 F = 0; F * Dt < 2.0f; ++F) RW.M().Advance(Dt);
		TestTrue(FString::Printf(TEXT("about 120 steps at %.0f fps"), 1.0f / Dt), FMath::Abs(RW.M().StepCount() - 120) <= 1);
		Runs.push_back(RW.After);
	}
	const size_t N = 118;
	for (size_t R = 1; R < Runs.size(); ++R)
	{
		TestTrue(TEXT("enough steps"), Runs[R].size() >= N && Runs[0].size() >= N);
		bool bSame = true;
		for (size_t I = 0; I < N && I < Runs[R].size() && I < Runs[0].size(); ++I) bSame &= SameCore(Runs[0][I], Runs[R][I]);
		TestTrue(FString::Printf(TEXT("%.0f fps steps match 30 fps"), 1.0f / Rates[R]), bSame);
	}
	// The look is applied once, on the first step, at every frame rate: she runs straight, so
	// her yaw stays exactly 0.1 to the end.
	for (size_t R = 0; R < Runs.size(); ++R)
		TestTrue(FString::Printf(TEXT("%.0f fps: the look turned her once, by 0.1"), 1.0f / Rates[R]), Same(Runs[R][0].yaw, 0.1f) && Same(Runs[R][N - 1].yaw, 0.1f));
	return true;
}

// A hitch runs at most MaxStepsPerFrame steps and drops the rest.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineMovementHitch, "Runline.Movement.Hitch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineMovementHitch::RunTest(const FString&)
{
	FRunnerWorld RW;
	RW.M().Advance(0.5f);
	TestEqual(TEXT("capped steps"), RW.M().StepCount(), int64(URunlineMovementComponent::MaxStepsPerFrame));
	TestTrue(TEXT("leftover time dropped"), RW.M().GetAccumulator() < Step);
	return true;
}

// Review Focus 2: a tap between two steps reaches the core once; a press doesn't repeat.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineMovementLatch, "Runline.Movement.InputLatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineMovementLatch::RunTest(const FString&)
{
	{
		FRunnerWorld RW;
		RW.M().Advance(10 * Step);   // land and settle
		const size_t Before = RW.Inputs.size();
		RW.M().PressJump();
		RW.M().SetJumpHeld(true);
		RW.M().Advance(1.0f / 144.0f);
		RW.M().SetJumpHeld(false);   // released before the next step
		for (int32 I = 0; I < 3; ++I) RW.M().Advance(1.0f / 144.0f);
		int32 Pressed = 0;
		for (size_t I = Before; I < RW.Inputs.size(); ++I) Pressed += RW.Inputs[I].jumpPressed ? 1 : 0;
		TestEqual(TEXT("the tap reached one step"), Pressed, 1);
		for (size_t I = Before; I < RW.Inputs.size(); ++I)
			if (RW.Inputs[I].jumpPressed) TestTrue(TEXT("held on the step that takes the tap"), RW.Inputs[I].jumpHeld);
		for (int32 I = 0; I < 30; ++I) RW.M().Advance(Step);
		TestEqual(TEXT("one jump"), RW.Count(runline::EventKind::Jump), 1);
	}
	{
		FRunnerWorld RW;
		RW.M().Advance(10 * Step);
		const size_t Before = RW.Inputs.size();
		RW.M().PressJump();
		RW.M().Advance(2.0f * Step + 1e-4f);   // a 30 fps frame: two steps
		TestEqual(TEXT("two steps this frame"), int32(RW.Inputs.size() - Before), 2);
		TestTrue(TEXT("first step pressed"), RW.Inputs[Before].jumpPressed);
		TestFalse(TEXT("second step not pressed again"), RW.Inputs[Before + 1].jumpPressed);
	}
	return true;
}

// Review Focus 4: a NotPorted path is logged, turned into a respawn, and never crashes.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineMovementGuard, "Runline.Movement.NotPortedGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineMovementGuard::RunTest(const FString&)
{
	AddExpectedError(TEXT("RunlineCore: not ported"), EAutomationExpectedErrorFlags::Contains, 1);
	FRunnerWorld RW;
	RW.M().MutableCore().tuning.autoStepUp = true;   // TryAutoStepUp isn't ported: it throws
	RW.M().SetMove(FVector2D(0.0, 1.0));
	for (int32 I = 0; I < 30; ++I) RW.M().Advance(Step);
	TestTrue(TEXT("the guard caught it"), RW.M().NotPortedCount() > 0);
	TestTrue(TEXT("back at the spawn"), RW.M().Core().feet == RW.M().Core().spawn);
	return true;
}

// Review Focus 4: the air 180 lands her on her back (ported), and jump gets her up.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineMovementOnBack, "Runline.Movement.LandOnYourBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineMovementOnBack::RunTest(const FString&)
{
	FRunnerWorld RW;
	RW.M().SetMove(FVector2D(0.0, 1.0));
	for (int32 I = 0; I < 120; ++I) RW.M().Advance(Step);
	RW.M().PressJump();
	bool bTurned = false;
	for (int32 I = 0; I < 60; ++I)
	{
		const runline::Controller& C = RW.M().Core();
		if (!bTurned && std::holds_alternative<runline::Air>(C.state) && C.vel.y < 1.0f)
		{
			RW.M().PressTurn();
			bTurned = true;
		}
		RW.M().Advance(Step);
	}
	TestTrue(TEXT("landed on her back"), RW.Count(runline::EventKind::LandOnBack) == 1);
	TestEqual(TEXT("no guard"), RW.M().NotPortedCount(), 0);
	RW.M().SetMove(FVector2D(0.0, 0.0));
	RW.M().PressJump();
	for (int32 I = 0; I < 120; ++I) RW.M().Advance(Step);
	TestTrue(TEXT("up again"), std::holds_alternative<runline::Ground>(RW.M().Core().state));
	return true;
}

// Review Important 1: when the core respawns her itself (falling past the kill plane), the camera
// starts at the respawn point; it must not sweep there from where she died.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineMovementDeathCut, "Runline.Movement.RespawnCutsTheCamera",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineMovementDeathCut::RunTest(const FString&)
{
	FRunnerWorld RW;
	RW.M().Advance(10 * Step);
	RW.M().MutableCore().feet = {0.0f, -39.99f, 0.0f};   // just above the kill plane (y -40)
	RW.M().MutableCore().state = runline::Air{};
	RW.M().MutableCore().vel = {0.0f, -10.0f, 0.0f};
	RW.M().Advance(Step);
	TestEqual(TEXT("the core respawned her"), RW.Count(runline::EventKind::Death), 1);
	RW.M().Advance(0.5f * Step);   // half way to the next step
	const FVector Want = RunlineUnits::ToUnrealPosition(RW.M().Core().GetView().eye);
	TestTrue(TEXT("the camera is at the respawn, not half way from the street"), RW.M().GetRenderBaseEye().Equals(Want, 1e-3));
	return true;
}

// Review Important 2: turns the core makes (the 180) are drawn smoothly between steps, and look
// the core will drop (mid-turn yaw) isn't drawn; look it will take is drawn at once.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineMovementViewBetweenSteps, "Runline.Movement.ViewBetweenSteps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineMovementViewBetweenSteps::RunTest(const FString&)
{
	FRunnerWorld RW;
	RW.M().Advance(10 * Step);
	// Look it will take shows at once, before any step.
	const double Yaw0 = RW.M().GetRenderRotation().Yaw;
	RW.M().AddLook(FVector2D(0.1, 0.0));
	TestEqual(TEXT("a mouse turn shows at once"), RW.M().GetRenderRotation().Yaw, Yaw0 - FMath::RadiansToDegrees(0.1), 1e-3);
	RW.M().Advance(Step);
	// The 180 at 144 fps: the view moves every frame, never holding still between steps.
	RW.M().PressTurn();
	TArray<double> Yaws;
	for (int32 F = 0; F < 60; ++F)
	{
		RW.M().Advance(1.0f / 144.0f);
		Yaws.Add(RW.M().GetRenderRotation().Yaw);
	}
	int32 Held = 0;
	// While the turn runs (turn180Time, 0.2 s), give or take a step at each end.
	const int32 TurnFrames = FMath::FloorToInt(RW.M().Core().tuning.turn180Time * 144.0f);
	for (int32 F = 2; F < TurnFrames - 2; ++F) Held += FMath::IsNearlyEqual(Yaws[F], Yaws[F - 1], 1e-6) ? 1 : 0;
	TestEqual(TEXT("the turn moves every frame (no stair steps)"), Held, 0);
	// Mid-turn the core drops yaw look: the view mustn't show it and then snap back.
	RW.M().PressTurn();
	RW.M().Advance(Step);
	TestFalse(TEXT("turning"), RW.M().Core().TakesYawLook());
	const double Before = RW.M().GetRenderRotation().Yaw;
	RW.M().AddLook(FVector2D(0.5, 0.0));
	TestEqual(TEXT("dropped look isn't drawn"), RW.M().GetRenderRotation().Yaw, Before, 1e-6);
	return true;
}

// The effects the runner shows are exactly faith-runner's: the LookLimiter and CameraFx run after
// each step, as its app does.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineMovementEffectsMatch, "Runline.Movement.EffectsMatchCoreDirect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineMovementEffectsMatch::RunTest(const FString&)
{
	FSettingsScope Scope(90.0f, 1.0f);
	FRunnerWorld RW;
	runline::BoxWorld Ref;
	Ref.Add(runline::Aabb::New({-50.0f, -1.0f, -50.0f}, {50.0f, 0.0f, 50.0f}));
	runline::Controller Bare(runline::Tuning{}, {0.0f, 0.0f, 0.0f}, 0.0f);
	Bare.Respawn();
	runline::LookLimiter Limiter;
	runline::CameraFx Fx;
	runline::Shot Shot;
	// As faith-runner's app builds it: the stick clamped to length 1, the raw strafe kept.
	runline::Input In;
	In.moveAxis = runline::Vec2{0.3f, 1.0f}.ClampLengthMax(1.0f);
	In.strafeRaw = 0.3f;
	RW.M().SetMove(FVector2D(0.3, 1.0));
	for (int32 I = 0; I < 120; ++I)
	{
		RW.M().Advance(Step);
		Bare.Step(Step, In, Ref);
		Limiter.Apply(Bare, Step);
		Shot = Fx.Update(Step, Bare, In);
	}
	const runline::Shot& Got = RW.M().LastShot();
	TestTrue(TEXT("same shot"), Same(Got.view.eye, Shot.view.eye) && Same(Got.view.yaw, Shot.view.yaw) && Same(Got.view.pitch, Shot.view.pitch)
		&& Same(Got.view.roll, Shot.view.roll) && Same(Got.view.fovDeg, Shot.view.fovDeg) && Same(Got.stepPhase, Shot.stepPhase));
	TestTrue(TEXT("she's bobbing (sprinting)"), Got.gait > 0.5f);
	return true;
}

// Review Focus 3: a death cuts the effects too: no shake left, and the offsets don't sweep.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineMovementEffectsCut, "Runline.Movement.EffectsCutOnRespawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineMovementEffectsCut::RunTest(const FString&)
{
	FSettingsScope Scope(90.0f, 1.0f);
	FRunnerWorld RW;
	RW.M().SetMove(FVector2D(0.0, 1.0));
	RW.M().Advance(60 * Step);
	RW.M().MutableCore().feet = {0.0f, -39.99f, 0.0f};
	RW.M().MutableCore().state = runline::Air{};
	RW.M().MutableCore().vel = {0.0f, -10.0f, 0.0f};
	RW.M().Advance(Step);
	TestEqual(TEXT("the core respawned her"), RW.Count(runline::EventKind::Death), 1);
	TestEqual(TEXT("no shake after dying"), RW.M().LastShot().shake, 0.0f);
	RW.M().Advance(0.5f * Step);
	const FVector Offset = RunlineUnits::ToUnrealPosition(RW.M().LastShot().view.eye) - RunlineUnits::ToUnrealPosition(RW.M().Core().GetView().eye);
	TestTrue(TEXT("the effects' offset is the current one, not half way from the street"),
		(RW.M().GetRenderEye() - RW.M().GetRenderBaseEye()).Equals(Offset, 1e-3));
	TestTrue(TEXT("and the base eye is at the respawn"), RW.M().GetRenderBaseEye().Equals(RunlineUnits::ToUnrealPosition(RW.M().Core().GetView().eye), 1e-3));
	return true;
}

// Review Focus 4: the unstepped look isn't drawn past the move's look limit.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineMovementPendingLimit, "Runline.Movement.PendingLookRespectsLimits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineMovementPendingLimit::RunTest(const FString&)
{
	FSettingsScope Scope(90.0f, 0.0f);
	FRunnerWorld RW;
	RW.M().Advance(10 * Step);
	// Hanging on a ledge you can look down only about 18 degrees (TdMove_Grab).
	RW.M().MutableCore().state = runline::LedgeHang{{0.0f, 0.0f, 1.0f}, 1.8f, false};
	const float Limit = runline::LookLimitFor(RW.M().Core()).pitchMin;
	RW.M().AddLook(FVector2D(0.0, -1.0));
	const double Shown = RW.M().GetRenderRotation().Pitch;
	TestTrue(TEXT("not drawn below the limit"), Shown >= FMath::RadiansToDegrees(double(Limit)) - 1e-3);
	return true;
}

// Review Focus 5: camera motion 0 leaves no bob, roll or shake (FOV and swan neck stay).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineMovementMotionZero, "Runline.Movement.CameraMotionZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineMovementMotionZero::RunTest(const FString&)
{
	FSettingsScope Scope(95.0f, 0.0f);
	FRunnerWorld RW;
	RW.M().SetMove(FVector2D(0.0, 1.0));
	for (int32 F = 0; F < 200; ++F)
	{
		RW.M().Advance(1.0f / 144.0f);
		if (F > 100)
		{
			if (RW.M().GetRenderRotation().Roll != 0.0) { AddError(TEXT("roll at camera motion 0")); break; }
			if (!RW.M().GetRenderEye().Equals(RW.M().GetRenderBaseEye(), 1e-6)) { AddError(TEXT("bob at camera motion 0")); break; }
			const FRotator R = RW.M().GetRenderRotation();
			if (R.Pitch != 0.0 || R.Yaw != 0.0) { AddError(TEXT("shake or kick in yaw/pitch at camera motion 0")); break; }
		}
	}
	TestTrue(TEXT("she's running"), RW.M().LastShot().gait > 0.5f);
	TestTrue(TEXT("FOV follows the setting (95, plus the sprint add)"), RW.M().GetRenderFov() >= 95.0f && RW.M().GetRenderFov() < 106.0f);
	// The swan neck stays at motion 0: looking down moves the eye forward and down.
	RW.M().AddLook(FVector2D(0.0, -1.2));
	for (int32 F = 0; F < 60; ++F) RW.M().Advance(Step);
	TestFalse(TEXT("swan neck still there"), RW.M().GetRenderEye().Equals(RW.M().GetRenderBaseEye(), 1.0));
	return true;
}

// The camera's roll tips the way camera.rs means it: wallrunning with the wall on her right she
// leans away from it (left), so the camera's right vector points up a little.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineMovementRollSign, "Runline.Movement.RollSign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineMovementRollSign::RunTest(const FString&)
{
	runline::Controller C(runline::Tuning{}, {0.0f, 1.0f, 0.0f}, 0.0f);   // facing -Z, right is +X
	C.state = runline::WallRun{{-1.0f, 0.0f, 0.0f}, 0.1f};                  // wall on the right
	runline::CameraFx Fx;
	runline::Shot S;
	for (int32 I = 0; I < 60; ++I) S = Fx.Update(Step, C, runline::Input{});
	TestTrue(TEXT("camera.rs: negative roll (head tilts left, away from the wall)"), S.view.roll < -0.1f);
	const FVector Right = FRotationMatrix(RunlineUnits::ToUnrealRotator(S.view.yaw, 0.0f, S.view.roll)).GetScaledAxis(EAxis::Y);
	TestTrue(TEXT("Unreal: the right side comes up"), Right.Z > 0.05);
	return true;
}

// The template's anim BP reads CMC's velocity, acceleration and IsFalling(); the component keeps
// them in step with the core, by direct assignment, never SetMovementMode (whose Walking mode runs
// CMC's floor adjustment and can move the capsule).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineMovementAnimLabels, "Runline.Movement.AnimLabels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineMovementAnimLabels::RunTest(const FString&)
{
	FRunnerWorld RW;
	const int32 ModesAtStart = RW.Runner->ModeChanges;
	RW.M().SetMove(FVector2D(0.0, 1.0));
	for (int32 I = 0; I < 60; ++I) RW.M().Advance(Step);   // one step a frame (one long Advance is a hitch, capped)
	TestFalse(TEXT("running: not falling"), RW.M().IsFalling());
	TestTrue(TEXT("running: on the ground"), RW.M().IsMovingOnGround());
	TestTrue(TEXT("running: accelerating"), RW.M().GetCurrentAcceleration().Size() > 0.0);
	TestTrue(TEXT("running: velocity"), RW.M().Velocity.Size() > 100.0);
	RW.M().PressJump();
	bool bFell = false;
	for (int32 I = 0; I < 10; ++I)
	{
		RW.M().Advance(Step);
		bFell |= RW.M().IsFalling();
	}
	TestTrue(TEXT("jumping: falling"), bFell);
	RW.M().SetMove(FVector2D::ZeroVector);
	for (int32 I = 0; I < 120; ++I) RW.M().Advance(Step);
	TestFalse(TEXT("landed: not falling"), RW.M().IsFalling());
	TestTrue(TEXT("standing: no acceleration"), RW.M().GetCurrentAcceleration().IsZero());
	TestEqual(TEXT("SetMovementMode never ran after BeginPlay"), RW.Runner->ModeChanges, ModesAtStart);
	return true;
}

// Review Critical 1: the roll somersault turns the view through a full circle in CameraFx; like
// faith-runner's app (main.rs: pitch clamped to +-pi/2), the rendered pitch never passes vertical.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineMovementFlipUpright, "Runline.Movement.RollFlipStaysUpright",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineMovementFlipUpright::RunTest(const FString&)
{
	FSettingsScope Scope(90.0f, 1.0f);
	FRunnerWorld RW;
	RW.M().SpawnAt({0.0f, 6.0f, 0.0f}, 0.0f);   // a 6 m drop: roll it
	double Worst = 0.0;
	for (int32 F = 0; F < 144 * 3; ++F)
	{
		if (F == 110) { RW.M().PressCrouch(); RW.M().SetCrouchHeld(true); }
		RW.M().Advance(1.0f / 144.0f);
		Worst = FMath::Max(Worst, FMath::Abs(RW.M().GetRenderRotation().Pitch));
	}
	TestEqual(TEXT("she rolled"), RW.Count(runline::EventKind::Roll), 1);
	TestTrue(FString::Printf(TEXT("pitch never past vertical (worst %.1f deg)"), Worst), Worst <= 90.0 + 1e-3);
	return true;
}

// Review Important 2: look the core has stepped but the LookLimiter pulls back isn't drawn past
// the limit (no sawtooth when pushing against it).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineMovementSteppedLimit, "Runline.Movement.SteppedLookRespectsLimits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineMovementSteppedLimit::RunTest(const FString&)
{
	FSettingsScope Scope(90.0f, 0.0f);
	FRunnerWorld RW;
	RW.M().Advance(10 * Step);
	const double Limit = FMath::RadiansToDegrees(double(runline::LookLimitFor(RW.M().Core()).pitchMin));
	double Worst = 0.0;
	for (int32 F = 0; F < 288; ++F)
	{
		RW.M().AddLook(FVector2D(0.0, -0.02));   // keep pushing down
		RW.M().Advance(1.0f / 144.0f);
		if (F > 100) Worst = FMath::Min(Worst, RW.M().GetRenderRotation().Pitch - Limit);
	}
	TestTrue(FString::Printf(TEXT("never drawn below the limit (worst %.3f deg)"), Worst), Worst >= -1e-3);
	return true;
}

// Review Important 5: a manual respawn (R, the menu) starts the camera effects afresh.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineMovementRespawnFx, "Runline.Movement.ManualRespawnResetsEffects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineMovementRespawnFx::RunTest(const FString&)
{
	FSettingsScope Scope(90.0f, 1.0f);
	FRunnerWorld RW;
	RW.M().SetMove(FVector2D(0.0, 1.0));
	for (int32 F = 0; F < 180; ++F) RW.M().Advance(Step);
	TestTrue(TEXT("bobbing before"), !RW.M().GetRenderEye().Equals(RW.M().GetRenderBaseEye(), 1e-3));
	RW.M().Respawn();
	TestTrue(TEXT("no effect offset right after the respawn"), RW.M().GetRenderEye().Equals(RW.M().GetRenderBaseEye(), 1e-4));
	TestEqual(TEXT("no roll right after the respawn"), RW.M().GetRenderRotation().Roll, 0.0, 1e-6);
	return true;
}

// Game speed: game time runs faster than real time; the core still steps at faith-runner's 1/60 s
// (bit-exact), there are just more steps per real second. Look stays real-time.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineMovementGameSpeed, "Runline.Movement.GameSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineMovementGameSpeed::RunTest(const FString&)
{
	FRunnerWorld RW;
	TestEqual(TEXT("the component's own default is faith-runner's speed"), RW.M().GameSpeed, 1.0f);
	RW.M().GameSpeed = 1.25f;
	for (int32 F = 0; F < 60; ++F) RW.M().Advance(1.0f / 60.0f);   // one real second
	TestTrue(FString::Printf(TEXT("75 steps in a real second (%lld)"), RW.M().StepCount()), FMath::Abs(RW.M().StepCount() - 75) <= 1);
	TestTrue(TEXT("each step is still 1/60 s of core time"), RW.Inputs.size() == size_t(RW.M().StepCount()));
	return true;
}

#endif
