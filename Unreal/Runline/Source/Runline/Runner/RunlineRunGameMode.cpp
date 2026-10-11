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
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"
#include "RunlineBodyAnimInstance.h"

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
	bPoses = UGameplayStatics::GetIntOption(Options, TEXT("poses"), 0) != 0;
	ScanSlots = UGameplayStatics::ParseOption(Options, TEXT("scan"));
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
	if (bPoses) StartPoses();
	if (!ScanSlots.IsEmpty()) StartScan();
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

namespace
{
	struct FPoseSample
	{
		const TCHAR* Name;
		runline::State State;
		float Speed = 0.0f;
		bool bCrouched = false;
		float FallSpeed = 0.0f;
		TOptional<float> LandImpact;
		/// Look pitch (radians; negative looks down).
		float Pitch = 0.0f;
	};

	/// One sample per core state (spec M3b §6.4), built around where she stands, facing her yaw.
	TArray<FPoseSample> PoseSamples(const runline::Controller& C)
	{
		const runline::Tuning& Tu = C.tuning;
		const runline::Vec3 F{-FMath::Sin(C.yaw), 0.0f, -FMath::Cos(C.yaw)};   // her forward (core)
		const runline::Vec3 Right{FMath::Cos(C.yaw), 0.0f, -FMath::Sin(C.yaw)};
		const runline::Vec3 Feet = C.feet;
		const runline::Vec3 Facing{-F.x, 0.0f, -F.z};   // a wall in front of her faces back at her
		const runline::Vec3 Up{0.0f, 1.0f, 0.0f};
		const auto VaultAt = [&](std::size_t Kind)
		{
			runline::Vault V;
			V.kind = Kind;
			V.from = Feet;
			V.hand = Feet + F * 0.6f;
			V.over = Feet + F * 0.9f;
			V.end = Feet + F * 1.6f;
			V.t = V.Duration() * 0.5f;
			return V;
		};
		const auto Trav = [&](runline::TraverseKind K, float Dur)
		{
			runline::Traverse T;
			T.kind = K;
			T.from = Feet;
			T.to = Feet + F * 0.5f + Up;
			T.dur = Dur;
			T.t = Dur * 0.5f;
			return T;
		};
		runline::LayOnGround Lying;
		Lying.t = 0.5f;
		runline::LayOnGround GettingUp = Lying;
		GettingUp.gettingUp = Tu.layOnGroundGetUpTime * 0.5f;
		TArray<FPoseSample> S;
		S.Add({TEXT("idle"), runline::Ground{}});
		S.Add({TEXT("run"), runline::Ground{}, 4.0f});
		S.Add({TEXT("sprint"), runline::Ground{}, 7.0f});
		S.Add({TEXT("crouch"), runline::Ground{}, 1.0f, true});
		S.Add({TEXT("air"), runline::Air{}, 4.0f, false, 5.0f});
		S.Add({TEXT("land_heavy"), runline::Ground{}, 0.5f, false, 0.0f, 9.0f});
		S.Add({TEXT("stunned"), runline::Stunned{Tu.hardLandStun * 0.3f}});
		S.Add({TEXT("roll"), runline::Roll{Tu.rollTime * 0.5f}, 5.0f});
		S.Add({TEXT("slide_into"), runline::Slide{0.1f}, 6.0f, true});
		S.Add({TEXT("slide_loop"), runline::Slide{0.6f}, 5.0f, true});
		S.Add({TEXT("vault_over"), VaultAt(3), 5.0f});
		S.Add({TEXT("vault_onto"), VaultAt(2), 5.0f});
		S.Add({TEXT("step_up"), VaultAt(1), 3.0f});
		S.Add({TEXT("pull_up"), Trav(runline::TraverseKind::PullUp, Tu.pullupTime)});
		S.Add({TEXT("springboard"), Trav(runline::TraverseKind::SpringBoard, Tu.springboardStepTime), 5.0f});
		S.Add({TEXT("wallrun_wall_left"), runline::WallRun{Right, 0.3f}, 6.0f});
		S.Add({TEXT("wallrun_wall_right"), runline::WallRun{Right * -1.0f, 0.3f}, 6.0f});
		S.Add({TEXT("wallclimb"), runline::WallClimb{Facing, 0.3f}});
		S.Add({TEXT("ledge_hang"), runline::LedgeHang{Facing, Feet.y + Tu.hangHandsAboveFeet, false}});
		S.Add({TEXT("grab_transfer"), runline::GrabTransfer{0.2f, Feet, Feet + Up, 0.6f, Facing, Feet.y + Tu.hangHandsAboveFeet + 1.0f}});
		S.Add({TEXT("lying"), Lying});
		S.Add({TEXT("getting_up"), GettingUp});
		S.Add({TEXT("run_looking_down"), runline::Ground{}, 4.0f, false, 0.0f, {}, -1.05f});
		S.Add({TEXT("slide_looking_down"), runline::Slide{0.6f}, 5.0f, true, 0.0f, {}, -1.05f});
		return S;
	}
}

