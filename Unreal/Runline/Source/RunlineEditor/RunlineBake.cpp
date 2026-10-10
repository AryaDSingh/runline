#include "RunlineBake.h"
#include "FileHelpers.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "RunlineCourseActors.h"
#include "RunlineCourseData.h"
#include "RunlineRunGameMode.h"
#include "RunlineUnits.h"

DEFINE_LOG_CATEGORY_STATIC(LogRunlineBake, Log, All);

UWorld* RunlineBake::BakeCourse(const FString& CourseName, const FString& MapPath)
{
	FRunlineCourseData Data;
	if (!FRunlineCourseData::Load(CourseName, Data)) return nullptr;
	UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
	if (!World)
	{
		UE_LOG(LogRunlineBake, Error, TEXT("Runline bake: couldn't make a new map"));
		return nullptr;
	}
	RunlineCourses::Spawn(World, Data, true);
	// The player start at checkpoint 0: the capsule's centre, half a standing height above the feet.
	const FRunlineCourseData::FCheckpointEntry& Start = Data.Checkpoints[0];
	const FVector At = RunlineUnits::ToUnrealPosition(Start.Spawn) + FVector(0.0, 0.0, 90.0);
	World->SpawnActor<APlayerStart>(At, RunlineUnits::ToUnrealRotator(Start.Yaw, 0.0f));
	World->GetWorldSettings()->DefaultGameMode = ARunlineRunGameMode::StaticClass();
	if (!UEditorLoadingAndSavingUtils::SaveMap(World, MapPath))
	{
		UE_LOG(LogRunlineBake, Error, TEXT("Runline bake: couldn't save %s"), *MapPath);
		return nullptr;
	}
	UE_LOG(LogRunlineBake, Display, TEXT("Runline bake: %s -> %s (%d boxes, %d checkpoints)"), *CourseName, *MapPath, Data.Boxes.Num(), Data.Checkpoints.Num());
	return World;
}
