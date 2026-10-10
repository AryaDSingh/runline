#pragma once
// Test-only: the smallest pawn that carries URunlineMovementComponent.
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "RunlineMovementComponent.h"
#include "RunlineTestRunner.generated.h"

UCLASS(NotBlueprintable, HideDropdown)
class ARunlineTestRunner : public ACharacter
{
	GENERATED_BODY()
public:
	ARunlineTestRunner(const FObjectInitializer& OI)
		: Super(OI.SetDefaultSubobjectClass<URunlineMovementComponent>(ACharacter::CharacterMovementComponentName)) {}

	URunlineMovementComponent& Runline() const { return *CastChecked<URunlineMovementComponent>(GetCharacterMovement()); }
};
