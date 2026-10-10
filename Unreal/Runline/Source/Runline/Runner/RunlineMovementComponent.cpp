#include "RunlineMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "RunlineUnits.h"
#include "UnrealWorld.h"

DEFINE_LOG_CATEGORY_STATIC(LogRunlineMovement, Log, All);

URunlineMovementComponent::URunlineMovementComponent()
	: Controller(std::make_unique<runline::Controller>(runline::Tuning{}, runline::Vec3{}, 0.0f))
{
	PrevEye = Controller->GetView().eye;
}

URunlineMovementComponent::~URunlineMovementComponent() = default;

void URunlineMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	SetMovementMode(MOVE_Custom);
	SyncCapsule();
}

void URunlineMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	// Skip CMC's own movement entirely (its physics modes never run); keep the base bookkeeping.
	UMovementComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
	Advance(DeltaTime);
}

const runline::World& URunlineMovementComponent::CoreWorld()
{
	if (!World) World = std::make_unique<FUnrealWorld>(GetWorld(), GetOwner());
	return *World;
}

void URunlineMovementComponent::SpawnAt(runline::Vec3 Feet, float Yaw)
{
	SetRespawn(Feet, Yaw);
	Respawn();
}

void URunlineMovementComponent::SetRespawn(runline::Vec3 Feet, float Yaw)
{
	Controller->spawn = Feet;
	Controller->spawnYaw = Yaw;
}

void URunlineMovementComponent::Respawn()
{
	Controller->Respawn();
	PendingLook = FVector2D::ZeroVector;
	PrevEye = Controller->GetView().eye;
	SyncCapsule();
}

void URunlineMovementComponent::SetMove(FVector2D RightForward) { Move = RightForward; }
void URunlineMovementComponent::AddLook(FVector2D YawLeftPitchUp) { PendingLook += YawLeftPitchUp; }
void URunlineMovementComponent::PressJump() { bJumpLatched = true; }
void URunlineMovementComponent::SetJumpHeld(bool bHeld) { bJumpHeld = bHeld; }
void URunlineMovementComponent::PressCrouch() { bCrouchLatched = true; }
void URunlineMovementComponent::SetCrouchHeld(bool bHeld) { bCrouchHeld = bHeld; }
void URunlineMovementComponent::PressTurn() { bTurnLatched = true; }

runline::Input URunlineMovementComponent::TakeInput()
{
	runline::Input In;
	In.moveAxis = runline::Vec2{float(Move.X), float(Move.Y)}.ClampLengthMax(1.0f);   // as faith-runner's app
	In.strafeRaw = FMath::Clamp(float(Move.X), -1.0f, 1.0f);
	In.look = runline::Vec2{float(PendingLook.X), float(PendingLook.Y)};
	// A press counts as held on the step that takes it, even if released before that step.
	In.jumpPressed = bJumpLatched;
	In.jumpHeld = bJumpHeld || bJumpLatched;
	In.crouchPressed = bCrouchLatched;
	In.crouchHeld = bCrouchHeld || bCrouchLatched;
	In.turnPressed = bTurnLatched;
	bJumpLatched = bCrouchLatched = bTurnLatched = false;
	PendingLook = FVector2D::ZeroVector;
	return In;
}

void URunlineMovementComponent::StepOnce()
{
	const runline::Input In = TakeInput();
	PrevEye = Controller->GetView().eye;
	try
	{
		Controller->Step(StepSeconds, In, CoreWorld());
	}
	catch (const runline::NotPorted& E)
	{
		++NotPorted;
		const FString What = UTF8_TO_TCHAR(E.what());
		if (!NotPortedSeen.Contains(What))
		{
			NotPortedSeen.Add(What);
			UE_LOG(LogRunlineMovement, Error, TEXT("RunlineCore: %s; respawning"), *What);
		}
		Controller->Respawn();
		PrevEye = Controller->GetView().eye;
	}
	++Steps;
	if (OnStep) OnStep(*Controller, In);
}

void URunlineMovementComponent::Advance(float DeltaSeconds)
{
	Accumulator += DeltaSeconds;
	int32 N = FMath::FloorToInt(Accumulator / StepSeconds);
	if (N > MaxStepsPerFrame)
	{
		N = MaxStepsPerFrame;
		Accumulator = N * StepSeconds;   // drop the rest of the hitch
	}
	for (int32 I = 0; I < N; ++I)
	{
		StepOnce();
		Accumulator -= StepSeconds;
	}
	if (N > 0) SyncCapsule();
}

void URunlineMovementComponent::SyncCapsule()
{
	ACharacter* Owner = Cast<ACharacter>(GetOwner());
	if (!Owner || !Owner->GetCapsuleComponent()) return;
	UCapsuleComponent* Cap = Owner->GetCapsuleComponent();
	const float Radius = Controller->tuning.halfWidth * 100.0f;
	const float HalfHeight = Controller->Height() * 50.0f;
	if (Cap->GetUnscaledCapsuleRadius() != Radius || Cap->GetUnscaledCapsuleHalfHeight() != HalfHeight)
		Cap->SetCapsuleSize(Radius, HalfHeight, false);
	Cap->SetWorldLocation(RunlineUnits::ToUnrealPosition(Controller->feet) + FVector(0, 0, HalfHeight), false, nullptr, ETeleportType::TeleportPhysics);
	Velocity = RunlineUnits::ToUnrealVelocity(Controller->vel);
}

FVector URunlineMovementComponent::GetRenderEye() const
{
	const float Alpha = FMath::Clamp(Accumulator / StepSeconds, 0.0f, 1.0f);
	const runline::Vec3 Eye = Controller->GetView().eye;
	return RunlineUnits::ToUnrealPosition(PrevEye * (1.0f - Alpha) + Eye * Alpha);
}

FRotator URunlineMovementComponent::GetRenderRotation() const
{
	const runline::View V = Controller->GetView();
	return RunlineUnits::ToUnrealRotator(V.yaw + float(PendingLook.X), FMath::Clamp(V.pitch + float(PendingLook.Y), -1.5f, 1.5f), V.roll);
}
