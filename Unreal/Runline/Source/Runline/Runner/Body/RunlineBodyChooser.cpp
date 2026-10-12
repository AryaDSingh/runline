#include "RunlineBodyChooser.h"
#include "RunlineMovementComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogRunlineBody, Log, All);

namespace
{
	using S = ERunlineAnimSlot;
	constexpr int32 LocomotionKey = 100000;
	/// Speeds (m/s) that pick the standing, running or sprinting version of a jump or a landing.
	constexpr float StandBelow = 1.5f, RunBelow = 5.5f;
	/// A landing harder than this (m/s down) is a heavy one.
	constexpr float HeavyImpact = 7.0f;
	/// The slide's into clip spans its first quarter second.
	constexpr float SlideIntoTime = 0.25f;
	/// An overlay fades out over the last part of its window.
	constexpr float OverlayFadeFrom = 0.7f;
	/// An overlay fades in over this; cut short (she leaves the ground), it fades out over this.
	constexpr float OverlayFadeIn = 0.06f, OverlayCutFade = 0.15f;

	float Frac(float Num, float Den) { return Den > 0.0f ? FMath::Clamp(Num / Den, 0.0f, 1.0f) : 1.0f; }
	S BySpeed(float Speed, S Stand, S Run, S Sprint) { return Speed < StandBelow ? Stand : Speed < RunBelow ? Run : Sprint; }
}

FRunlineBodyInput FRunlineBodyInput::From(const URunlineMovementComponent& M, float Dt)
{
	const runline::Controller& C = M.Core();
	FRunlineBodyInput In;
	In.State = C.state;
	In.bCrouched = C.crouched;
	In.Vel = C.vel;
	if (const auto* W = std::get_if<runline::WallRun>(&C.state)) In.WallNormal = W->normal;
	In.Yaw = C.yaw;
	In.CoreTuning = C.tuning;
	In.StepSeconds = URunlineMovementComponent::StepSeconds;
	In.StepAlpha = M.GetStepAlpha();
	In.Dt = Dt;
	In.Events = M.GetFrameEvents();
	In.SnapCount = M.GetSnapCount();
	return In;
}

void FRunlineBodyChooser::Reset()
{
	Main = FMain{};
	Fading.Reset();
	Overlay.Reset();
	OverlayWeight = 0.0f;
	bOverlayCut = false;
	AirStart.Reset();
	FallTime = 0.0f;
	LocoPhase = 0.0f;
	bWasSliding = false;
}

FRunlineClipInfo FRunlineBodyChooser::Info(ERunlineAnimSlot Slot) const
{
	return Clips ? Clips(Slot) : FRunlineClipInfo{};
}

bool FRunlineBodyChooser::Has(ERunlineAnimSlot Slot)
{
	const FRunlineClipInfo I = Info(Slot);
	if (I.bLoaded && I.Length > 0.0f) return true;
	if (!Warned.Contains(Slot))
	{
		Warned.Add(Slot);
		UE_LOG(LogRunlineBody, Warning, TEXT("Runline body: no clip for slot %s; the locomotion loop plays instead"), RunlineAnimSlotName(Slot));
	}
	return false;
}

