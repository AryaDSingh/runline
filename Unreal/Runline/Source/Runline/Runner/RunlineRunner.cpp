#include "RunlineRunner.h"
#include "Camera/CameraComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "RunlineCourseActors.h"
#include "RunlineMovementComponent.h"
#include "RunlineRunGameMode.h"
#include "RunlineSettings.h"
#include "RunlineUnits.h"
#include "RunlineAnimSet.h"
#include "RunlineBodyAnimInstance.h"
#include "SRunlineMenu.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include "Engine/GameViewportClient.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

ARunlineRunner::ARunlineRunner(const FObjectInitializer& OI)
	: Super(OI.SetDefaultSubobjectClass<URunlineMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	bUseControllerRotationYaw = false;
	// The whole simulation's speed (so every gap and wall on faith-runner's courses still works):
	// x1.25 after the M2 playtest, 20% slower (x1.0, Mirror's Edge's own) after the M3b playtest.
	// The body's clips follow the core's seconds, so they slow with it.
	Runline().GameSpeed = 1.0f;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->bUsePawnControlRotation = false;
	// The camera is placed in world space every frame from the core's (interpolated) view.
	Camera->SetUsingAbsoluteLocation(true);
	Camera->SetUsingAbsoluteRotation(true);
	Camera->SetFieldOfView(90.0f);
	// The template's first-person rendering: the arms draw at their own FOV and scale, so they don't
	// stretch at wide FOVs or clip into walls (RunlineCharacter.cpp's values).
	Camera->bEnableFirstPersonFieldOfView = true;
	Camera->FirstPersonFieldOfView = 70.0f;
	Camera->bEnableFirstPersonScale = true;
	Camera->FirstPersonScale = 0.6f;

	// The template's Manny: the body (seen by others and in shadows, not by her) plays the GASP clips
	// by the core's state (URunlineBodyAnimInstance, M3b) when they're imported, else ABP_Unarmed,
	// which reads the movement component's velocity, acceleration and IsFalling (kept in step with
	// the core). The arms and the legs copy its pose.
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> Manny(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"));
	static ConstructorHelpers::FClassFinder<UAnimInstance> BodyAnim(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"));
	static ConstructorHelpers::FClassFinder<UAnimInstance> ArmsAnim(TEXT("/Game/FirstPerson/Anims/ABP_FP_Copy"));
	USkeletalMeshComponent* Body = GetMesh();
	Body->SetSkeletalMesh(Manny.Object);
	Body->SetAnimInstanceClass(BodyAnim.Class);
	Body->SetOwnerNoSee(true);
	Body->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;
	Body->SetRelativeLocationAndRotation(FVector(0.0, 0.0, -90.0), FRotator(0.0, -90.0, 0.0));   // Manny faces +Y
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonMesh"));
	FirstPersonMesh->SetupAttachment(Body);
	FirstPersonMesh->SetSkeletalMesh(Manny.Object);
	FirstPersonMesh->SetAnimInstanceClass(ArmsAnim.Class);
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));
	LegsMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("LegsMesh"));
	LegsMesh->SetupAttachment(Body);
	LegsMesh->SetSkeletalMesh(Manny.Object);
	LegsMesh->SetAnimInstanceClass(ArmsAnim.Class);
	LegsMesh->SetOnlyOwnerSee(true);
	LegsMesh->SetCastShadow(false);   // the body casts her shadow
	LegsMesh->SetCollisionProfileName(FName("NoCollision"));
}

TObjectPtr<URunlineAnimSet> ARunlineRunner::AnimSetForTests = nullptr;

void ARunlineRunner::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// Bones hidden in the constructor don't stick (the mesh isn't registered yet).
	LegsMesh->HideBoneByName(TEXT("spine_02"), PBO_None);
	// The GASP body when its clips are imported (scripts/import-gasp.ps1); a fresh clone keeps
	// ABP_Unarmed.
	const URunlineAnimSet* Set = AnimSetForTests ? AnimSetForTests.Get() : URunlineAnimSet::Get();
	const auto Loads = [Set](ERunlineAnimSlot Slot) { return Set && Set->Slots.IsValidIndex(int32(Slot)) && RunlineLoadClip(Set->Slots[int32(Slot)].Clip); };
	if (Loads(ERunlineAnimSlot::Idle) && Loads(ERunlineAnimSlot::Run)) GetMesh()->SetAnimInstanceClass(URunlineBodyAnimInstance::StaticClass());
}

