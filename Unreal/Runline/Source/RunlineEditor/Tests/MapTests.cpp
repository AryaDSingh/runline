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
#include "RunlineTestRunner.h"
#include "UnrealWorld.h"
THIRD_PARTY_INCLUDES_START
#include "Replay.h"
THIRD_PARTY_INCLUDES_END
#include <fstream>

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
	// Rooftops first, then greybox over it: the file must end up greybox, not rooftops plus greybox.
	FRunlineCourseData Data;
	FRunlineCourseData::Load(TEXT("greybox"), Data);
	TestNotNull(TEXT("first bake"), RunlineBake::BakeCourse(TEXT("rooftops"), TestMap));
	TestNotNull(TEXT("second bake"), RunlineBake::BakeCourse(TEXT("greybox"), TestMap));
	UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(TestMap);
	TestNotNull(TEXT("loads"), World);
	if (World)
	{
		TestEqual(TEXT("greybox's boxes, once"), Count(World, ARunlineBox::StaticClass()), Data.Boxes.Num());
		const TArray<ARunlineCheckpoint*> Cps = RunlineCourses::Checkpoints(World);
		TestTrue(TEXT("greybox's checkpoints"), Cps.Num() == Data.Checkpoints.Num() && Cps[0]->CheckpointName == Data.Checkpoints[0].Name);
	}
	DeleteTestMap();
	return true;
}

// The baked Rooftops is faith-runner's Rooftops: its solid boxes, in level order, are exactly the
// boxes faith-runner's own Rooftops tests ran on, and every one of those tests replays bit-exact
// on the map.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineMapsRooftopsTraces, "Runline.Maps.RooftopsReplaysEveryTrace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineMapsRooftopsTraces::RunTest(const FString&)
{
	UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(TEXT("/Game/Runline/Maps/Rooftops"));
	TestNotNull(TEXT("the map loads"), World);
	if (!World) return false;
	TArray<ARunlineBox*> Solids;
	for (TActorIterator<ARunlineBox> It(World); It; ++It)
		if (It->bSolid) Solids.Add(*It);
	Solids.Sort([](const ARunlineBox& A, const ARunlineBox& B) { return A.Order < B.Order; });
	const FUnrealWorld W(World, nullptr);
	const FString Dir = FString(RUNLINE_GOLDEN_DIR) / TEXT("traces");
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Dir / TEXT("rooftops_tests__*.trace")), true, false);
	TestEqual(TEXT("faith-runner's Rooftops traces"), Files.Num(), 15);
	for (const FString& File : Files)
	{
		std::ifstream In(TCHAR_TO_UTF8(*(Dir / File)));
		const trace::Trace T = trace::Parse(In, TCHAR_TO_UTF8(*File));
		for (const trace::WorldDump& Wd : T.worlds)
		{
			bool bSame = int32(Wd.boxes.size()) == Solids.Num();
			for (int32 I = 0; bSame && I < Solids.Num(); ++I)
			{
				const runline::Aabb B = Solids[I]->CoreBox();
				bSame = B.min == Wd.boxes[I].min && B.max == Wd.boxes[I].max;
			}
			TestTrue(FString::Printf(TEXT("%s: the map's boxes are the trace's, in order"), *File), bSame);
		}
		const std::string R = replay::Replay(T, [&](std::size_t) -> const runline::World& { return W; });
		if (!R.empty()) AddError(FString::Printf(TEXT("%s: %hs"), *File, R.c_str()));
	}
	return true;
}

// Spec 4.4: a scripted route through the real movement component on the map. faith-runner's
// r1_rail_skylight_pipe_gap (rooftops_tests.rs): vault the rail, mantle the skylight, slide the
// pipe, jump the gap to R2.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineMapsRooftopsRoute, "Runline.Maps.RooftopsRouteThroughTheComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineMapsRooftopsRoute::RunTest(const FString&)
{
	UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(TEXT("/Game/Runline/Maps/Rooftops"));
	TestNotNull(TEXT("the map loads"), World);
	if (!World) return false;
	const TArray<ARunlineCheckpoint*> Cps = RunlineCourses::Checkpoints(World);
	TestTrue(TEXT("checkpoints"), Cps.Num() > 1);
	if (Cps.Num() < 2) return false;
	// rooftops.rs `mod z`.
	const float R1End = -28.0f, Rail = -6.0f, Skylight = -14.0f, Pipe = -22.0f, R2Start = -32.0f;
	ARunlineTestRunner* Runner = World->SpawnActor<ARunlineTestRunner>();
	URunlineMovementComponent& M = Runner->Runline();
	M.SpawnAt(Cps[0]->CoreSpawn(), Cps[0]->CoreYaw());
	TArray<runline::EventKind> Events;
	M.OnStep = [&Events](const runline::Controller& C, const runline::Input&) { for (const runline::Event& E : C.events) Events.Add(E.kind); };
	const auto Once = [](bool& Flag, bool bCond) { const bool bFire = bCond && !Flag; Flag |= bFire; return bFire; };
	bool bV = false, bM = false, bSl = false, bG = false;
	for (int32 F = 0; F < 600; ++F)
	{
		const runline::Controller& C = M.Core();
		const bool bGround = std::holds_alternative<runline::Ground>(C.state);
		if (C.feet.z < R2Start - 2.0f && bGround)
		{
			M.SetMove(FVector2D::ZeroVector);
			M.SetCrouchHeld(false);
		}
		else
		{
			const float Steer = FMath::Clamp((0.0f - C.feet.x) * 1.2f, -0.6f, 0.6f);   // toward_x(c, 0)
			M.SetMove(FVector2D(Steer, 1.0));
			bool bJump = Once(bV, C.feet.z < Rail + 1.0f);
			bJump |= Once(bM, C.feet.z < Skylight + 0.8f && C.feet.z > Skylight - 1.0f);
			if (Once(bSl, C.feet.z < Pipe + 2.6f && bGround)) M.PressCrouch();
			M.SetCrouchHeld(bSl && C.feet.z > Pipe - 0.8f);
			bJump |= Once(bG, C.feet.z < R1End + 0.5f && bGround);
			if (bJump) M.PressJump();
		}
		M.Advance(1.0f / 60.0f);
	}
	M.OnStep = nullptr;
	TestTrue(TEXT("vaulted"), Events.Contains(runline::EventKind::Vault));
	TestTrue(TEXT("slid"), Events.Contains(runline::EventKind::Slide));
	TestFalse(TEXT("no deaths"), Events.Contains(runline::EventKind::Death));
	const int32 At = RunlineCourses::CheckpointAt(Cps, M.Core().feet);
	TestTrue(FString::Printf(TEXT("reached R2 (at %g, %g, %g)"), M.Core().feet.x, M.Core().feet.y, M.Core().feet.z), At >= 0 && Cps[At]->CheckpointName == TEXT("R2 Billboards"));
	Runner->Destroy();
	return true;
}

#endif
