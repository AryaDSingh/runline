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

	/// Mouse look: radians per mouse count (faith-runner: 0.0022).
	UPROPERTY(EditAnywhere, Category = "Runline|Input")
	float MouseRadiansPerCount = 0.0022f;
	/// Stick look: radians per second at full deflection (faith-runner: 3.2).
	UPROPERTY(EditAnywhere, Category = "Runline|Input")
	float PadRadiansPerSecond = 3.2f;

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PawnClientRestart() override;

private:
	void BuildInput();
	void OnMove(const FInputActionValue& V);
	void OnMoveStop(const FInputActionValue& V);
	void OnMouseLook(const FInputActionValue& V);
	void OnPadLook(const FInputActionValue& V);
	void OnJump(const FInputActionValue& V);
	void OnJumpStop(const FInputActionValue& V);
	void OnCrouch(const FInputActionValue& V);
	void OnCrouchStop(const FInputActionValue& V);
	void OnTurn(const FInputActionValue& V);
	void OnRespawn(const FInputActionValue& V);

	UPROPERTY(VisibleAnywhere, Category = "Runline")
	TObjectPtr<UCameraComponent> Camera;
	UPROPERTY() TObjectPtr<UInputMappingContext> Mapping;
	UPROPERTY() TObjectPtr<UInputAction> MoveAction;
	UPROPERTY() TObjectPtr<UInputAction> MouseLookAction;
	UPROPERTY() TObjectPtr<UInputAction> PadLookAction;
	UPROPERTY() TObjectPtr<UInputAction> JumpAction;
	UPROPERTY() TObjectPtr<UInputAction> CrouchAction;
	UPROPERTY() TObjectPtr<UInputAction> TurnAction;
	UPROPERTY() TObjectPtr<UInputAction> RespawnAction;
	UPROPERTY() TObjectPtr<ARunlineCourse> Course;
	int32 Checkpoint = 0;
};
