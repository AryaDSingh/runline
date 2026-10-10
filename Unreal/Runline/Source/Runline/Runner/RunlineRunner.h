#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "RunlineRunner.generated.h"

class ARunlineCourse;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class URunlineMovementComponent;
struct FInputActionValue;

/// The runner: an ACharacter whose movement is URunlineMovementComponent (RunlineCore), a camera at
/// the interpolated eye, and Enhanced Input built in code (faith-runner's bindings, keyboard and
/// pad). Checkpoints come from the course: walking into one makes it the respawn point.
UCLASS()
class ARunlineRunner : public ACharacter
{
	GENERATED_BODY()

public:
	ARunlineRunner(const FObjectInitializer& OI);

	URunlineMovementComponent& Runline() const;

	virtual void BeginPlay() override;
	/// Saves the settings and closes the menu (Quit, map change, the runner destroyed).
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PawnClientRestart() override;

	/// Opens the pause menu (pauses, shows the cursor) or closes it (saves the settings, resumes).
	void ToggleMenu();
	/// Turns the body with the view and stands it where her eye is (called each tick).
	void UpdateBodyPlacement();
	bool IsMenuOpen() const { return Menu.IsValid(); }

private:
	void BuildInput();
	void OnMove(const FInputActionValue& V);
	void OnMoveStop(const FInputActionValue& V);
	void OnPadMove(const FInputActionValue& V);
	void OnPadMoveStop(const FInputActionValue& V);
	void PushMove();
	void OnMouseLook(const FInputActionValue& V);
	void OnPadLook(const FInputActionValue& V);
	void OnJump(const FInputActionValue& V);
	void OnJumpStop(const FInputActionValue& V);
	void OnCrouch(const FInputActionValue& V);
	void OnCrouchStop(const FInputActionValue& V);
	void OnTurn(const FInputActionValue& V);
	void OnRespawn(const FInputActionValue& V);
	void OnMenu(const FInputActionValue& V);

	UPROPERTY(VisibleAnywhere, Category = "Runline")
	TObjectPtr<UCameraComponent> Camera;
	/// The template's first-person arms: they copy the (hidden) body's pose (ABP_FP_Copy's Copy Pose
	/// From Mesh uses the attached parent), so they hang off the body, which turns with the view.
	UPROPERTY(VisibleAnywhere, Category = "Runline")
	TObjectPtr<class USkeletalMeshComponent> FirstPersonMesh;
	UPROPERTY() TObjectPtr<UInputMappingContext> Mapping;
	UPROPERTY() TObjectPtr<UInputAction> MoveAction;
	UPROPERTY() TObjectPtr<UInputAction> PadMoveAction;
	UPROPERTY() TObjectPtr<UInputAction> MouseLookAction;
	UPROPERTY() TObjectPtr<UInputAction> PadLookAction;
	UPROPERTY() TObjectPtr<UInputAction> JumpAction;
	UPROPERTY() TObjectPtr<UInputAction> CrouchAction;
	UPROPERTY() TObjectPtr<UInputAction> TurnAction;
	UPROPERTY() TObjectPtr<UInputAction> RespawnAction;
	UPROPERTY() TObjectPtr<UInputAction> MenuAction;
	TSharedPtr<class SRunlineMenu> Menu;
	UPROPERTY() TObjectPtr<ARunlineCourse> Course;
	int32 Checkpoint = 0;
	// Keys and stick, summed as faith-runner does (the core clamps the length).
	FVector2D KeyMove = FVector2D::ZeroVector;
	FVector2D PadMove = FVector2D::ZeroVector;
};
