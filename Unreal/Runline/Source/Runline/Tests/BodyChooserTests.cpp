#include "Misc/AutomationTest.h"
#include "RunlineBodyChooser.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr float Frame = 1.0f / 60.0f;
	/// Every clip loaded, 1 s long; loops authored at walk 1.5, run 4, sprint 6, crouch 1 m/s.
	FRunlineBodyChooser Chooser(TSet<ERunlineAnimSlot> Missing = {})
	{
		FRunlineBodyChooser C;
		C.Clips = [Missing](ERunlineAnimSlot S)
		{
			FRunlineClipInfo I;
			I.Length = 1.0f;
			I.bLoaded = !Missing.Contains(S);
			I.AuthoredSpeed = S == ERunlineAnimSlot::Walk ? 1.5f : S == ERunlineAnimSlot::Run ? 4.0f : S == ERunlineAnimSlot::Sprint ? 6.0f : S == ERunlineAnimSlot::CrouchWalk ? 1.0f : 0.0f;
			I.HoldAt = S == ERunlineAnimSlot::Hang ? 0.35f : 0.0f;
			return I;
		};
		return C;
	}
	FRunlineBodyInput In(const runline::State& S, float Speed = 0.0f)
	{
		FRunlineBodyInput I;
		I.State = S;
		I.Vel = {0.0f, 0.0f, -Speed};
		I.Dt = Frame;
		return I;
	}
	FRunlineBodyPose Settle(FRunlineBodyChooser& C, const FRunlineBodyInput& I, int32 N = 60)
	{
		FRunlineBodyPose P;
		for (int32 K = 0; K < N; ++K) P = C.Choose(I);
		return P;
	}
	/// The layer of slot S (weight 0 if absent).
	FRunlineBodyLayer Layer(const FRunlineBodyPose& P, ERunlineAnimSlot S)
	{
		for (const FRunlineBodyLayer& L : P.Layers) if (L.Slot == S) return L;
		return {S, 0.0f, 0.0f};
	}
	float Total(const FRunlineBodyPose& P) { float W = 0; for (const FRunlineBodyLayer& L : P.Layers) W += L.Weight; return W; }
	runline::Event Land(float Impact) { runline::Event E{runline::EventKind::Land}; E.impact = Impact; return E; }
}

// Running at 4.5 m/s: between the run (4) and sprint (6) loops, at one shared phase; the cycle plays
// at 4.5 / 4 the run clip's rate (it dominates).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineBodyLocomotion, "Runline.Body.LocomotionBySpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineBodyLocomotion::RunTest(const FString&)
{
	FRunlineBodyChooser C = Chooser();
	const FRunlineBodyPose P = Settle(C, In(runline::Ground{}, 4.5f));
	const FRunlineBodyLayer Run = Layer(P, ERunlineAnimSlot::Run), Sprint = Layer(P, ERunlineAnimSlot::Sprint);
	TestEqual(TEXT("weights sum to 1"), Total(P), 1.0f, 1e-4f);
	TestEqual(TEXT("run 75%"), Run.Weight, 0.75f, 1e-3f);
	TestEqual(TEXT("sprint 25%"), Sprint.Weight, 0.25f, 1e-3f);
	TestEqual(TEXT("one phase"), Run.Time, Sprint.Time, 1e-4f);
	const float Before = Run.Time;
	const FRunlineBodyPose Q = C.Choose(In(runline::Ground{}, 4.5f));
	TestEqual(TEXT("the cycle runs at 4.5 / 4"), FMath::Fmod(Layer(Q, ERunlineAnimSlot::Run).Time - Before + 1.0f, 1.0f), Frame * 4.5f / 4.0f, 1e-4f);
	TestEqual(TEXT("standing: idle"), Layer(Settle(C, In(runline::Ground{}, 0.0f)), ERunlineAnimSlot::Idle).Weight, 1.0f, 1e-4f);
	return true;
}