URunlineMovementComponent& ARunlineRunner::Runline() const
{
	return *CastChecked<URunlineMovementComponent>(GetCharacterMovement());
}

void ARunlineRunner::BeginPlay()
{
	Super::BeginPlay();
	AddTickPrerequisiteComponent(GetCharacterMovement());
	// The body evaluates after the runner has given it this frame's input.
	GetMesh()->PrimaryComponentTick.AddPrerequisite(this, PrimaryActorTick);
	FindCourse();
}

void ARunlineRunner::FindCourse()
{
	Checkpoints.Reset();
	for (ARunlineCheckpoint* C : RunlineCourses::Checkpoints(GetWorld())) Checkpoints.Add(C);
	if (Checkpoints.Num() > 0) Runline().SpawnAt(Checkpoints[0]->CoreSpawn(), Checkpoints[0]->CoreYaw());
	Finish = RunlineCourses::Finish(GetWorld());
	Checkpoint = 0;
}

void ARunlineRunner::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const URunlineMovementComponent& M = Runline();
	Camera->SetWorldLocationAndRotation(M.GetRenderEye(), M.GetRenderRotation());
	Camera->SetFieldOfView(M.GetRenderFov());
	UpdateBodyPlacement();
	if (URunlineBodyAnimInstance* Body = Cast<URunlineBodyAnimInstance>(GetMesh()->GetAnimInstance()))
	{
		Body->SetInput(FRunlineBodyInput::From(M, M.GetFrameSeconds()));   // the game seconds the core ran
		FirstPersonMesh->SetVisibility(!Body->GetPose().bHideArms);
	}
	if (AController* C = GetController()) C->SetControlRotation(M.GetRenderRotation());
	UpdateCourse(DeltaSeconds);
}

void ARunlineRunner::UpdateCourse(float DeltaSeconds)
{
	const URunlineMovementComponent& M = Runline();
	// Checkpoints: faith-runner's app takes a checkpoint further on than the current one.
	TArray<ARunlineCheckpoint*> Cps(Checkpoints);
	const int32 At = RunlineCourses::CheckpointAt(Cps, M.Core().feet);
	if (At > Checkpoint && !bFinishedHold)
	{
		Checkpoint = At;
		Runline().SetRespawn(Cps[At]->CoreSpawn(), Cps[At]->CoreYaw());
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::White, FString::Printf(TEXT("CHECKPOINT - %s"), *Cps[At]->CheckpointName));
	}
	// The time trial, after the checkpoint (faith-runner's app order). Real seconds: the actor doesn't
	// tick while paused.
	const runline::Controller& Core = M.Core();
	const bool bInFinish = Finish && RunlineCourses::Inside(Finish->CoreRegion(), Core.feet);
	if (Clock.Update(DeltaSeconds, At == 0, std::holds_alternative<runline::Ground>(Core.state), bInFinish, Checkpoint))
	{
		// faith-runner: the finish puts you back on the start checkpoint (held until a respawn).
		Checkpoint = 0;
		bFinishedHold = true;
		if (Cps.Num() > 0) Runline().SetRespawn(Cps[0]->CoreSpawn(), Cps[0]->CoreYaw());
		FinishShownFor = 3.0f;
	}
	FinishShownFor = FMath::Max(FinishShownFor - DeltaSeconds, 0.0f);
}