FRunlineBodyChooser::FMain FRunlineBodyChooser::Locomotion(bool bCrouched, float Speed, float Dt)
{
	// The loops by their capture speed (standing still is 0); missing ones are left out.
	TArray<TPair<S, float>, TInlineAllocator<4>> Loops;
	const S Order[] = {S::Idle, S::Walk, S::Run, S::Sprint};
	const S CrouchOrder[] = {S::CrouchIdle, S::CrouchWalk};
	const TArrayView<const S> Candidates = bCrouched ? TArrayView<const S>(CrouchOrder) : TArrayView<const S>(Order);
	for (const S L : Candidates)
		if (Info(L).bLoaded && Info(L).Length > 0.0f) Loops.Add({L, L == S::Idle || L == S::CrouchIdle ? 0.0f : Info(L).AuthoredSpeed});
	FMain M;
	M.bLocomotion = true;
	M.Key = LocomotionKey + (bCrouched ? 1 : 0);
	if (Loops.Num() == 0)
	{
		M.A = M.B = S::Idle;
		return M;
	}
	// The two loops either side of her speed.
	int32 Hi = 0;
	while (Hi < Loops.Num() && Loops[Hi].Value < Speed) ++Hi;
	if (Hi == 0) { M.A = M.B = Loops[0].Key; }
	else if (Hi == Loops.Num()) { M.A = M.B = Loops.Last().Key; }
	else
	{
		M.A = Loops[Hi - 1].Key;
		M.B = Loops[Hi].Key;
		M.BWeight = Frac(Speed - Loops[Hi - 1].Value, Loops[Hi].Value - Loops[Hi - 1].Value);
	}
	// One shared phase, advanced at the dominant loop's rate: her speed over its capture speed.
	const S Dominant = M.BWeight > 0.5f ? M.B : M.A;
	const FRunlineClipInfo D = Info(Dominant);
	const float Rate = D.AuthoredSpeed > 0.0f ? FMath::Clamp(Speed / D.AuthoredSpeed, 0.7f, 1.4f) : 1.0f;
	const float Span = D.End() - D.Start();
	if (Span > 0.0f) LocoPhase = FMath::Frac(LocoPhase + Dt * Rate / Span);
	M.Phase = LocoPhase;
	return M;
}

FRunlineBodyChooser::FMain FRunlineBodyChooser::MainFor(const FRunlineBodyInput& In, float Speed)
{
	const runline::Tuning& Tu = In.CoreTuning;
	const float Late = In.StepAlpha * In.StepSeconds;
	const runline::State& St = In.State;
	// One clip, positioned at Fraction of its window (or Time seconds into it).
	const auto One = [&](S Slot, float Fraction, float FadeTime = 0.15f) -> FMain
	{
		if (!Has(Slot)) return Locomotion(In.bCrouched, Speed, In.Dt);
		FMain M;
		M.Slot = Slot;
		M.Time = Info(Slot).At(Fraction);
		M.Key = int32(St.index()) * 64 + int32(Slot);
		M.FadeTime = FadeTime;
		return M;
	};
	const auto Seconds = [&](S Slot, float T) -> FMain
	{
		const FRunlineClipInfo I = Info(Slot);
		return One(Slot, Frac(T, I.End() - I.Start()));
	};
	if (const auto* R = std::get_if<runline::Roll>(&St)) return One(S::Roll, Frac(R->t + Late, Tu.rollTime), 0.08f);
	if (const auto* St2 = std::get_if<runline::Stunned>(&St)) return One(S::Stumble, Frac(St2->t + Late, Tu.hardLandStun));
	if (const auto* V = std::get_if<runline::Vault>(&St))
		return One(V->kind <= 1 ? S::StepUp : V->Onto() ? S::Mantle : S::Vault, Frac(V->t + Late, V->Duration()));
	if (const auto* Tr = std::get_if<runline::Traverse>(&St))
	{
		const S Slot = Tr->kind == runline::TraverseKind::PullUp ? S::PullUp : Tr->kind == runline::TraverseKind::SpringBoard ? S::Springboard : S::Mantle;
		return One(Slot, Frac(Tr->t + Late, Tr->dur));
	}
	if (const auto* Sl = std::get_if<runline::Slide>(&St))
	{
		const float T = Sl->t + Late;
		if (T < SlideIntoTime) return One(S::SlideInto, Frac(T, SlideIntoTime), 0.08f);
		if (!Has(S::SlideLoop)) return Locomotion(true, Speed, In.Dt);
		const FRunlineClipInfo I = Info(S::SlideLoop);
		const float Span = I.End() - I.Start();
		FMain M = One(S::SlideLoop, 0.0f);
		M.Time = I.Start() + (Span > 0.0f ? FMath::Fmod(T - SlideIntoTime, Span) : 0.0f);
		return M;
	}
	if (std::holds_alternative<runline::Air>(St))
	{
		if (AirStart)
		{
			const FRunlineClipInfo I = Info(AirStart->Slot);
			if (AirStart->T < I.End() - I.Start()) return Seconds(AirStart->Slot, AirStart->T);
		}
		if (!Has(S::Fall)) return Locomotion(false, Speed, In.Dt);
		const FRunlineClipInfo I = Info(S::Fall);
		const float Span = I.End() - I.Start();
		FMain M = One(S::Fall, 0.0f);
		M.Time = I.Start() + (Span > 0.0f ? FMath::Fmod(FallTime, Span) : 0.0f);
		return M;
	}
	// Stand-ins (spec §4): marked in the anim set; M3c replaces them.
	if (const auto* Wc = std::get_if<runline::WallClimb>(&St)) return Seconds(S::PullUp, Wc->t + Late);
	if (const auto* Wt = std::get_if<runline::WallClimbTurned>(&St)) return Seconds(S::JumpRun, Wt->t + Late);
	if (std::holds_alternative<runline::LedgeHang>(St) || std::holds_alternative<runline::GrabTransfer>(St))
		return One(S::Hang, Info(S::Hang).HoldAt);
	if (const auto* Lay = std::get_if<runline::LayOnGround>(&St))
	{
		// Lying: the stumble's end; getting up: played back to the start of its window.
		const float Up = Lay->gettingUp ? Frac(*Lay->gettingUp + Late, Tu.layOnGroundGetUpTime) : 0.0f;
		return One(S::Stumble, 1.0f - Up);
	}
	// Ground, SoftLand, WallRun (sprinting, leaned: the stand-in): locomotion.
	return Locomotion(In.bCrouched && std::holds_alternative<runline::Ground>(St), Speed, In.Dt);
}

