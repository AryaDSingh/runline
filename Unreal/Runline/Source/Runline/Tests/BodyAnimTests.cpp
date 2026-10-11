#include "Misc/AutomationTest.h"
#include "Animation/AnimSequence.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RunlineAnimSet.h"

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

#endif
