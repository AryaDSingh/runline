#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RunlineRunGameMode.generated.h"


/// Plays a faith-runner course with the runner: `?game=/Script/Runline.RunlineRunGameMode?course=rooftops`
/// on any map (an empty one such as /Engine/Maps/Entry). The course defaults to greybox.
/// `?shot=<seconds>` takes a screenshot (Saved/Screenshots) that long after the start and quits:
/// for checking the look without anyone at the keyboard. `?menu=1` opens the pause menu at the start
/// (with ?shot, to check the menu's look). `?autorun=1` holds forward from the start (screenshots of
/// the run: bob, FOV, tilt).
UCLASS()
class ARunlineRunGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARunlineRunGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	/// Spawns the CSV course `CourseName` into World unless the map has its own (any checkpoint).
	static void EnsureCourse(UWorld* World, const FString& CourseName);
	virtual void StartPlay() override;

private:
	FString CourseName = TEXT("greybox");
	float ShotAfter = 0.0f;
	bool bMenuAtStart = false;
	bool bAutorun = false;
	/// ?pitch=<degrees>: look up (+) or down (-) at the start (screenshots of the arms and body).
	float StartPitchDeg = 0.0f;
};