// Review Focus 4: a vault half way through its Duration() is half way through its clip, whatever
// the frame times; the step alpha counts.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineBodyOneShots, "Runline.Body.OneShotsFollowTheCoreTimer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineBodyOneShots::RunTest(const FString&)
{
	FRunlineBodyChooser C = Chooser();
	runline::Vault V;
	V.kind = 3;   // VaultOver: 0 + 0.35 + 0.3 s
	V.t = V.Duration() * 0.5f;
	FRunlineBodyInput I = In(V);
	I.Dt = 0.25f;   // a hitch
	TestEqual(TEXT("half way"), Layer(Settle(C, I, 3), ERunlineAnimSlot::Vault).Time, 0.5f, 1e-4f);
	I.StepAlpha = 0.5f;
	TestEqual(TEXT("plus half a step"), Layer(C.Choose(I), ERunlineAnimSlot::Vault).Time, (V.t + 0.5f * I.StepSeconds) / V.Duration(), 1e-4f);
	runline::Roll R;
	R.t = I.CoreTuning.rollTime * 0.25f;
	TestEqual(TEXT("a roll spans rollTime"), Layer(Settle(C, In(R)), ERunlineAnimSlot::Roll).Time, 0.25f, 1e-4f);
	V.kind = 1;   // a step-up
	V.t = 0.0f;
	TestTrue(TEXT("step-ups use StepUp"), Layer(Settle(C, In(V)), ERunlineAnimSlot::StepUp).Weight > 0.99f);
	return true;
}

// Landings pick by speed and impact, play over the locomotion, and fade out.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineBodyLandings, "Runline.Body.LandingsBySpeedAndImpact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineBodyLandings::RunTest(const FString&)
{
	FRunlineBodyChooser C = Chooser();
	Settle(C, In(runline::Ground{}, 6.5f));
	FRunlineBodyInput I = In(runline::Ground{}, 6.5f);
	I.Events = {Land(9.0f)};
	C.Choose(I);
	I.Events.Reset();
	const FRunlineBodyPose P = Settle(C, I, 6);   // past the overlay's fade-in
	TestTrue(TEXT("sprint, heavy"), Layer(P, ERunlineAnimSlot::LandSprintHeavy).Weight > 0.99f);
	const FRunlineBodyPose After = Settle(C, I, 70);
	TestEqual(TEXT("faded out after its length"), Layer(After, ERunlineAnimSlot::LandSprintHeavy).Weight, 0.0f, 1e-4f);
	FRunlineBodyChooser D = Chooser();
	Settle(D, In(runline::Ground{}, 0.5f));
	FRunlineBodyInput J = In(runline::Ground{}, 0.5f);
	J.Events = {Land(3.0f)};
	D.Choose(J);
	J.Events.Reset();
	TestTrue(TEXT("standing, light"), Layer(Settle(D, J, 6), ERunlineAnimSlot::LandStandLight).Weight > 0.99f);
	return true;
}

// Review Focus 2: a move the core ends early doesn't run on: its clip cross-fades out.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineBodyEndsWithMove, "Runline.Body.ClipEndsWithTheMove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineBodyEndsWithMove::RunTest(const FString&)
{
	FRunlineBodyChooser C = Chooser();
	runline::Vault V;
	V.kind = 3;
	V.t = 0.1f;
	Settle(C, In(V), 10);
	const FRunlineBodyPose First = C.Choose(In(runline::Ground{}, 4.0f));
	TestTrue(TEXT("still mostly the vault on the first frame (a cross-fade)"), Layer(First, ERunlineAnimSlot::Vault).Weight > 0.8f);
	const FRunlineBodyPose Later = Settle(C, In(runline::Ground{}, 4.0f), 12);
	TestEqual(TEXT("gone after 0.15 s"), Layer(Later, ERunlineAnimSlot::Vault).Weight, 0.0f, 1e-4f);
	TestEqual(TEXT("weights sum to 1 throughout"), Total(First), 1.0f, 1e-4f);
	return true;
}

// Review Focus 3: a respawn cuts: no fade from the old pose.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineBodySnap, "Runline.Body.SnapCuts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineBodySnap::RunTest(const FString&)
{
	FRunlineBodyChooser C = Chooser();
	Settle(C, In(runline::Roll{0.2f}));
	FRunlineBodyInput I = In(runline::Ground{});
	I.SnapCount = 1;
	const FRunlineBodyPose P = C.Choose(I);
	TestEqual(TEXT("no roll left"), Layer(P, ERunlineAnimSlot::Roll).Weight, 0.0f, 1e-6f);
	TestEqual(TEXT("idle at once"), Layer(P, ERunlineAnimSlot::Idle).Weight, 1.0f, 1e-6f);
	return true;
}