void ARunlineRunner::UpdateBodyPlacement()
{
	// The body turns with the view (her yaw) and stands on her feet, whatever the capsule's height.
	SetActorRotation(FRotator(0.0, Runline().GetRenderRotation().Yaw, 0.0));
	// Her eye drops when she crouches or slides. ABP_Unarmed's body (a standing Manny) drops with it,
	// so the arms stay at the eye; the GASP body's clips crouch and slide by themselves (M3b), so it
	// stays on her feet.
	const runline::Controller& C = Runline().Core();
	const bool bGasp = Cast<URunlineBodyAnimInstance>(GetMesh()->GetAnimInstance()) != nullptr;
	const float EyeDrop = bGasp ? 0.0f : (C.GetView().eye.y - C.feet.y) - (C.tuning.standHeight - C.tuning.eyeFromTop);
	// And it moves with the rendered (interpolated) eye, not the capsule's 60 Hz steps, so the arms
	// don't hop against a camera that glides between steps (spec: camera and arms interpolated).
	const FVector Smoothing = Runline().GetRenderBaseEye() - RunlineUnits::ToUnrealPosition(C.GetView().eye);
	GetMesh()->SetWorldLocation(GetCapsuleComponent()->GetComponentLocation()
		+ FVector(0.0, 0.0, -GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + EyeDrop * 100.0) + Smoothing);
}

void ARunlineRunner::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Menu.IsValid()) ToggleMenu();   // unpauses and removes the widget (and saves)
	if (Menu.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Menu.ToSharedRef());   // no controller left to close it
		Menu.Reset();
	}
	if (Hud.IsValid() && GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(Hud.ToSharedRef());
	Hud.Reset();
	URunlineSettings::Get().Save();
	Super::EndPlay(Reason);
}

void ARunlineRunner::BuildInput()
{
	if (Mapping) return;
	const auto Make = [this](const TCHAR* Name, EInputActionValueType Type)
	{
		UInputAction* A = NewObject<UInputAction>(this, Name);
		A->ValueType = Type;
		return A;
	};
	MoveAction = Make(TEXT("Move"), EInputActionValueType::Axis2D);
	// W+S cancel, A+D cancel (faith-runner sums the keys); the default keeps the strongest key.
	MoveAction->AccumulationBehavior = EInputActionAccumulationBehavior::Cumulative;
	PadMoveAction = Make(TEXT("PadMove"), EInputActionValueType::Axis2D);
	MouseLookAction = Make(TEXT("MouseLook"), EInputActionValueType::Axis2D);
	PadLookAction = Make(TEXT("PadLook"), EInputActionValueType::Axis2D);
	JumpAction = Make(TEXT("Jump"), EInputActionValueType::Boolean);
	CrouchAction = Make(TEXT("Crouch"), EInputActionValueType::Boolean);
	TurnAction = Make(TEXT("Turn"), EInputActionValueType::Boolean);
	RespawnAction = Make(TEXT("Respawn"), EInputActionValueType::Boolean);
	MenuAction = Make(TEXT("Menu"), EInputActionValueType::Boolean);
	MenuAction->bTriggerWhenPaused = true;

	Mapping = NewObject<UInputMappingContext>(this, TEXT("RunlineMapping"));
	const auto Swizzle = [this]() { return NewObject<UInputModifierSwizzleAxis>(this); };   // default YXZ: a key's X becomes Y
	const auto Negate = [this]() { return NewObject<UInputModifierNegate>(this); };
	// Move: W/S forward and back (on Y), D/A right and left (on X), or the left stick.
	Mapping->MapKey(MoveAction, EKeys::W).Modifiers.Add(Swizzle());
	FEnhancedActionKeyMapping& S = Mapping->MapKey(MoveAction, EKeys::S);
	S.Modifiers.Add(Swizzle());
	S.Modifiers.Add(Negate());
	Mapping->MapKey(MoveAction, EKeys::D);
	Mapping->MapKey(MoveAction, EKeys::A).Modifiers.Add(Negate());
	// Sticks: raw values; faith-runner's dead zones and response are in URunlineSettings.
	Mapping->MapKey(PadMoveAction, EKeys::Gamepad_Left2D);
	Mapping->MapKey(MouseLookAction, EKeys::Mouse2D);
	Mapping->MapKey(PadLookAction, EKeys::Gamepad_Right2D);
	for (const FKey& K : {EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_LeftShoulder}) Mapping->MapKey(JumpAction, K);
	for (const FKey& K : {EKeys::LeftShift, EKeys::C, EKeys::LeftControl, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_LeftTrigger}) Mapping->MapKey(CrouchAction, K);
	for (const FKey& K : {EKeys::Q, EKeys::Gamepad_FaceButton_Top}) Mapping->MapKey(TurnAction, K);
	for (const FKey& K : {EKeys::R, EKeys::Gamepad_Special_Left}) Mapping->MapKey(RespawnAction, K);
	for (const FKey& K : {EKeys::Escape, EKeys::Gamepad_Special_Right}) Mapping->MapKey(MenuAction, K);
	// faith-runner's number keys: 1-9 put you at that checkpoint (1 restarts the run).
	const FKey Digits[] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine};
	for (const FKey& K : Digits)
	{
		UInputAction* A = Make(*FString::Printf(TEXT("Checkpoint%s"), *K.ToString()), EInputActionValueType::Boolean);
		Mapping->MapKey(A, K);
		CheckpointActions.Add(A);
	}
}

