#include "RunlineRunGameMode.h"
#include "Engine/World.h"
#include "Containers/Ticker.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Kismet/GameplayStatics.h"
#include "RunlineCourseActors.h"
#include "RunlineCourseData.h"
#include "EngineUtils.h"
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
	bAutorun = UGameplayStatics::GetIntOption(Options, TEXT("autorun"), 0) != 0;
	StartPitchDeg = float(UGameplayStatics::GetIntOption(Options, TEXT("pitch"), 0));
}

void ARunlineRunGameMode::StartPlay()
{
	EnsureCourse(GetWorld(), CourseName);   // before the runner's BeginPlay looks for checkpoints
	Super::StartPlay();
	if (bMenuAtStart)
	{
		FTimerHandle Open;
		GetWorldTimerManager().SetTimer(Open, [this]
		{
			if (ARunlineRunner* R = Cast<ARunlineRunner>(UGameplayStatics::GetPlayerPawn(this, 0))) R->ToggleMenu();
		}, 1.0f, false);
	}
	if (StartPitchDeg != 0.0f)
	{
		FTimerHandle Look;
		GetWorldTimerManager().SetTimer(Look, [this]
		{
			if (ARunlineRunner* R = Cast<ARunlineRunner>(UGameplayStatics::GetPlayerPawn(this, 0))) R->Runline().AddLook(FVector2D(0.0, FMath::DegreesToRadians(StartPitchDeg)));
		}, 0.5f, false);
	}
	if (bAutorun)
	{
		FTimerHandle Run;
		GetWorldTimerManager().SetTimer(Run, [this]
		{
			if (ARunlineRunner* R = Cast<ARunlineRunner>(UGameplayStatics::GetPlayerPawn(this, 0))) R->Runline().SetMove(FVector2D(0.0, 1.0));
		}, 0.5f, false);
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
			FPlatformMisc::RequestExit(false);
			return false;
		}
		return true;
	}));
}

void ARunlineRunGameMode::EnsureCourse(UWorld* World, const FString& CourseName)
{
	if (RunlineCourses::Checkpoints(World).Num() > 0) return;   // the map has its own course
	if (TActorIterator<ARunlineBox>(World))
	{
		// A map being built: don't drop the CSV course on top of the designer's boxes.
		UE_LOG(LogRunlineGame, Error, TEXT("Runline: %s has boxes but no checkpoints; add an ARunlineCheckpoint (Index 0) to start the runner on it"), *World->GetMapName());
		return;
	}
	FRunlineCourseData Data;
	if (FRunlineCourseData::Load(CourseName, Data)) RunlineCourses::Spawn(World, Data, true);
}
