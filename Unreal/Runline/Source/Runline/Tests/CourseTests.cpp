#include "Misc/AutomationTest.h"
#include "Components/BoxComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RunlineCourse.h"
#include "RunlineTestWorld.h"
#include "UnrealWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/// The level file's solid boxes, straight from the CSV (core metres).
	TArray<runline::Aabb> SolidsInFile(const FString& Name)
	{
		TArray<FString> Lines;
		FFileHelper::LoadFileToStringArray(Lines, *(FPaths::ProjectDir() / TEXT("Data/Levels") / Name + TEXT(".csv")));
		TArray<runline::Aabb> Out;
		for (const FString& L : Lines)
		{
			TArray<FString> F;
			L.ParseIntoArray(F, TEXT(","), false);
			if (F.Num() == 8 && F[0] == TEXT("solid"))
				Out.Add({{FCString::Atof(*F[2]), FCString::Atof(*F[3]), FCString::Atof(*F[4])}, {FCString::Atof(*F[5]), FCString::Atof(*F[6]), FCString::Atof(*F[7])}});
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineCourseLoads, "Runline.Course.LoadsBothLevels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineCourseLoads::RunTest(const FString&)
{
	for (const TCHAR* Name : {TEXT("greybox"), TEXT("rooftops")})
	{
		FRunlineTestWorld TW;
		ARunlineCourse* Course = TW.World->SpawnActor<ARunlineCourse>();
		TestTrue(FString::Printf(TEXT("%s loads"), Name), Course->Load(Name));
		const TArray<runline::Aabb> Want = SolidsInFile(Name);
		TestTrue(TEXT("file has solids"), Want.Num() > 10);
		TestEqual(TEXT("one collider per solid"), Course->SolidCount(), Want.Num());
		TestTrue(TEXT("checkpoints"), Course->Checkpoints().Num() > 0);
		// Every collider covers exactly its CSV box, in level order.
		TArray<UBoxComponent*> Boxes;
		Course->GetComponents<UBoxComponent>(Boxes);
		int32 Exact = 0;
		for (int32 I = 0; I < Want.Num(); ++I)
		{
			for (const UBoxComponent* B : Boxes)
			{
				if (!B->ComponentHasTag(FName(*FString::Printf(TEXT("RunlineOrder%d"), I)))) continue;
				const FVector C = B->GetComponentLocation(), E = B->GetScaledBoxExtent();
				const runline::Aabb Got = RunlineUnits::ToCoreBox(FBox(C - E, C + E));
				Exact += (Got.min == Want[I].min && Got.max == Want[I].max) ? 1 : 0;
			}
		}
		TestEqual(TEXT("colliders exactly match the file"), Exact, Want.Num());
		// She runs on it: spawned at the first checkpoint, held forward for a second, she stays
		// on the roof.
		const ARunlineCourse::FCheckpoint& Start = Course->Checkpoints()[0];
		const FUnrealWorld W(TW.World, nullptr);
		runline::Controller C(runline::Tuning{}, Start.Spawn, Start.Yaw);
		C.Respawn();
		runline::Input In;
		In.moveAxis = {0.0f, 1.0f};
		for (int32 I = 0; I < 60; ++I) C.Step(1.0f / 60.0f, In, W);
		TestTrue(FString::Printf(TEXT("%s: runs on the roof"), Name), C.feet.y > Start.Spawn.y - 0.5f && C.feet.z < Start.Spawn.z - 1.0f);
		TestEqual(TEXT("checkpoint at the start"), Course->CheckpointAt(Start.Spawn), 0);
	}
	{
		FRunlineTestWorld TW;
		ARunlineCourse* Course = TW.World->SpawnActor<ARunlineCourse>();
		Course->Load(TEXT("rooftops"));
		TestEqual(TEXT("rooftops starts at R1"), Course->Checkpoints()[0].Name, FString(TEXT("R1 Start")));
	}
	{
		AddExpectedError(TEXT("no level file"), EAutomationExpectedErrorFlags::Contains, 1);
		FRunlineTestWorld TW;
		ARunlineCourse* Course = TW.World->SpawnActor<ARunlineCourse>();
		TestFalse(TEXT("a missing level fails cleanly"), Course->Load(TEXT("no_such_level")));
	}
	return true;
}

#endif