void ARunlineRunner::PawnClientRestart()
{
	Super::PawnClientRestart();
	BuildInput();
	if (!Hud.IsValid() && GEngine && GEngine->GameViewport)
	{
		TWeakObjectPtr<ARunlineRunner> Self(this);
		Hud = SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(FMargin(0.0f, 24.0f))
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 28))
			.ShadowOffset(FVector2D(2.0, 2.0))
			.Text_Lambda([Self] { return Self.IsValid() ? Self->HudText() : FText::GetEmpty(); })
		];
		GEngine->GameViewport->AddViewportWidgetContent(Hud.ToSharedRef(), 5);
	}
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
		if (UEnhancedInputLocalPlayerSubsystem* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
			Sub->AddMappingContext(Mapping, 1);
}

void ARunlineRunner::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	BuildInput();
	UEnhancedInputComponent* In = CastChecked<UEnhancedInputComponent>(PlayerInputComponent);
	In->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ARunlineRunner::OnMove);
	In->BindAction(MoveAction, ETriggerEvent::Completed, this, &ARunlineRunner::OnMoveStop);
	In->BindAction(PadMoveAction, ETriggerEvent::Triggered, this, &ARunlineRunner::OnPadMove);
	In->BindAction(PadMoveAction, ETriggerEvent::Completed, this, &ARunlineRunner::OnPadMoveStop);
	In->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &ARunlineRunner::OnMouseLook);
	In->BindAction(PadLookAction, ETriggerEvent::Triggered, this, &ARunlineRunner::OnPadLook);
	In->BindAction(JumpAction, ETriggerEvent::Started, this, &ARunlineRunner::OnJump);
	In->BindAction(JumpAction, ETriggerEvent::Completed, this, &ARunlineRunner::OnJumpStop);
	In->BindAction(CrouchAction, ETriggerEvent::Started, this, &ARunlineRunner::OnCrouch);
	In->BindAction(CrouchAction, ETriggerEvent::Completed, this, &ARunlineRunner::OnCrouchStop);
	In->BindAction(TurnAction, ETriggerEvent::Started, this, &ARunlineRunner::OnTurn);
	In->BindAction(RespawnAction, ETriggerEvent::Started, this, &ARunlineRunner::OnRespawn);
	In->BindAction(MenuAction, ETriggerEvent::Started, this, &ARunlineRunner::OnMenu);
	for (int32 I = 0; I < CheckpointActions.Num(); ++I)
		In->BindActionValueLambda(CheckpointActions[I], ETriggerEvent::Started, [this, I](const FInputActionValue&) { RespawnAt(I); });
}

