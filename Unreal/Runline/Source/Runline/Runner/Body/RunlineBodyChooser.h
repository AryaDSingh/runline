#pragma once
#include "CoreMinimal.h"
#include "RunlineCoreBridge.h"
#include "RunlineAnimSet.h"

class URunlineMovementComponent;

/// What the chooser needs to know about one slot's clip.
struct FRunlineClipInfo
{
	float Length = 0.0f;
	/// Metres per second the loop was captured at (0: not a loop).
	float AuthoredSpeed = 0.0f;
	/// A held stand-in's place in its window (0..1).
	float HoldAt = 0.0f;
	/// The window that is the move (seconds; To 0: the clip's end).
	float From = 0.0f;
	float To = 0.0f;
	bool bLoaded = false;

	float Start() const { return FMath::Clamp(From, 0.0f, Length); }
	float End() const { return To > 0.0f ? FMath::Clamp(To, Start(), Length) : Length; }
	/// Where in the clip a move Fraction (0..1) of the way through is.
	float At(float Fraction) const { return Start() + FMath::Clamp(Fraction, 0.0f, 1.0f) * (End() - Start()); }
};

/// The core's side of one frame.
struct FRunlineBodyInput
{
	runline::State State = runline::Ground{};
	bool bCrouched = false;
	runline::Vec3 Vel{};
	/// The wall's normal while wallrunning.
	runline::Vec3 WallNormal{};
	/// Core yaw (radians): 0 faces -Z, positive turns left.
	float Yaw = 0.0f;
	runline::Tuning CoreTuning{};
	float StepSeconds = 1.0f / 60.0f;
	/// How far the render is past the last step (0..1).
	float StepAlpha = 0.0f;
	/// Game seconds since the last frame.
	float Dt = 0.0f;
	/// Every event of the steps run since the last frame.
	TArray<runline::Event> Events;
	/// URunlineMovementComponent::GetSnapCount: a change means the view was cut.
	int32 SnapCount = 0;

	static FRunlineBodyInput From(const URunlineMovementComponent& M, float Dt);
};

struct FRunlineBodyLayer
{
	ERunlineAnimSlot Slot = ERunlineAnimSlot::Idle;
	/// Seconds into the clip.
	float Time = 0.0f;
	float Weight = 0.0f;
};

/// The clips to blend this frame (weights sum to 1), and the wallrun stand-in's lean.
struct FRunlineBodyPose
{
	TArray<FRunlineBodyLayer, TInlineAllocator<8>> Layers;
	/// Degrees about her forward axis, positive to her right.
	float LeanDegrees = 0.0f;
	/// Hide the first-person arms: the hang stand-in brings the copied hands through the camera.
	bool bHideArms = false;
};

/// M3b: which GASP clips the body plays, where in them, and how much (spec 2026-10-11-m3b §4). The
/// core decides: a timed move's clip is positioned by the core's own timer, never by summed frame
/// times; locomotion loops follow her real speed. Plain C++: tested without any clips.
class RUNLINE_API FRunlineBodyChooser
{
public:
	/// The clip behind each slot (the anim instance gives the loaded GASP clips; tests give fakes).
	TFunction<FRunlineClipInfo(ERunlineAnimSlot)> Clips;

	FRunlineBodyPose Choose(const FRunlineBodyInput& In);
	/// Back to rest: no fades, overlays or air start carried over.
	void Reset();
	/// How many slots had no clip (each warned about once).
	int32 Warnings() const { return Warned.Num(); }

private:
	/// What a state plays: one clip at a time, or the locomotion pair (two loops at one phase).
	struct FMain
	{
		int32 Key = INDEX_NONE;
		bool bLocomotion = false;
		ERunlineAnimSlot Slot = ERunlineAnimSlot::Idle;
		float Time = 0.0f;
		ERunlineAnimSlot A = ERunlineAnimSlot::Idle, B = ERunlineAnimSlot::Idle;
		float BWeight = 0.0f;
		float Phase = 0.0f;
		float Weight = 1.0f;
		float FadeTime = 0.15f;
	};
	struct FOverlay
	{
		ERunlineAnimSlot Slot = ERunlineAnimSlot::Idle;
		float T = 0.0f;
	};

	FRunlineClipInfo Info(ERunlineAnimSlot Slot) const;
	bool Has(ERunlineAnimSlot Slot);
	FMain Locomotion(bool bCrouched, float Speed, float Dt);
	FMain MainFor(const FRunlineBodyInput& In, float Speed);
	void AddLayers(const FMain& M, float Scale, FRunlineBodyPose& Out) const;

	FMain Main;
	TArray<FMain, TInlineAllocator<4>> Fading;
	TOptional<FOverlay> Overlay;
	TOptional<FOverlay> AirStart;
	float FallTime = 0.0f;
	float LocoPhase = 0.0f;
	bool bWasSliding = false;
	TOptional<int32> LastSnap;
	TSet<ERunlineAnimSlot> Warned;
};
