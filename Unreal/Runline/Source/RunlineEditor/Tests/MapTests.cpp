#include "Misc/AutomationTest.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "RunlineBake.h"
#include "RunlineCourseActors.h"
#include "RunlineCourseData.h"
#include "RunlineRunGameMode.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	const TCHAR* TestMap = TEXT("/Game/Runline/Maps/Test_Greybox");

	/// Leaves the test map behind: a fresh blank editor map, then the file deleted.
	void DeleteTestMap()
	{
		UEditorLoadingAndSavingUtils::NewBlankMap(false);
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		IFileManager::Get().Delete(*FPackageName::LongPackageNameToFilename(TestMap, FPackageName::GetMapPackageExtension()), false, true, true);
	}

	int32 Count(UWorld* World, UClass* Class)
	{
		int32 N = 0;
		for (TActorIterator<AActor> It(World, Class); It; ++It) ++N;
		return N;
	}
}

// The bake makes a map you can open and play: every box, the checkpoints in order, a player start
// and Runline's game mode.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineBakeMap, "Runline.Bake.MakesAPlayableMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineBakeMap::RunTest(const FString&)
{
	FRunlineCourseData Data;
	TestTrue(TEXT("greybox data"), FRunlineCourseData::Load(TEXT("greybox"), Data));
	TestNotNull(TEXT("baked"), RunlineBake::BakeCourse(TEXT("greybox"), TestMap));
	UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(TestMap);
	TestNotNull(TEXT("loads"), World);
	if (World)
	{
		TestEqual(TEXT("one actor per box"), Count(World, ARunlineBox::StaticClass()), Data.Boxes.Num());
		TestEqual(TEXT("one player start"), Count(World, APlayerStart::StaticClass()), 1);
		const TArray<ARunlineCheckpoint*> Cps = RunlineCourses::Checkpoints(World);
		TestEqual(TEXT("checkpoints"), Cps.Num(), Data.Checkpoints.Num());
		for (int32 I = 0; I < Cps.Num() && I < Data.Checkpoints.Num(); ++I)
			TestEqual(TEXT("checkpoint name, in order"), Cps[I]->CheckpointName, Data.Checkpoints[I].Name);
		TestTrue(TEXT("Runline's game mode"), World->GetWorldSettings()->DefaultGameMode == ARunlineRunGameMode::StaticClass());
		TestNotNull(TEXT("a finish"), RunlineCourses::Finish(World));
	}
	DeleteTestMap();
	return true;
}

// Review Focus 3: baking again over the same map replaces it (no doubled actors).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineBakeTwice, "Runline.Bake.RebakeReplaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineBakeTwice::RunTest(const FString&)
{
	FRunlineCourseData Data;
	FRunlineCourseData::Load(TEXT("greybox"), Data);
	RunlineBake::BakeCourse(TEXT("greybox"), TestMap);
	RunlineBake::BakeCourse(TEXT("greybox"), TestMap);
	UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(TestMap);
	TestNotNull(TEXT("loads"), World);
	if (World) TestEqual(TEXT("boxes once"), Count(World, ARunlineBox::StaticClass()), Data.Boxes.Num());
	DeleteTestMap();
	return true;
}

#endif