void ARunlineRunner::OnMove(const FInputActionValue& V) { KeyMove = V.Get<FVector2D>(); PushMove(); }
void ARunlineRunner::OnMoveStop(const FInputActionValue&) { KeyMove = FVector2D::ZeroVector; PushMove(); }
void ARunlineRunner::OnPadMove(const FInputActionValue& V) { PadMove = URunlineSettings::PadMove(V.Get<FVector2D>()); PushMove(); }
void ARunlineRunner::OnPadMoveStop(const FInputActionValue&) { PadMove = FVector2D::ZeroVector; PushMove(); }
void ARunlineRunner::PushMove() { Runline().SetMove(KeyMove + PadMove); }
void ARunlineRunner::OnMouseLook(const FInputActionValue& V) { Runline().AddLook(URunlineSettings::Get().MouseLook(V.Get<FVector2D>())); }
void ARunlineRunner::OnPadLook(const FInputActionValue& V) { Runline().AddLook(URunlineSettings::Get().PadLook(V.Get<FVector2D>(), GetWorld()->GetDeltaSeconds())); }
void ARunlineRunner::OnJump(const FInputActionValue&) { Runline().PressJump(); Runline().SetJumpHeld(true); }
void ARunlineRunner::OnJumpStop(const FInputActionValue&) { Runline().SetJumpHeld(false); }
void ARunlineRunner::OnCrouch(const FInputActionValue&) { Runline().PressCrouch(); Runline().SetCrouchHeld(true); }
void ARunlineRunner::OnCrouchStop(const FInputActionValue&) { Runline().SetCrouchHeld(false); }
void ARunlineRunner::OnTurn(const FInputActionValue&) { Runline().PressTurn(); }
void ARunlineRunner::OnRespawn(const FInputActionValue&) { RespawnHere(); }

void ARunlineRunner::RespawnAt(int32 Index)
{
	if (!Checkpoints.IsValidIndex(Index)) return;
	Checkpoint = Index;
	Runline().SetRespawn(Checkpoints[Index]->CoreSpawn(), Checkpoints[Index]->CoreYaw());
	RespawnHere();
}

void ARunlineRunner::RespawnHere()
{
	bFinishedHold = false;
	Runline().Respawn();
	Clock.OnRespawn(Checkpoint);
}

FText ARunlineRunner::HudText() const
{
	if (Clock.Running) return FText::FromString(FRunlineTimer::Format(*Clock.Running));
	if (FinishShownFor > 0.0f && Clock.Last)
	{
		const bool bBest = Clock.Best && *Clock.Best == *Clock.Last;
		return FText::FromString(FString::Printf(TEXT("FINISH %s%s"), *FRunlineTimer::Format(*Clock.Last),
			bBest ? TEXT("  NEW BEST") : *FString::Printf(TEXT("  (best %s)"), *FRunlineTimer::Format(*Clock.Best))));
	}
	if (Clock.Best) return FText::FromString(FString::Printf(TEXT("best %s"), *FRunlineTimer::Format(*Clock.Best)));
	return FText::GetEmpty();
}
void ARunlineRunner::OnMenu(const FInputActionValue&) { ToggleMenu(); }

void ARunlineRunner::ToggleMenu()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !GEngine || !GEngine->GameViewport) return;
	if (!Menu.IsValid())
	{
		Menu = SNew(SRunlineMenu)
			.OnResume(FSimpleDelegate::CreateUObject(this, &ARunlineRunner::ToggleMenu))
			.OnRespawn(FSimpleDelegate::CreateWeakLambda(this, [this] { ToggleMenu(); RespawnHere(); }))
			.OnQuit(FSimpleDelegate::CreateWeakLambda(this, [this, PC] { UKismetSystemLibrary::QuitGame(this, PC, EQuitPreference::Quit, false); }));
		GEngine->GameViewport->AddViewportWidgetContent(Menu.ToSharedRef(), 10);
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(Menu);
		PC->SetInputMode(Mode);
		PC->bShowMouseCursor = true;
		UGameplayStatics::SetGamePaused(this, true);
	}
	else
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Menu.ToSharedRef());
		Menu.Reset();
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
		UGameplayStatics::SetGamePaused(this, false);
		URunlineSettings::Get().Save();
	}
}
