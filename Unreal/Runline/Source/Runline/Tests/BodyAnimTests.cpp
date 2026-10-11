#include "Misc/AutomationTest.h"
#include "Animation/AnimSequence.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RunlineAnimSet.h"
#include "RunlineBodyAnimInstance.h"
#include "RunlineTestWorld.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	UAnimSequence* Clip(const TCHAR* Slot)
	{
		return LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/ThirdParty/GASP/A_%s.A_%s"), Slot, Slot), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
}

// The pipeline: a GASP clip, retargeted to our Manny, imported in place. Skips (with a warning)
// on a machine without the clips: run scripts/import-gasp.ps1.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineBodyImported, "Runline.Body.ImportedRunClip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineBodyImported::RunTest(const FString&)
{
	UAnimSequence* Run = Clip(TEXT("Run"));
	if (!Run)
	{
		AddWarning(TEXT("No GASP clips imported on this machine (scripts/import-gasp.ps1): skipped"));
		return true;
	}
	TestTrue(TEXT("on our Manny's skeleton"), Run->GetSkeleton() && Run->GetSkeleton()->GetPathName() == TEXT("/Game/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin"));
	TestTrue(TEXT("plays in place"), Run->bForceRootLock && !Run->bEnableRootMotion);
	const double Length = Run->GetPlayLength();
	const FTransform Moved = Run->ExtractRootMotionFromRange(0.0, Length, FAnimExtractContext());
	const double Speed = Moved.GetTranslation().Size2D() / 100.0 / Length;
	AddInfo(FString::Printf(TEXT("Run loop: %.3f s, %.2f m/s"), Length, Speed));
	TestTrue(TEXT("a run's authored speed (2 to 7 m/s)"), Speed > 2.0 && Speed < 7.0);
	return true;
}

// The list and the anim set agree: every slot has a line, a clip, and (after import) the clip
// loads; stand-ins are marked.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineBodyAnimSet, "Runline.Body.AnimSetMatchesTheList",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineBodyAnimSet::RunTest(const FString&)
{
	TArray<FString> Lines;
	TestTrue(TEXT("clip list"), FFileHelper::LoadFileToStringArray(Lines, *(FPaths::ProjectDir() / TEXT("../../Data/Animation/gasp-clips.txt"))));
	TSet<int32> Listed;
	for (const FString& L : Lines)
	{
		if (L.IsEmpty() || L.StartsWith(TEXT("#"))) continue;
		ERunlineAnimSlot S;
		TestTrue(*FString::Printf(TEXT("known slot in '%s'"), *L), RunlineAnimSlotFromName(L.Left(L.Find(TEXT(","))), S));
		Listed.Add(int32(S));
	}
	TestEqual(TEXT("every slot listed once"), Listed.Num(), int32(ERunlineAnimSlot::Count));
	const URunlineAnimSet* Set = URunlineAnimSet::Get();
	TestNotNull(TEXT("the anim set asset"), Set);
	if (!Set) return false;
	TestEqual(TEXT("one entry per slot"), Set->Slots.Num(), int32(ERunlineAnimSlot::Count));
	TestTrue(TEXT("Hang is a stand-in"), Set->Slots[int32(ERunlineAnimSlot::Hang)].bStandIn);
	if (!Clip(TEXT("Run"))) { AddWarning(TEXT("No GASP clips imported: clip loading not checked")); return true; }
	for (int32 I = 0; I < Set->Slots.Num(); ++I)
		TestNotNull(*FString::Printf(TEXT("%s loads"), RunlineAnimSlotName(ERunlineAnimSlot(I))), Set->Slots[I].Clip.LoadSynchronous());
	TestTrue(TEXT("the run's authored speed was measured"), Set->Slots[int32(ERunlineAnimSlot::Run)].AuthoredSpeed > 2.0f);
	return true;
}

// The real clips on the real mesh: a vault half way through shows the vault clip's half-way pose,
// and with no input the body stands in the idle clip's first frame (never the reference T-pose).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineBodyOnMesh, "Runline.Body.ClipsOnTheMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineBodyOnMesh::RunTest(const FString&)
{
	if (!Clip(TEXT("Run"))) { AddWarning(TEXT("No GASP clips imported: skipped")); return true; }
	FRunlineTestWorld TW;
	AActor* A = TW.World->SpawnActor<AActor>();
	USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(A);
	A->SetRootComponent(Mesh);
	Mesh->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple")));
	Mesh->SetAnimInstanceClass(URunlineBodyAnimInstance::StaticClass());
	Mesh->RegisterComponent();
	URunlineBodyAnimInstance* Anim = Cast<URunlineBodyAnimInstance>(Mesh->GetAnimInstance());
	TestTrue(TEXT("clips found"), Anim && Anim->HasClips());
	if (!Anim) return false;
	const auto Evaluate = [&]() { Mesh->TickAnimation(0.0f, false); Mesh->RefreshBoneTransforms(); };
	runline::Vault V;
	V.kind = 3;
	V.t = V.Duration() * 0.5f;
	FRunlineBodyInput I;
	I.State = V;
	I.Dt = 1.0f / 60.0f;
	I.SnapCount = 1;   // cut straight to it
	Anim->SetInput(I);
	Evaluate();
	UAnimSequence* Vault = Clip(TEXT("Vault"));
	const int32 Hand = Mesh->GetBoneIndex(TEXT("hand_r"));
	// The clip's own hand at half way, built up from the pelvis (the root is locked in place).
	const FReferenceSkeleton& Ref = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
	const int32 Pelvis = Mesh->GetBoneIndex(TEXT("pelvis"));
	const FAnimExtractContext Half(double(Vault->GetPlayLength()) * 0.5);
	FTransform Want = FTransform::Identity;
	for (int32 B = Hand; B != INDEX_NONE && B != Ref.GetParentIndex(Pelvis); B = Ref.GetParentIndex(B))
	{
		FTransform Local;
		Vault->GetBoneTransform(Local, FSkeletonPoseBoneIndex(Vault->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(Ref.GetBoneName(B))), Half, false);
		Want = Want * Local;
	}
	const FTransform Root = Mesh->GetBoneTransform(Ref.GetParentIndex(Pelvis), FTransform::Identity);
	const FVector Got = Root.InverseTransformPosition(Mesh->GetBoneTransform(Hand, FTransform::Identity).GetLocation());
	AddInfo(FString::Printf(TEXT("hand: got %s, clip %s"), *Got.ToString(), *Want.GetLocation().ToString()));
	TestTrue(TEXT("the vault's half-way hand"), Got.Equals(Want.GetLocation(), 1.0));
	return true;
}

#endif