void ARunlineRunGameMode::StartPoses()
{
	// Real time on the core ticker: 3 s for shaders, then per sample: set it, let the blends settle,
	// her view, then the side view.
	TWeakObjectPtr<ARunlineRunGameMode> Self(this);
	const FString Dir = FPaths::ScreenShotDir() / TEXT("Poses");
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[Self, Dir, Elapsed = 0.0f, Index = -1, Phase = 0, Samples = TArray<FPoseSample>(), Side = TWeakObjectPtr<ACameraActor>()](float Dt) mutable
	{
		ARunlineRunGameMode* Mode = Self.Get();
		if (!Mode) return false;
		ARunlineRunner* R = Cast<ARunlineRunner>(UGameplayStatics::GetPlayerPawn(Mode, 0));
		APlayerController* Pc = UGameplayStatics::GetPlayerController(Mode, 0);
		if (!R || !Pc) return true;
		Elapsed += Dt;
		if (Index < 0)
		{
			if (Elapsed < 3.0f) return true;
			Samples = PoseSamples(R->Runline().Core());
			R->Runline().bFrozen = true;
			Side = Mode->GetWorld()->SpawnActor<ACameraActor>();
			Index = 0;
			Phase = 0;
			Elapsed = 0.0f;
		}
		if (Index >= Samples.Num())
		{
			if (Elapsed > 1.0f)
			{
				FPlatformMisc::RequestExit(false);
				return false;
			}
			return true;
		}
		const FPoseSample& S = Samples[Index];
		const FString Base = Dir / FString::Printf(TEXT("%02d_%s"), Index, S.Name);
		if (Phase == 0)
		{
			runline::Controller& C = R->Runline().MutableCore();
			const runline::Vec3 F{-FMath::Sin(C.yaw), 0.0f, -FMath::Cos(C.yaw)};
			C.state = S.State;
			C.crouched = S.bCrouched;
			// The render shows look not yet stepped at once (the core is frozen): look down that way.
			if (S.Pitch != 0.0f) R->Runline().AddLook(FVector2D(0.0, S.Pitch));
			C.vel = F * S.Speed + runline::Vec3{0.0f, -S.FallSpeed, 0.0f};
			if (S.LandImpact)
			{
				runline::Event E{runline::EventKind::Land};
				E.impact = *S.LandImpact;
				R->Runline().DevEvents.Add(E);
			}
			Pc->SetViewTarget(R);
			Phase = 1;
			Elapsed = 0.0f;
		}
		else if (Phase == 1 && Elapsed > 0.8f)
		{
			FScreenshotRequest::RequestScreenshot(Base + TEXT("_fp.png"), false, false);
			Phase = 2;
		}
		else if (Phase == 2 && Elapsed > 1.0f)
		{
			// From her right, at hip height, looking at her.
			const FVector Hips = R->GetMesh()->GetComponentLocation() + FVector(0.0, 0.0, 95.0);
			const FVector From = Hips + R->GetActorRightVector() * 300.0;
			if (ACameraActor* Cam = Side.Get())
			{
				Cam->SetActorLocationAndRotation(From, (Hips - From).Rotation());
				Pc->SetViewTarget(Cam);
			}
			Phase = 3;
		}
		else if (Phase == 3 && Elapsed > 1.3f)
		{
			FScreenshotRequest::RequestScreenshot(Base + TEXT("_side.png"), false, false);
			Phase = 4;
		}
		else if (Phase == 4 && Elapsed > 1.5f)
		{
			++Index;
			Phase = 0;
			Elapsed = 0.0f;
		}
		return true;
	}));
}

