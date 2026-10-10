#include "Misc/AutomationTest.h"
#include "RunlineTestRunner.h"
#include "RunlineTestWorld.h"
#include "Components/CapsuleComponent.h"
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

#endif
