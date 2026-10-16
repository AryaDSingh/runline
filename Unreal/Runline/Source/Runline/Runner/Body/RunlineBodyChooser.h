#pragma once
// Which clips the body plays, where in them, and how much (spec 2026-10-11 M3b, §4): the core's
// state, timers and events in, weighted clip layers out. All the timing rules live here, testable
// with no clips: the anim instance (Task 5) only samples and blends what this returns.
//
// Plain C++ (standard types, no UObject), so its doctest suite (RunlineCore/tests/
// body_chooser_tests.cpp) builds and runs with the core's, on every platform CI covers. The two
// Unreal-facing pieces stay outside: filling an FRunlineBodyInput from the movement component, and
// logging (set OnMissingClip to a UE_LOG).
#include "RunlineAnimSlot.h"

#include <cstdint>
#include <functional>
#include <set>
#include <vector>

#if defined(RUNLINE_STANDALONE)
#include "runline/Controller.h"
#else
#include "RunlineCoreBridge.h"
#endif

/// What the anim set knows about a slot's clip.
struct FRunlineClipInfo
{
	float Length = 0.0f;
	/// Loops: the speed (m/s) the clip was authored at; the cycle plays at speed / this.
	float AuthoredSpeed = 0.0f;
	/// Held stand-ins (Hang): where to hold, as a fraction of the clip.
	float HoldAt = 0.0f;
	bool bLoaded = false;
};

/// One frame of the core, as the body sees it.
struct FRunlineBodyInput
{
	runline::State State = runline::Ground{};
	bool bCrouched = false;
	runline::Vec3 Vel{};
	/// The core's yaw: 0 faces -Z, positive turns left.
	float Yaw = 0.0f;
	runline::Tuning CoreTuning{};
	/// The core's fixed step and how far the render is into the next one: every core timer is read
	/// StepAlpha * StepSeconds later, as the camera is.
	float StepSeconds = 1.0f / 60.0f;
	float StepAlpha = 0.0f;
	/// Render time since the last call (scaled by game speed; 0 when paused).
	float Dt = 1.0f / 60.0f;
	/// The core's events since the last call.
	std::vector<runline::Event> Events;
	/// The movement component's snap counter: a change (respawn, death) cuts with no blend.
	std::int32_t SnapCount = 0;
};

struct FRunlineBodyLayer
{
	ERunlineAnimSlot Slot = ERunlineAnimSlot::Idle;
	/// Seconds into the clip.
	float Time = 0.0f;
	float Weight = 0.0f;
};

struct FRunlineBodyPose
{
	/// Weights sum to 1 (or the list is empty: no loaded clip to play).
	std::vector<FRunlineBodyLayer> Layers;
	/// Wallrun stand-in: the body leans this far, positive to her right.
	float LeanDegrees = 0.0f;
};

class FRunlineBodyChooser
{
public:
	/// The anim set's answer for a slot. Unset means nothing is loaded.
	std::function<FRunlineClipInfo(ERunlineAnimSlot)> Clips;
	/// Called once per slot the first time it's wanted without a loaded clip.
	std::function<void(ERunlineAnimSlot)> OnMissingClip;

	FRunlineBodyPose Choose(const FRunlineBodyInput& In);
	/// Forget everything: the next pose starts fresh, with no blend.
	void Reset();
	/// How many slots have been missing so far (one per slot).
	int Warnings() const { return static_cast<int>(Warned.size()); }

	/// Cross-fade times between main clips (spec §4): quicker into a slide or a roll.
	static constexpr float FadeTime = 0.15f;
	static constexpr float QuickFadeTime = 0.08f;
	/// Seconds of a slide spent on SlideInto before SlideLoop.
	static constexpr float SlideIntoTime = 0.25f;
	/// Jump starts and landings by speed: below Stand, below Run, else Sprint.
	static constexpr float StandSpeed = 1.5f;
	static constexpr float RunSpeed = 5.5f;
	/// A landing at more than this downward speed (m/s) is Heavy.
	static constexpr float HeavyImpact = 7.0f;
	/// Overlays fade out over the last 30% of the clip.
	static constexpr float OverlayFadeShare = 0.3f;
	/// The locomotion cycle's rate, held to this range.
	static constexpr float MinRate = 0.7f;
	static constexpr float MaxRate = 1.4f;
	static constexpr float WallRunLean = 20.0f;

private:
	/// What the main clip is: a locomotion blend (standing or crouched), or one slot.
	enum class EMainKind : std::uint8_t { Locomotion, CrouchLocomotion, Slot };
	struct FMain
	{
		EMainKind Kind = EMainKind::Locomotion;
		ERunlineAnimSlot Slot = ERunlineAnimSlot::Idle;
		/// Slot: seconds into the clip.
		float Time = 0.0f;
		/// Which state chose it: a new state is a new main even on the same slot.
		std::size_t StateIndex = 0;
		bool SameAs(const FMain& O) const { return Kind == O.Kind && Slot == O.Slot && StateIndex == O.StateIndex; }
	};
	struct FFading
	{
		FMain Main;
		float Weight = 1.0f;
		/// Weight lost per second.
		float Rate = 1.0f;
	};
	struct FOverlay
	{
		ERunlineAnimSlot Slot = ERunlineAnimSlot::LandStandLight;
		float Time = 0.0f;
	};
	struct FAirStart
	{
		ERunlineAnimSlot Slot = ERunlineAnimSlot::JumpRun;
		float Time = 0.0f;
	};

	FRunlineClipInfo Clip(ERunlineAnimSlot Slot) const;
	bool Loaded(ERunlineAnimSlot Slot);
	FMain MainFor(const FRunlineBodyInput& In);
	/// A slot main, or the locomotion loop when its clip is missing.
	FMain SlotMain(const FRunlineBodyInput& In, ERunlineAnimSlot Slot, float Time);
	/// The locomotion loops for a speed: up to two, with weights summing to 1.
	std::vector<FRunlineBodyLayer> LocomotionLayers(bool bCrouched, float Speed);
	std::vector<FRunlineBodyLayer> LayersOf(const FMain& Main, float Speed);
	void StepLocomotion(const FRunlineBodyInput& In, float Speed);
	void Events(const FRunlineBodyInput& In, float Speed);

	bool bStarted = false;
	std::int32_t LastSnapCount = 0;
	runline::State LastState = runline::Ground{};
	FMain Main;
	std::vector<FFading> Fading;
	std::vector<FOverlay> Overlays;
	bool bHasAirStart = false;
	FAirStart AirStart;
	/// The locomotion cycle's shared phase, 0 to 1.
	float Phase = 0.0f;
	/// Seconds in the air (the fall loop's clock).
	float FallTime = 0.0f;
	float Lean = 0.0f;
	std::set<ERunlineAnimSlot> Warned;
};
