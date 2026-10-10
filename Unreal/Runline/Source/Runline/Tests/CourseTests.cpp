#include "Misc/AutomationTest.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "RunlineCourseActors.h"
#include "RunlineCourseData.h"
#include "RunlineRunGameMode.h"
#include "RunlineTestWorld.h"
#include "UnrealWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	TArray<ARunlineBox*> BoxesIn(UWorld* World)
	{
		TArray<ARunlineBox*> Out;
		for (TActorIterator<ARunlineBox> It(World); It; ++It) Out.Add(*It);
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineCourseLoads, "Runline.Course.LoadsBothLevels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineCourseLoads::RunTest(const FString&)
{
	for (const TCHAR* Name : {TEXT("greybox"), TEXT("rooftops")})
	{
		FRunlineCourseData Data;
		TestTrue(FString::Printf(TEXT("%s loads"), Name), FRunlineCourseData::Load(Name, Data));
		int32 Solids = 0;
		for (const auto& B : Data.Boxes) Solids += B.bSolid ? 1 : 0;
		TestTrue(TEXT("solids"), Solids > 10);
		TestTrue(TEXT("checkpoints"), Data.Checkpoints.Num() > 0);
		FRunlineTestWorld TW;
		RunlineCourses::Spawn(TW.World, Data, false);
		// Every box covers exactly its CSV box, with its level order.
		int32 Exact = 0;
		TArray<ARunlineBox*> Boxes = BoxesIn(TW.World);
		TestEqual(TEXT("one actor per box"), Boxes.Num(), Data.Boxes.Num());
		for (const ARunlineBox* A : Boxes)
		{
			if (!A->bSolid) continue;
			const auto& Want = Data.Boxes[A->Order];
			const runline::Aabb Got = A->CoreBox();
			Exact += (Want.bSolid && Got.min == Want.Box.min && Got.max == Want.Box.max) ? 1 : 0;
		}
		TestEqual(TEXT("solids exactly match the file, in order"), Exact, Solids);
		const TArray<ARunlineCheckpoint*> Cps = RunlineCourses::Checkpoints(TW.World);
		TestEqual(TEXT("checkpoints"), Cps.Num(), Data.Checkpoints.Num());
		TestEqual(TEXT("first checkpoint's name"), Cps[0]->CheckpointName, Data.Checkpoints[0].Name);
		TestTrue(TEXT("spawn round-trips"), Cps[0]->CoreSpawn() == Data.Checkpoints[0].Spawn);
		// She runs on it.
		const FUnrealWorld W(TW.World, nullptr);
		runline::Controller C(runline::Tuning{}, Cps[0]->CoreSpawn(), Cps[0]->CoreYaw());
		C.Respawn();
		runline::Input In;
		In.moveAxis = {0.0f, 1.0f};
		for (int32 I = 0; I < 60; ++I) C.Step(1.0f / 60.0f, In, W);
		TestTrue(FString::Printf(TEXT("%s: runs on the roof"), Name), C.feet.y > Cps[0]->CoreSpawn().y - 0.5f && C.feet.z < Cps[0]->CoreSpawn().z - 1.0f);
		TestEqual(TEXT("checkpoint at the start"), RunlineCourses::CheckpointAt(Cps, Cps[0]->CoreSpawn()), 0);
	}
	{
		FRunlineCourseData Data;
		FRunlineCourseData::Load(TEXT("rooftops"), Data);
		TestEqual(TEXT("rooftops starts at R1"), Data.Checkpoints[0].Name, FString(TEXT("R1 Start")));
		TestTrue(TEXT("rooftops has a finish"), Data.Finish.IsSet());
	}
	{
		AddExpectedError(TEXT("no level file"), EAutomationExpectedErrorFlags::Contains, 1);
		FRunlineCourseData Data;
		TestFalse(TEXT("a missing level fails cleanly"), FRunlineCourseData::Load(TEXT("no_such_level"), Data));
	}
	return true;
}

// Review Focus 1: a designer moves or resizes a box in the editor; the mesh follows, and the core
// collides with the new box.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineCourseBoxEdit, "Runline.Course.BoxMeshFollowsCollider",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineCourseBoxEdit::RunTest(const FString&)
{
	FRunlineTestWorld TW;
	ARunlineBox* A = TW.World->SpawnActor<ARunlineBox>();
	A->SetFromCore(runline::Aabb::New({-1.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f}));
	// An editor drag: bigger and moved.
	A->Box->SetBoxExtent(FVector(300.0, 200.0, 100.0));
	A->SetActorLocation(FVector(1000.0, 0.0, 50.0));
	A->RerunConstructionScripts();
	const FBox MeshBox = A->Mesh->Bounds.GetBox();
	const FBox ColliderBox = A->Box->Bounds.GetBox();
	TestTrue(TEXT("the mesh covers the collider"), MeshBox.Min.Equals(ColliderBox.Min, 0.01) && MeshBox.Max.Equals(ColliderBox.Max, 0.01));
	const FUnrealWorld W(TW.World, nullptr);
	TestTrue(TEXT("the core sees the moved box"), W.Overlaps(RunlineUnits::ToCoreBox(ColliderBox.ExpandBy(-1.0))));
	TestFalse(TEXT("and not the old one"), W.Overlaps(runline::Aabb::New({-0.5f, 0.2f, -0.5f}, {0.5f, 0.8f, 0.5f})));
	return true;
}

// Review Focus 2: a map with no checkpoints (the empty map) gets the CSV course; a map with its
// own course gets nothing extra.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineCourseFallback, "Runline.Course.SpawnsWhenTheMapHasNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineCourseFallback::RunTest(const FString&)
{
	{
		FRunlineTestWorld TW;
		ARunlineRunGameMode::EnsureCourse(TW.World, TEXT("greybox"));
		TestTrue(TEXT("spawned the CSV course"), RunlineCourses::Checkpoints(TW.World).Num() > 0);
	}
	{
		FRunlineTestWorld TW;
		ARunlineCheckpoint* Own = TW.World->SpawnActor<ARunlineCheckpoint>();
		Own->CheckpointName = TEXT("Designer start");
		ARunlineRunGameMode::EnsureCourse(TW.World, TEXT("greybox"));
		TestEqual(TEXT("left the map's own course alone"), RunlineCourses::Checkpoints(TW.World).Num(), 1);
		TestEqual(TEXT("no boxes added"), BoxesIn(TW.World).Num(), 0);
	}
	return true;
}

#endif
