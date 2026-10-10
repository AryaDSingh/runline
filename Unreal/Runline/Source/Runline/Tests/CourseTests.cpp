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

#if WITH_EDITOR   // editing actors (RerunConstructionScripts) is editor-only
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

#endif

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

// Review Important 4: a box or checkpoint a designer drags in sorts after the level's own (no
// clash with faith-runner's box 0 or the start checkpoint).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineCourseNewActors, "Runline.Course.NewActorsSortAfterTheLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineCourseNewActors::RunTest(const FString&)
{
	FRunlineTestWorld TW;
	FRunlineCourseData Data;
	FRunlineCourseData::Load(TEXT("greybox"), Data);
	RunlineCourses::Spawn(TW.World, Data, false);
	ARunlineCheckpoint* Added = TW.World->SpawnActor<ARunlineCheckpoint>();
	Added->CheckpointName = TEXT("New");
	const TArray<ARunlineCheckpoint*> Cps = RunlineCourses::Checkpoints(TW.World);
	TestEqual(TEXT("the start is still the start"), Cps[0]->CheckpointName, Data.Checkpoints[0].Name);
	TestTrue(TEXT("the new one comes last"), Cps.Last() == Added);
	ARunlineBox* Box = TW.World->SpawnActor<ARunlineBox>();
	Box->UpdateVisual();
	bool bHasOrderTag = false;
	for (const FName& Tag : Box->Box->ComponentTags) bHasOrderTag |= Tag.ToString().StartsWith(TEXT("RunlineOrder"));
	TestFalse(TEXT("a new box takes no level order (it sorts after the level's)"), bHasOrderTag);
	return true;
}

// Review Important 6: the CSV fallback only fills an empty map; a map with the designer's own boxes
// is left alone.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineCourseFallbackBoxes, "Runline.Course.FallbackLeavesDesignerBoxesAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineCourseFallbackBoxes::RunTest(const FString&)
{
	AddExpectedError(TEXT("has boxes but no checkpoints"), EAutomationExpectedErrorFlags::Contains, 1);
	FRunlineTestWorld TW;
	ARunlineBox* Mine = TW.World->SpawnActor<ARunlineBox>();
	Mine->SetFromCore(runline::Aabb::New({-5.0f, -1.0f, -5.0f}, {5.0f, 0.0f, 5.0f}));
	ARunlineRunGameMode::EnsureCourse(TW.World, TEXT("greybox"));
	TestEqual(TEXT("no course added on top"), BoxesIn(TW.World).Num(), 1);
	return true;
}

#if WITH_EDITOR
// Review Important 2-3: rotating or scaling a box, checkpoint or finish in the editor folds into the
// box (the core is axis-aligned): what's drawn is what the core collides with and where she spawns.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineCourseFold, "Runline.Course.RotationAndScaleFoldIntoTheBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineCourseFold::RunTest(const FString&)
{
	FRunlineTestWorld TW;
	ARunlineBox* A = TW.World->SpawnActor<ARunlineBox>();
	A->SetFromCore(runline::Aabb::New({-1.0f, 0.0f, -3.0f}, {1.0f, 1.0f, 3.0f}));
	A->SetActorRotation(FRotator(0.0, 90.0, 0.0));
	A->SetActorScale3D(FVector(1.0, 2.0, -1.0));
	const FBox Before = A->Box->Bounds.GetBox();   // what the editor draws for the turned, scaled box
	A->RerunConstructionScripts();
	TestTrue(TEXT("box: no rotation left"), A->GetActorRotation().IsNearlyZero(1e-6));
	TestTrue(TEXT("box: no scale left"), A->GetActorScale3D().Equals(FVector::OneVector, 1e-9));
	TestTrue(TEXT("box: same space as before the fold"), A->Box->Bounds.GetBox().Equals(Before, 0.01));
	const FBox Drawn = A->Mesh->Bounds.GetBox();
	const FUnrealWorld W(TW.World, nullptr);
	TestTrue(TEXT("box: the core collides with what's drawn"), W.Overlaps(RunlineUnits::ToCoreBox(Drawn.ExpandBy(-1.0))));
	TestTrue(TEXT("box: the core's box is what's drawn"), RunlineUnits::ToUnrealBox(A->CoreBox()).Equals(Drawn, 0.01));
	ARunlineCheckpoint* C = TW.World->SpawnActor<ARunlineCheckpoint>();
	C->Region->SetBoxExtent(FVector(100.0, 300.0, 100.0));
	C->SpawnPoint = FVector(200.0, 0.0, 0.0);
	C->SetActorLocation(FVector(1000.0, 0.0, 0.0));
	C->SetActorRotation(FRotator(0.0, 90.0, 0.0));
	const FVector TurnedSpawn = C->GetActorTransform().TransformPosition(C->SpawnPoint);
	const FBox TurnedRegion = C->Region->Bounds.GetBox();
	C->RerunConstructionScripts();
	TestTrue(TEXT("checkpoint: no rotation left"), C->GetActorRotation().IsNearlyZero(1e-6));
	TestTrue(TEXT("checkpoint: spawn stays where it was drawn"), RunlineUnits::ToUnrealPosition(C->CoreSpawn()).Equals(TurnedSpawn, 0.01));
	TestTrue(TEXT("checkpoint: region stays where it was drawn"), RunlineUnits::ToUnrealBox(C->CoreRegion()).Equals(TurnedRegion, 0.01));
	TestEqual(TEXT("checkpoint: the turn carried into the spawn yaw"), C->SpawnYawDegrees, 90.0f, 1e-3f);
	return true;
}
#endif

#endif
