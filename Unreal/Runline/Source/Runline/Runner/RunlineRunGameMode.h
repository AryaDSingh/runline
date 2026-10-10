#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RunlineRunGameMode.generated.h"

class ARunlineCourse;

/// Plays a faith-runner course with the runner: `?game=/Script/Runline.RunlineRunGameMode?course=rooftops`
/// on any map (an empty one such as /Engine/Maps/Entry). The course defaults to greybox.
/// `?shot=<seconds>` takes a screenshot (Saved/Screenshots) that long after the start and quits:
/// for checking the look without anyone at the keyboard.
UCLASS()
class ARunlineRunGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARunlineRunGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;
	/// The course, built on first use.
	ARunlineCourse* GetCourse();

private:
	FString CourseName = TEXT("greybox");
	float ShotAfter = 0.0f;
	UPROPERTY() TObjectPtr<ARunlineCourse> Course;
};