// Review Focus 5: a missing clip falls back to the locomotion loop, with one warning per slot.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineBodyMissing, "Runline.Body.MissingSlotUsesLocomotion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineBodyMissing::RunTest(const FString&)
{
	FRunlineBodyChooser C = Chooser({ERunlineAnimSlot::Roll});
	const FRunlineBodyPose P = Settle(C, In(runline::Roll{0.1f}, 4.0f));
	TestEqual(TEXT("no roll"), Layer(P, ERunlineAnimSlot::Roll).Weight, 0.0f, 1e-6f);
	TestEqual(TEXT("the run loop instead"), Layer(P, ERunlineAnimSlot::Run).Weight, 1.0f, 1e-4f);
	TestEqual(TEXT("one warning"), C.Warnings(), 1);
	return true;
}

// Stand-ins and the slide: hang held at HoldAt; wallrun leans away from the wall; the slide's
// into-then-loop.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineBodyStandIns, "Runline.Body.StandInsAndSlide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineBodyStandIns::RunTest(const FString&)
{
	FRunlineBodyChooser C = Chooser();
	const FRunlineBodyPose Hang = Settle(C, In(runline::LedgeHang{{0.0f, 0.0f, 1.0f}, 2.0f, false}));
	TestEqual(TEXT("hang held at 35%"), Layer(Hang, ERunlineAnimSlot::Hang).Time, 0.35f, 1e-4f);
	// Pose-capture finding (M3b Task 7): the hang stand-in brings the copied first-person hands through the
	// camera (a mangled, oversized hand): the arms hide while it plays; elsewhere they show.
	TestTrue(TEXT("hang stand-in hides the first-person arms"), Hang.bHideArms);
	TestFalse(TEXT("running shows them"), Settle(C, In(runline::Ground{}, 4.0f)).bHideArms);
	FRunlineBodyInput W = In(runline::WallRun{{-1.0f, 0.0f, 0.0f}, 0.3f}, 6.0f);   // wall on her right (core yaw 0)
	W.WallNormal = {-1.0f, 0.0f, 0.0f};
	const FRunlineBodyPose Run = Settle(C, W);
	TestTrue(TEXT("sprinting"), Layer(Run, ERunlineAnimSlot::Sprint).Weight > 0.99f);
	TestEqual(TEXT("leaning 20 degrees, away from the wall (left)"), Run.LeanDegrees, -20.0f, 1e-3f);
	TestTrue(TEXT("slide: into first"), Layer(Settle(C, In(runline::Slide{0.1f})), ERunlineAnimSlot::SlideInto).Weight > 0.99f);
	TestTrue(TEXT("then the loop"), Layer(Settle(C, In(runline::Slide{0.6f})), ERunlineAnimSlot::SlideLoop).Weight > 0.99f);
	return true;
}


// Final review Important 1: a landing overlay fades in, and a jump out of it fades it out (it used to
// appear and vanish in one frame: a pop she sees in first person on every land-and-jump).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineBodyOverlayFades, "Runline.Body.OverlaysFadeInAndOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineBodyOverlayFades::RunTest(const FString&)
{
	FRunlineBodyChooser C = Chooser();
	Settle(C, In(runline::Ground{}, 4.0f));
	FRunlineBodyInput I = In(runline::Ground{}, 4.0f);
	I.Events = {Land(3.0f)};
	const float First = Layer(C.Choose(I), ERunlineAnimSlot::LandRunLight).Weight;
	TestTrue(TEXT("fading in, not at full weight on the first frame"), First > 0.0f && First < 0.5f);
	I.Events.Reset();
	float Prev = Layer(Settle(C, I, 6), ERunlineAnimSlot::LandRunLight).Weight;
	TestTrue(TEXT("full after the fade-in"), Prev > 0.99f);
	FRunlineBodyInput Jump = In(runline::Air{}, 4.0f);
	Jump.Events = {runline::Event{runline::EventKind::Jump}};
	for (int32 K = 0; K < 15; ++K)
	{
		const float W = Layer(C.Choose(Jump), ERunlineAnimSlot::LandRunLight).Weight;
		Jump.Events.Reset();
		TestTrue(*FString::Printf(TEXT("frame %d: fades out (%.3f -> %.3f)"), K, Prev, W), Prev - W <= 0.12f);
		Prev = W;
	}
	TestEqual(TEXT("gone after 0.25 s"), Prev, 0.0f, 1e-4f);
	return true;
}

#endif
