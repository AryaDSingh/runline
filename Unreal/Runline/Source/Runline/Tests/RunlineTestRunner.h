#pragma once
// Test-only: the smallest pawn that carries URunlineMovementComponent.
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "RunlineMovementComponent.h"
#include "RunlineTestRunner.generated.h"

UCLASS(NotBlueprintable, HideDropdown)
class RUNLINE_API ARunlineTestRunner : public ACharacter
{
	GENERATED_BODY()
public:
	ARunlineTestRunner(const FObjectInitializer& OI)
		: Super(OI.SetDefaultSubobjectClass<URunlineMovementComponent>(ACharacter::CharacterMovementComponentName)) {}

	URunlineMovementComponent& Runline() const { return *CastChecked<URunlineMovementComponent>(GetCharacterMovement()); }

	/// How many times CMC's SetMovementMode has run (it fires OnMovementModeChanged).
	int32 ModeChanges = 0;
	virtual void OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode = 0) override
	{
		Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);
		++ModeChanges;
	}
};
