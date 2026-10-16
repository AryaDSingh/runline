#include "RunlineBodyChooser.h"

#include <algorithm>
#include <cmath>

namespace
{
	template <class... Ts> struct TOverloaded : Ts... { using Ts::operator()...; };
	template <class... Ts> TOverloaded(Ts...) -> TOverloaded<Ts...>;

	float Clamp01(float V) { return std::clamp(V, 0.0f, 1.0f); }
	bool OnGround(const runline::State& S)
	{
		return std::holds_alternative<runline::Ground>(S) || std::holds_alternative<runline::SoftLand>(S);
	}
	/// Jump starts and landings pick by horizontal speed.
	int SpeedBand(float Speed)
	{
		return Speed < FRunlineBodyChooser::StandSpeed ? 0 : Speed < FRunlineBodyChooser::RunSpeed ? 1 : 2;
	}
	ERunlineAnimSlot JumpSlot(float Speed)
	{
		constexpr ERunlineAnimSlot Slots[] = {ERunlineAnimSlot::JumpStand, ERunlineAnimSlot::JumpRun, ERunlineAnimSlot::JumpSprint};
		return Slots[SpeedBand(Speed)];
	}
	ERunlineAnimSlot LandSlot(float Speed, bool bHeavy)
	{
		constexpr ERunlineAnimSlot Light[] = {ERunlineAnimSlot::LandStandLight, ERunlineAnimSlot::LandRunLight, ERunlineAnimSlot::LandSprintLight};
		constexpr ERunlineAnimSlot Heavy[] = {ERunlineAnimSlot::LandStandHeavy, ERunlineAnimSlot::LandRunHeavy, ERunlineAnimSlot::LandSprintHeavy};
		return (bHeavy ? Heavy : Light)[SpeedBand(Speed)];
	}
	/// Clip time for a one-shot the core times: t of Duration, as seconds into a clip of Length.
	float Timed(float T, float Duration, float Length)
	{
		return Duration > 0.0f ? Clamp01(T / Duration) * Length : Length;
	}
}

FRunlineClipInfo FRunlineBodyChooser::Clip(ERunlineAnimSlot Slot) const
{
	return Clips ? Clips(Slot) : FRunlineClipInfo{};
}

bool FRunlineBodyChooser::Loaded(ERunlineAnimSlot Slot)
{
	if (Clip(Slot).bLoaded) return true;
	if (Warned.insert(Slot).second && OnMissingClip) OnMissingClip(Slot);
	return false;
}

void FRunlineBodyChooser::Reset()
{
	bStarted = false;
	Main = FMain{};
	Fading.clear();
	Overlays.clear();
	bHasAirStart = false;
	Phase = 0.0f;
	FallTime = 0.0f;
	Lean = 0.0f;
}

std::vector<FRunlineBodyLayer> FRunlineBodyChooser::LocomotionLayers(bool bCrouched, float Speed)
{
	// The loops in speed order, at their authored speeds (the idles stand still).
	const std::vector<ERunlineAnimSlot> Order = bCrouched
		? std::vector<ERunlineAnimSlot>{ERunlineAnimSlot::CrouchIdle, ERunlineAnimSlot::CrouchWalk}
		: std::vector<ERunlineAnimSlot>{ERunlineAnimSlot::Idle, ERunlineAnimSlot::Walk, ERunlineAnimSlot::Run, ERunlineAnimSlot::Sprint};
	struct FLoop { ERunlineAnimSlot Slot; float Speed; float Length; };
	std::vector<FLoop> Loops;
	for (const ERunlineAnimSlot S : Order)
	{
		if (!Loaded(S)) continue;
		const FRunlineClipInfo I = Clip(S);
		const bool bIdle = S == ERunlineAnimSlot::Idle || S == ERunlineAnimSlot::CrouchIdle;
		Loops.push_back({S, bIdle ? 0.0f : I.AuthoredSpeed, I.Length});
	}
	if (Loops.empty()) return {};
	const auto At = [this](const FLoop& L, float Weight) { return FRunlineBodyLayer{L.Slot, Phase * L.Length, Weight}; };
	if (Speed <= Loops.front().Speed) return {At(Loops.front(), 1.0f)};
	if (Speed >= Loops.back().Speed) return {At(Loops.back(), 1.0f)};
	for (std::size_t I = 0; I + 1 < Loops.size(); ++I)
	{
		const FLoop& A = Loops[I];
		const FLoop& B = Loops[I + 1];
		if (Speed < B.Speed)
		{
			const float Toward = B.Speed > A.Speed ? (Speed - A.Speed) / (B.Speed - A.Speed) : 1.0f;
			return {At(A, 1.0f - Toward), At(B, Toward)};
		}
	}
	return {At(Loops.back(), 1.0f)};
}