void FRunlineBodyChooser::AddLayers(const FMain& M, float Scale, FRunlineBodyPose& Out) const
{
	const float W = M.Weight * Scale;
	if (W <= 0.0f) return;
	if (!M.bLocomotion)
	{
		Out.Layers.Add({M.Slot, M.Time, W});
		return;
	}
	const auto Loop = [&](S Slot, float Weight)
	{
		if (Weight <= 0.0f) return;
		const FRunlineClipInfo I = Info(Slot);
		Out.Layers.Add({Slot, I.Start() + M.Phase * (I.End() - I.Start()), Weight});
	};
	if (M.A == M.B) Loop(M.A, W);
	else
	{
		Loop(M.A, W * (1.0f - M.BWeight));
		Loop(M.B, W * M.BWeight);
	}
}

FRunlineBodyPose FRunlineBodyChooser::Choose(const FRunlineBodyInput& In)
{
	bool bCut = false;
	if (!LastSnap || *LastSnap != In.SnapCount)
	{
		Reset();
		LastSnap = In.SnapCount;
		bCut = true;
	}
	const float Speed = FMath::Sqrt(In.Vel.x * In.Vel.x + In.Vel.z * In.Vel.z);
	const bool bGround = std::holds_alternative<runline::Ground>(In.State) || std::holds_alternative<runline::SoftLand>(In.State);
	const bool bAir = std::holds_alternative<runline::Air>(In.State);

	// Events: a jump starts the air clip; a landing starts an overlay.
	for (const runline::Event& E : In.Events)
	{
		switch (E.kind)
		{
		case runline::EventKind::Jump:
		case runline::EventKind::WallJump:
		case runline::EventKind::SpringBoard:
			AirStart = FOverlay{BySpeed(Speed, S::JumpStand, S::JumpRun, S::JumpSprint), 0.0f};
			if (!Has(AirStart->Slot)) AirStart.Reset();
			break;
		case runline::EventKind::Land:
		case runline::EventKind::HardLand:
		case runline::EventKind::SoftLand:
		{
			const bool bHeavy = E.impact > HeavyImpact;
			const S Slot = BySpeed(Speed, bHeavy ? S::LandStandHeavy : S::LandStandLight, bHeavy ? S::LandRunHeavy : S::LandRunLight,
				bHeavy ? S::LandSprintHeavy : S::LandSprintLight);
			if (Has(Slot)) { Overlay = FOverlay{Slot, 0.0f}; OverlayWeight = 0.0f; bOverlayCut = false; }
			break;
		}
		default: break;
		}
	}
	// Leaving a slide for the ground: the slide's way out, as an overlay.
	const bool bSliding = std::holds_alternative<runline::Slide>(In.State);
	if (bWasSliding && !bSliding && bGround && Has(S::SlideOut)) { Overlay = FOverlay{S::SlideOut, 0.0f}; OverlayWeight = 0.0f; bOverlayCut = false; }
	bWasSliding = bSliding;

	if (bAir)
	{
		if (AirStart) AirStart->T += In.Dt;
		FallTime += In.Dt;
	}
	else
	{
		AirStart.Reset();
		FallTime = 0.0f;
	}

	// The main clip; a change cross-fades from the old one.
	FMain Want = MainFor(In, Speed);
	if (bCut || Main.Key == INDEX_NONE)
	{
		Fading.Reset();
		Want.Weight = 1.0f;
	}
	else if (Want.Key != Main.Key)
	{
		Fading.Add(Main);
		Want.Weight = 0.0f;
	}
	else Want.Weight = Main.Weight;
	Main = Want;
	const float Step = Main.FadeTime > 0.0f ? In.Dt / Main.FadeTime : 1.0f;
	Main.Weight = FMath::Min(1.0f, Main.Weight + Step);
	for (FMain& F : Fading) F.Weight -= Step;
	Fading.RemoveAll([](const FMain& F) { return F.Weight <= 0.0f; });

	// The overlay (Ground and SoftLand only): full weight, fading over the end of its window.
	// (Leaving the ground cuts it short: it fades out where it is, never pops.)
	if (Overlay && !bGround) bOverlayCut = true;
	if (Overlay)
	{
		const FRunlineClipInfo I = Info(Overlay->Slot);
		const float Span = I.End() - I.Start();
		const float U = Frac(Overlay->T, Span);
		if (bOverlayCut) OverlayWeight -= OverlayCutFade > 0.0f ? In.Dt / OverlayCutFade : 1.0f;
		else
		{
			const float WindowFade = U < OverlayFadeFrom ? 1.0f : 1.0f - Frac(U - OverlayFadeFrom, 1.0f - OverlayFadeFrom);
			OverlayWeight = FMath::Min(FMath::Min(1.0f, OverlayWeight + (OverlayFadeIn > 0.0f ? In.Dt / OverlayFadeIn : 1.0f)), WindowFade);
		}
		if (OverlayWeight <= 0.0f && (bOverlayCut || U >= 1.0f))
		{
			Overlay.Reset();
			OverlayWeight = 0.0f;
			bOverlayCut = false;
		}
	}
	OverlayWeight = FMath::Clamp(OverlayWeight, 0.0f, 1.0f);
	if (!Overlay) OverlayWeight = 0.0f;

	FRunlineBodyPose Out;
	const float Under = 1.0f - OverlayWeight;
	for (const FMain& F : Fading) AddLayers(F, Under, Out);
	AddLayers(Main, Under, Out);
	if (Overlay && OverlayWeight > 0.0f) Out.Layers.Add({Overlay->Slot, Info(Overlay->Slot).Start() + Overlay->T, OverlayWeight});
	if (Overlay && !bOverlayCut) Overlay->T += In.Dt;
	float Total = 0.0f;
	for (const FRunlineBodyLayer& L : Out.Layers) Total += L.Weight;
	if (Total > 0.0f)
		for (FRunlineBodyLayer& L : Out.Layers) L.Weight /= Total;

	Out.bHideArms = std::holds_alternative<runline::LedgeHang>(In.State) || std::holds_alternative<runline::GrabTransfer>(In.State);
	// The wallrun stand-in leans away from the wall: her right is (cos yaw, 0, -sin yaw) (core).
	if (const auto* W = std::get_if<runline::WallRun>(&In.State))
	{
		const float RightDotWall = -(FMath::Cos(In.Yaw) * W->normal.x - FMath::Sin(In.Yaw) * W->normal.z);
		Out.LeanDegrees = RightDotWall > 0.0f ? -20.0f : 20.0f;
	}
	return Out;
}
