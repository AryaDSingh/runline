#include "RunlineRunGameMode.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Kismet/GameplayStatics.h"
#include "RunlineCourse.h"
#include "RunlineMovementComponent.h"
#include "RunlineRunner.h"

DEFINE_LOG_CATEGORY_STATIC(LogRunlineGame, Log, All);

ARunlineRunGameMode::ARunlineRunGameMode()
{
	DefaultPawnClass = ARunlineRunner::StaticClass();
}

void ARunlineRunGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	const FString Name = UGameplayStatics::ParseOption(Options, TEXT("course"));
	if (!Name.IsEmpty()) CourseName = Name;
	ShotAfter = UGameplayStatics::GetIntOption(Options, TEXT("shot"), 0);
}

void ARunlineRunGameMode::StartPlay()
{
	Super::StartPlay();
	if (ShotAfter <= 0.0f) return;
	FTimerHandle Shot, Quit;
	GetWorldTimerManager().SetTimer(Shot, [this]
	{
		FScreenshotRequest::RequestScreenshot(false);
		if (const ARunlineRunner* R = Cast<ARunlineRunner>(UGameplayStatics::GetPlayerPawn(this, 0)))
		{
			const runline::Controller& C = R->Runline().Core();
			UE_LOG(LogRunlineGame, Display, TEXT("Runline shot: feet (%g, %g, %g) yaw %g state %hs, %lld steps, move input (%g, %g)"),
				C.feet.x, C.feet.y, C.feet.z, C.yaw, runline::StateName(C.state), R->Runline().StepCount(), R->Runline().GetMoveInput().X, R->Runline().GetMoveInput().Y);
		}
	}, ShotAfter, false);
	GetWorldTimerManager().SetTimer(Quit, [this] { GetWorld()->Exec(GetWorld(), TEXT("quit")); }, ShotAfter + 2.0f, false);
}

ARunlineCourse* ARunlineRunGameMode::GetCourse()
{
	if (!Course)
	{
		Course = GetWorld()->SpawnActor<ARunlineCourse>();
		if (Course) Course->Load(CourseName);
	}
	return Course;
}