void FRunlineBodyChooser::StepLocomotion(const FRunlineBodyInput& In, float Speed)
{
	// The shared cycle advances at the dominant loop's rate: speed over its authored speed, held to
	// 0.7 to 1.4 (an idle plays at 1).
	const std::vector<FRunlineBodyLayer> Pair = LocomotionLayers(In.bCrouched, Speed);
	if (Pair.empty()) return;
	const FRunlineBodyLayer& Dominant = *std::max_element(Pair.begin(), Pair.end(),
		[](const FRunlineBodyLayer& A, const FRunlineBodyLayer& B) { return A.Weight < B.Weight; });
	const FRunlineClipInfo I = Clip(Dominant.Slot);
	if (I.Length <= 0.0f) return;
	const bool bIdle = Dominant.Slot == ERunlineAnimSlot::Idle || Dominant.Slot == ERunlineAnimSlot::CrouchIdle;
	const float Rate = bIdle || I.AuthoredSpeed <= 0.0f ? 1.0f : std::clamp(Speed / I.AuthoredSpeed, MinRate, MaxRate);
	Phase = std::fmod(Phase + In.Dt * Rate / I.Length, 1.0f);
}

FRunlineBodyChooser::FMain FRunlineBodyChooser::SlotMain(const FRunlineBodyInput& In, ERunlineAnimSlot Slot, float Time)
{
	FMain M;
	M.StateIndex = In.State.index();
	if (!Loaded(Slot))
	{
		// A missing clip plays the locomotion loop for her speed instead.
		M.Kind = In.bCrouched ? EMainKind::CrouchLocomotion : EMainKind::Locomotion;
		return M;
	}
	M.Kind = EMainKind::Slot;
	M.Slot = Slot;
	M.Time = Time;
	return M;
}