void ARunlineRunGameMode::StartScan()
{
	// Dev: for each listed slot, the side view at 8 times spread over its clip (finding each clip's
	// move window and hold frame). Saved/Screenshots/Scan/<Slot>_<k>.png.
	TArray<FString> Names;
	ScanSlots.ParseIntoArray(Names, TEXT(","), true);
	TWeakObjectPtr<ARunlineRunGameMode> Self(this);
	const FString Dir = FPaths::ScreenShotDir() / TEXT("Scan");
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[Self, Dir, Names, Elapsed = 0.0f, Index = -1, Phase = 0, Side = TWeakObjectPtr<ACameraActor>()](float Dt) mutable
	{
		ARunlineRunGameMode* Mode = Self.Get();
		if (!Mode) return false;
		ARunlineRunner* R = Cast<ARunlineRunner>(UGameplayStatics::GetPlayerPawn(Mode, 0));
		APlayerController* Pc = UGameplayStatics::GetPlayerController(Mode, 0);
		URunlineBodyAnimInstance* Body = R ? Cast<URunlineBodyAnimInstance>(R->GetMesh()->GetAnimInstance()) : nullptr;
		if (!R || !Pc || !Body) return true;
		Elapsed += Dt;
		constexpr int32 Shots = 8;
		if (Index < 0)
		{
			if (Elapsed < 3.0f) return true;
			R->Runline().bFrozen = true;
			Side = Mode->GetWorld()->SpawnActor<ACameraActor>();
			const FVector Hips = R->GetMesh()->GetComponentLocation() + FVector(0.0, 0.0, 95.0);
			const FVector From = Hips + R->GetActorRightVector() * 350.0;
			Side->SetActorLocationAndRotation(From, (Hips - From).Rotation());
			Pc->SetViewTarget(Side.Get());
			Index = 0;
			Elapsed = 0.0f;
		}
		if (Index >= Names.Num() * Shots)
		{
			if (Elapsed > 1.0f)
			{
				FPlatformMisc::RequestExit(false);
				return false;
			}
			return true;
		}
		ERunlineAnimSlot Slot;
		if (!RunlineAnimSlotFromName(Names[Index / Shots], Slot))
		{
			Index += Shots;
			return true;
		}
		const int32 K = Index % Shots;
		const float Time = Body->ClipLength(Slot) * float(K) / float(Shots - 1) * 0.999f;
		if (Phase == 0)
		{
			Body->DevOverride = FRunlineBodyLayer{Slot, Time, 1.0f};
			Phase = 1;
			Elapsed = 0.0f;
		}
		else if (Phase == 1 && Elapsed > 0.4f)
		{
			FScreenshotRequest::RequestScreenshot(Dir / FString::Printf(TEXT("%s_%d_%.2fs.png"), *Names[Index / Shots], K, Time), false, false);
			Phase = 2;
		}
		else if (Phase == 2 && Elapsed > 0.6f)
		{
			++Index;
			Phase = 0;
		}
		return true;
	}));
}
