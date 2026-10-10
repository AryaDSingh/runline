#include "Misc/AutomationTest.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "RunlineRunner.h"
#include "RunlineTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

// The runner carries the template's Manny: a body running ABP_Unarmed (hidden from her) and arms
// running ABP_FP_Copy attached to it (the copy-pose node reads the attached parent).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineRunnerArms, "Runline.Runner.ArmsAndBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineRunnerArms::RunTest(const FString&)
{
	FRunlineTestWorld TW;
	ARunlineRunner* R = TW.World->SpawnActor<ARunlineRunner>();
	TestNotNull(TEXT("runner"), R);
	if (!R) return false;
	TArray<USkeletalMeshComponent*> Meshes;
	R->GetComponents<USkeletalMeshComponent>(Meshes);
	USkeletalMeshComponent* Arms = nullptr;
	for (USkeletalMeshComponent* M : Meshes)
		if (M != R->GetMesh()) Arms = M;
	TestNotNull(TEXT("arms"), Arms);
	if (!Arms) return false;
	TestNotNull(TEXT("body mesh loaded"), R->GetMesh()->GetSkeletalMeshAsset());
	TestNotNull(TEXT("arms mesh loaded"), Arms->GetSkeletalMeshAsset());
	TestTrue(TEXT("body runs ABP_Unarmed"), R->GetMesh()->GetAnimClass() && R->GetMesh()->GetAnimClass()->GetName().Contains(TEXT("ABP_Unarmed")));
	TestTrue(TEXT("arms run ABP_FP_Copy"), Arms->GetAnimClass() && Arms->GetAnimClass()->GetName().Contains(TEXT("ABP_FP_Copy")));
	TestTrue(TEXT("arms hang off the body"), Arms->GetAttachParent() == R->GetMesh());
	TestTrue(TEXT("she doesn't see the body"), R->GetMesh()->bOwnerNoSee);
	TestTrue(TEXT("only she sees the arms"), Arms->bOnlyOwnerSee);
	return true;
}

#endif