FRunlineBodyChooser::FMain FRunlineBodyChooser::MainFor(const FRunlineBodyInput& In)
{
	// Every core timer is read where the render is: StepAlpha of a step later.
	const float Late = In.StepAlpha * In.StepSeconds;
	const runline::Tuning& Tu = In.CoreTuning;
	const auto Len = [this](ERunlineAnimSlot S) { return Clip(S).Length; };
	const auto Locomotion = [&]
	{
		FMain M;
		M.Kind = In.bCrouched ? EMainKind::CrouchLocomotion : EMainKind::Locomotion;
		M.StateIndex = In.State.index();
		return M;
	};
	return std::visit(TOverloaded{
		[&](const runline::Ground&) { return Locomotion(); },
		[&](const runline::SoftLand&) { return Locomotion(); },
		[&](const runline::Air&)
		{
			if (bHasAirStart && AirStart.Time < Len(AirStart.Slot) && Clip(AirStart.Slot).bLoaded)
				return SlotMain(In, AirStart.Slot, AirStart.Time);
			const float L = Len(ERunlineAnimSlot::Fall);
			return SlotMain(In, ERunlineAnimSlot::Fall, L > 0.0f ? std::fmod(FallTime, L) : 0.0f);
		},
		[&](const runline::Slide& S)
		{
			const float T = S.t + Late;
			if (T < SlideIntoTime) return SlotMain(In, ERunlineAnimSlot::SlideInto, Timed(T, SlideIntoTime, Len(ERunlineAnimSlot::SlideInto)));
			const float L = Len(ERunlineAnimSlot::SlideLoop);
			return SlotMain(In, ERunlineAnimSlot::SlideLoop, L > 0.0f ? std::fmod(T - SlideIntoTime, L) : 0.0f);
		},
		[&](const runline::Roll& S) { return SlotMain(In, ERunlineAnimSlot::Roll, Timed(S.t + Late, Tu.rollTime, Len(ERunlineAnimSlot::Roll))); },
		[&](const runline::Stunned& S) { return SlotMain(In, ERunlineAnimSlot::Stumble, Timed(S.t + Late, Tu.hardLandStun, Len(ERunlineAnimSlot::Stumble))); },
		[&](const runline::LayOnGround& S)
		{
			// Lying is the stumble's last frame; getting up plays it back to standing.
			const float L = Len(ERunlineAnimSlot::Stumble);
			if (!S.gettingUp) return SlotMain(In, ERunlineAnimSlot::Stumble, L);
			return SlotMain(In, ERunlineAnimSlot::Stumble, L - Timed(*S.gettingUp + Late, Tu.layOnGroundGetUpTime, L));
		},
		[&](const runline::WallRun&)
		{
			// Stand-in: the sprint loop, on the shared cycle (the lean comes separately).
			return SlotMain(In, ERunlineAnimSlot::Sprint, Phase * Len(ERunlineAnimSlot::Sprint));
		},
		[&](const runline::WallClimb& S)
		{
			return SlotMain(In, ERunlineAnimSlot::PullUp, std::min(S.t + Late, Len(ERunlineAnimSlot::PullUp)));
		},
		[&](const runline::WallClimbTurned& S)
		{
			return SlotMain(In, ERunlineAnimSlot::JumpRun, std::min(S.t + Late, Len(ERunlineAnimSlot::JumpRun)));
		},
		[&](const runline::LedgeHang&) { return SlotMain(In, ERunlineAnimSlot::Hang, Clip(ERunlineAnimSlot::Hang).HoldAt * Len(ERunlineAnimSlot::Hang)); },
		[&](const runline::GrabTransfer&) { return SlotMain(In, ERunlineAnimSlot::Hang, Clip(ERunlineAnimSlot::Hang).HoldAt * Len(ERunlineAnimSlot::Hang)); },
		[&](const runline::Traverse& S)
		{
			const ERunlineAnimSlot Slot =
				S.kind == runline::TraverseKind::PullUp ? ERunlineAnimSlot::PullUp :
				S.kind == runline::TraverseKind::Mantle ? ERunlineAnimSlot::Mantle :
				S.kind == runline::TraverseKind::SpringBoard ? ERunlineAnimSlot::Springboard : ERunlineAnimSlot::Vault;
			return SlotMain(In, Slot, Timed(S.t + Late, S.dur, Len(Slot)));
		},
		[&](const runline::Vault& S)
		{
			// Kinds 0 and 1 are the step-ups; the rest go over (Vault) or onto (Mantle).
			const ERunlineAnimSlot Slot = S.kind <= 1 ? ERunlineAnimSlot::StepUp
				: runline::VAULT_TYPES[S.kind].onto ? ERunlineAnimSlot::Mantle : ERunlineAnimSlot::Vault;
			return SlotMain(In, Slot, Timed(S.t + Late, S.Duration(), Len(Slot)));
		},
	}, In.State);
}

void FRunlineBodyChooser::Events(const FRunlineBodyInput& In, float Speed)
{
	// What's already playing moves on at rate 1.
	for (FOverlay& O : Overlays) O.Time += In.Dt;
	if (bHasAirStart) AirStart.Time += In.Dt;
	FallTime = std::holds_alternative<runline::Air>(LastState) ? FallTime + In.Dt : 0.0f;

	const bool bGround = OnGround(In.State);
	const bool bAir = std::holds_alternative<runline::Air>(In.State);
	// Overlays only play over Ground and SoftLand, and end with their clip.
	Overlays.erase(std::remove_if(Overlays.begin(), Overlays.end(), [&](const FOverlay& O)
	{
		return !bGround || O.Time >= Clip(O.Slot).Length;
	}), Overlays.end());
	if (!bAir) bHasAirStart = false;

	const auto StartOverlay = [&](ERunlineAnimSlot Slot)
	{
		if (!bGround || !Loaded(Slot)) return;
		Overlays.clear();   // a new landing replaces the last
		Overlays.push_back({Slot, 0.0f});
	};
	const auto StartJump = [&]
	{
		bHasAirStart = true;
		AirStart = {JumpSlot(Speed), 0.0f};
	};
	for (const runline::Event& E : In.Events)
	{
		switch (E.kind)
		{
		case runline::EventKind::Jump: StartJump(); break;
		case runline::EventKind::Land: StartOverlay(LandSlot(Speed, E.impact > HeavyImpact)); break;
		case runline::EventKind::HardLand: StartOverlay(LandSlot(Speed, true)); break;
		case runline::EventKind::SoftLand: StartOverlay(LandSlot(Speed, E.impact > HeavyImpact)); break;
		default: break;
		}
	}
	// A slide that ends on her feet gets up with SlideOut; a springboard launches like a jump.
	if (std::holds_alternative<runline::Slide>(LastState) && bGround) StartOverlay(ERunlineAnimSlot::SlideOut);
	if (const auto* T = std::get_if<runline::Traverse>(&LastState); T && T->kind == runline::TraverseKind::SpringBoard && bAir)
		StartJump();
}

