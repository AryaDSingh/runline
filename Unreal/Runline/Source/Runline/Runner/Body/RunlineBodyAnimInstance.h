#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "RunlineBodyChooser.h"
#include "RunlineBodyAnimInstance.generated.h"

class UAnimSequence;

/// Samples the chosen GASP clips at their times and blends them (worker thread), then leans the
/// spine for the wallrun stand-in. No anim graph.
USTRUCT()
struct FRunlineBodyProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

	FRunlineBodyProxy() = default;
	explicit FRunlineBodyProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	struct FLayer
	{
		const UAnimSequence* Seq = nullptr;
		float Time = 0.0f;
		float Weight = 0.0f;
	};
	TArray<FLayer, TInlineAllocator<8>> Layers;
	float Lean = 0.0f;
};

/// M3b: the runner's body, playing GASP clips by the core's state (FRunlineBodyChooser). The
/// runner gives it each frame's FRunlineBodyInput; the first-person arms and the legs copy its pose.
UCLASS(Transient, NotBlueprintable)
class RUNLINE_API URunlineBodyAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/// This frame's core side (game thread): runs the chooser.
	void SetInput(const FRunlineBodyInput& In);
	/// The anim set resolved, with at least the idle and run loops.
	bool HasClips() const;
	const FRunlineBodyChooser& GetChooser() const { return Chooser; }
	const FRunlineBodyPose& GetPose() const { return Pose; }

protected:
	virtual void NativeInitializeAnimation() override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FRunlineBodyProxy(this); }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override { delete static_cast<FRunlineBodyProxy*>(InProxy); }

private:
	void LoadClips();

	/// By slot; null where the clip is missing.
	UPROPERTY() TArray<TObjectPtr<UAnimSequence>> Sequences;
	TArray<FRunlineClipInfo> Infos;
	FRunlineBodyChooser Chooser;
	FRunlineBodyPose Pose;

	friend struct FRunlineBodyProxy;
};
