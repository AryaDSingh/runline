#include "Misc/AutomationTest.h"
#include "Animation/AnimSequence.h"

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

#endif