std::vector<FRunlineBodyLayer> FRunlineBodyChooser::LayersOf(const FMain& M, float Speed)
{
	if (M.Kind == EMainKind::Slot) return {{M.Slot, M.Time, 1.0f}};
	return LocomotionLayers(M.Kind == EMainKind::CrouchLocomotion, Speed);
}

FRunlineBodyPose FRunlineBodyChooser::Choose(const FRunlineBodyInput& In)
{
	const float Speed = std::hypot(In.Vel.x, In.Vel.z);
	// A snap (respawn, death) or the first frame: cut, no blend.
	const bool bCut = !bStarted || In.SnapCount != LastSnapCount;
	if (bCut)
	{
		Reset();
		bStarted = true;
		LastSnapCount = In.SnapCount;
		LastState = In.State;
	}
	StepLocomotion(In, Speed);
	Events(In, Speed);

	const FMain Want = MainFor(In);
	if (bCut) Main = Want;
	else if (!Want.SameAs(Main))
	{
		// The old main fades out while the new one fades in: quicker into a slide or a roll.
		float Faded = 0.0f;
		for (const FFading& F : Fading) Faded += F.Weight;
		const bool bQuick = std::holds_alternative<runline::Slide>(In.State) || std::holds_alternative<runline::Roll>(In.State);
		Fading.push_back({Main, std::max(0.0f, 1.0f - Faded), 1.0f / (bQuick ? QuickFadeTime : FadeTime)});
		Main = Want;
	}
	else Main.Time = Want.Time;   // the same clip: the core moves it on
	for (FFading& F : Fading) F.Weight -= In.Dt * F.Rate;
	Fading.erase(std::remove_if(Fading.begin(), Fading.end(), [](const FFading& F) { return F.Weight <= 0.0f; }), Fading.end());

	// The wallrun stand-in leans away from the wall, easing in and out over a fade.
	float LeanTarget = 0.0f;
	if (const auto* W = std::get_if<runline::WallRun>(&In.State))
	{
		const runline::Vec3 Right{std::cos(In.Yaw), 0.0f, -std::sin(In.Yaw)};
		const bool bWallOnRight = W->normal.x * Right.x + W->normal.z * Right.z < 0.0f;
		LeanTarget = bWallOnRight ? -WallRunLean : WallRunLean;
	}
	const float LeanStep = In.Dt * WallRunLean / FadeTime;
	Lean = bCut ? LeanTarget : Lean + std::clamp(LeanTarget - Lean, -LeanStep, LeanStep);

	FRunlineBodyPose Pose;
	Pose.LeanDegrees = Lean;
	float Faded = 0.0f;
	for (const FFading& F : Fading)
	{
		Faded += F.Weight;
		for (FRunlineBodyLayer L : LayersOf(F.Main, Speed)) { L.Weight *= F.Weight; Pose.Layers.push_back(L); }
	}
	const float MainWeight = std::max(0.0f, 1.0f - Faded);
	for (FRunlineBodyLayer L : LayersOf(Main, Speed)) { L.Weight *= MainWeight; Pose.Layers.push_back(L); }
	// Overlays go on top at full weight, fading out over their last 30%; what's under them gives way.
	for (const FOverlay& O : Overlays)
	{
		const float Length = Clip(O.Slot).Length;
		const float Left = Length > 0.0f ? 1.0f - O.Time / Length : 0.0f;
		const float W = Clamp01(Left / OverlayFadeShare);
		for (FRunlineBodyLayer& L : Pose.Layers) L.Weight *= 1.0f - W;
		Pose.Layers.push_back({O.Slot, O.Time, W});
	}
	Pose.Layers.erase(std::remove_if(Pose.Layers.begin(), Pose.Layers.end(), [](const FRunlineBodyLayer& L) { return L.Weight <= 1e-6f; }), Pose.Layers.end());
	float Total = 0.0f;
	for (const FRunlineBodyLayer& L : Pose.Layers) Total += L.Weight;
	if (Total > 0.0f) for (FRunlineBodyLayer& L : Pose.Layers) L.Weight /= Total;

	LastState = In.State;
	return Pose;
}
