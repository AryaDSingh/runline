#include "RunlineRunGameMode.h"
#include "Engine/World.h"
#include "Containers/Ticker.h"
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
	bMenuAtStart = UGameplayStatics::GetIntOption(Options, TEXT("menu"), 0) != 0;
}

void ARunlineRunGameMode::StartPlay()
{
	Super::StartPlay();
	if (bMenuAtStart)
	{
		FTimerHandle Open;
		GetWorldTimerManager().SetTimer(Open, [this]
		{
			if (ARunlineRunner* R = Cast<ARunlineRunner>(UGameplayStatics::GetPlayerPawn(this, 0))) R->ToggleMenu();
		}, 1.0f, false);
	}
	if (ShotAfter <= 0.0f) return;
	// Real time, not game time: game timers stop while the menu pauses the game.
	TWeakObjectPtr<ARunlineRunGameMode> Self(this);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self, Shot = ShotAfter, Elapsed = 0.0f, bShot = false](float Dt) mutable
	{
		ARunlineRunGameMode* Mode = Self.Get();
		if (!Mode) return false;
		Elapsed += Dt;
		if (!bShot && Elapsed >= Shot)
		{
			bShot = true;
			FScreenshotRequest::RequestScreenshot(true);   // with the UI (the menu)
			if (const ARunlineRunner* R = Cast<ARunlineRunner>(UGameplayStatics::GetPlayerPawn(Mode, 0)))
			{
				const runline::Controller& C = R->Runline().Core();
				UE_LOG(LogRunlineGame, Display, TEXT("Runline shot: feet (%g, %g, %g) yaw %g state %hs, %lld steps, move input (%g, %g)"),
					C.feet.x, C.feet.y, C.feet.z, C.yaw, runline::StateName(C.state), R->Runline().StepCount(), R->Runline().GetMoveInput().X, R->Runline().GetMoveInput().Y);
			}
		}
		if (bShot && Elapsed >= Shot + 2.0f)
		{
			Mode->GetWorld()->Exec(Mode->GetWorld(), TEXT("quit"));
			return false;
		}
		return true;
	}));
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
