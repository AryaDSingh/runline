#include "RunlineBodyAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"
#include "BonePose.h"

void URunlineBodyAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	LoadClips();
}

void URunlineBodyAnimInstance::LoadClips()
{
	if (Infos.Num() > 0) return;
	const int32 Count = int32(ERunlineAnimSlot::Count);
	Sequences.SetNum(Count);
	Infos.SetNum(Count);
	Anchors.Init(false, Count);
	if (const URunlineAnimSet* Set = URunlineAnimSet::Get())
	{
		for (int32 I = 0; I < FMath::Min(Count, Set->Slots.Num()); ++I)
		{
			const FRunlineAnimSlotDef& Def = Set->Slots[I];
			UAnimSequence* Seq = RunlineLoadClip(Def.Clip);
			Sequences[I] = Seq;
			FRunlineClipInfo& Info = Infos[I];
			Info.Length = Seq ? Seq->GetPlayLength() : 0.0f;
			Info.AuthoredSpeed = Def.AuthoredSpeed;
			Info.HoldAt = Def.HoldAt;
			Info.From = Def.From;
			Info.To = Def.To;
			Info.bLoaded = Seq != nullptr;
			Anchors[I] = Def.bAnchorPelvis;
		}
	}
	Chooser.Clips = [this](ERunlineAnimSlot Slot) { return Infos.IsValidIndex(int32(Slot)) ? Infos[int32(Slot)] : FRunlineClipInfo{}; };
	// Before the first input: standing in the idle loop (never the reference pose).
	if (Infos[int32(ERunlineAnimSlot::Idle)].bLoaded) Pose.Layers = {{ERunlineAnimSlot::Idle, 0.0f, 1.0f}};
}

bool URunlineBodyAnimInstance::HasClips() const
{
	return Infos.IsValidIndex(int32(ERunlineAnimSlot::Run)) && Infos[int32(ERunlineAnimSlot::Idle)].bLoaded && Infos[int32(ERunlineAnimSlot::Run)].bLoaded;
}

void URunlineBodyAnimInstance::SetInput(const FRunlineBodyInput& In)
{
	LoadClips();
	Pose = Chooser.Choose(In);
	if (DevOverride)
	{
		Pose.Layers = {*DevOverride};
		Pose.LeanDegrees = 0.0f;
		Pose.bHideArms = false;
	}
}

void FRunlineBodyProxy::PreUpdate(UAnimInstance* Instance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(Instance, DeltaSeconds);
	const URunlineBodyAnimInstance* A = CastChecked<URunlineBodyAnimInstance>(Instance);
	Layers.Reset();
	for (const FRunlineBodyLayer& L : A->Pose.Layers)
		if (A->Sequences.IsValidIndex(int32(L.Slot)) && A->Sequences[int32(L.Slot)] && L.Weight > 0.0f)
			Layers.Add({A->Sequences[int32(L.Slot)], L.Time, L.Weight, A->Anchors.IsValidIndex(int32(L.Slot)) && A->Anchors[int32(L.Slot)]});
	Lean = A->Pose.LeanDegrees;
}

namespace
{
	/// An anchored clip: its pelvis back at the reference (standing) height.
	void AnchorPelvis(FCompactPose& Pose)
	{
		const FBoneContainer& Bones = Pose.GetBoneContainer();
		const int32 Mesh = Bones.GetPoseBoneIndexForBoneName(TEXT("pelvis"));
		if (Mesh == INDEX_NONE) return;
		const FCompactPoseBoneIndex Pelvis = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Mesh));
		FVector T = Pose[Pelvis].GetTranslation();
		T.Z = Bones.GetRefPoseTransform(Pelvis).GetTranslation().Z;
		Pose[Pelvis].SetTranslation(T);
	}
}

bool FRunlineBodyProxy::Evaluate(FPoseContext& Output)
{
	if (Layers.Num() == 0)
	{
		Output.ResetToRefPose();
		return true;
	}
	if (Layers.Num() == 1)
	{
		FAnimationPoseData Data(Output);
		Layers[0].Seq->GetAnimationPose(Data, FAnimExtractContext(double(Layers[0].Time)));
		if (Layers[0].bAnchor) AnchorPelvis(Output.Pose);
	}
	else
	{
		const int32 N = Layers.Num();
		TArray<FCompactPose, TInlineAllocator<8>> Poses;
		TArray<FBlendedCurve, TInlineAllocator<8>> Curves;
		TArray<UE::Anim::FStackAttributeContainer, TInlineAllocator<8>> Attributes;
		TArray<float, TInlineAllocator<8>> Weights;
		Poses.SetNum(N);
		Curves.SetNum(N);
		Attributes.SetNum(N);
		for (int32 I = 0; I < N; ++I)
		{
			Poses[I].SetBoneContainer(&Output.Pose.GetBoneContainer());
			Curves[I].InitFrom(Output.Curve);
			FAnimationPoseData Data(Poses[I], Curves[I], Attributes[I]);
			Layers[I].Seq->GetAnimationPose(Data, FAnimExtractContext(double(Layers[I].Time)));
			if (Layers[I].bAnchor) AnchorPelvis(Poses[I]);
			Weights.Add(Layers[I].Weight);
		}
		FAnimationPoseData Out(Output);
		FAnimationRuntime::BlendPosesTogether(Poses, Curves, Attributes, Weights, Out);
	}
	if (Lean != 0.0f)
	{
		// The wallrun stand-in: the spine leans about her forward axis (Manny faces +Y), half at
		// spine_01, half at spine_03.
		const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
		FCSPose<FCompactPose> CS;
		CS.InitPose(Output.Pose);
		const FQuat Half(FVector(0.0, 1.0, 0.0), FMath::DegreesToRadians(Lean) * 0.5);
		for (const TCHAR* Name : {TEXT("spine_01"), TEXT("spine_03")})
		{
			const int32 Mesh = Bones.GetPoseBoneIndexForBoneName(FName(Name));
			if (Mesh == INDEX_NONE) continue;
			const FCompactPoseBoneIndex Bone = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Mesh));
			FTransform T = CS.GetComponentSpaceTransform(Bone);
			T.SetRotation(Half * T.GetRotation());
			const FBoneTransform Set[1] = {FBoneTransform(Bone, T)};
			CS.SafeSetCSBoneTransforms(Set);
		}
		FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(CS), Output.Pose);
	}
	return true;
}
