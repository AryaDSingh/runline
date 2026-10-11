#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RunlineAnimSet.generated.h"

class UAnimSequence;

/// The clips the body plays (spec M3b §4). Order matters: it indexes URunlineAnimSet::Slots.
UENUM()
enum class ERunlineAnimSlot : uint8
{
	Idle, Walk, Run, Sprint, CrouchIdle, CrouchWalk, JumpStand, JumpRun, JumpSprint, Fall,
	LandStandLight, LandStandHeavy, LandRunLight, LandRunHeavy, LandSprintLight, LandSprintHeavy,
	Stumble, Roll, SlideInto, SlideLoop, SlideOut, Vault, Mantle, StepUp, PullUp, Springboard, Hang,
	Count
};

RUNLINE_API const TCHAR* RunlineAnimSlotName(ERunlineAnimSlot Slot);
RUNLINE_API bool RunlineAnimSlotFromName(const FString& Name, ERunlineAnimSlot& Out);

USTRUCT()
struct FRunlineAnimSlotDef
{
	GENERATED_BODY()
	/// The clip (soft: the GASP clips are git-ignored, so this asset loads without them).
	UPROPERTY(EditAnywhere, Category = "Anim") TSoftObjectPtr<UAnimSequence> Clip;
	/// Metres per second the clip was captured at (loops; measured at import). 0: not a loop.
	UPROPERTY(EditAnywhere, Category = "Anim") float AuthoredSpeed = 0.0f;
	/// The part of the clip that is the move (seconds; To 0: the clip's end). GASP clips carry
	/// lead-ins and run-outs (a 3.4 s roll), so a timed move maps the core's progress onto this
	/// window, not the whole clip.
	UPROPERTY(EditAnywhere, Category = "Anim") float From = 0.0f;
	UPROPERTY(EditAnywhere, Category = "Anim") float To = 0.0f;
	/// For a held stand-in: where in the clip (0..1) to hold.
	UPROPERTY(EditAnywhere, Category = "Anim") float HoldAt = 0.0f;
	/// A stand-in for a move GASP has no clip for (M3c replaces these).
	UPROPERTY(EditAnywhere, Category = "Anim") bool bStandIn = false;
};

/// The body's clips, one per slot (made by scripts/import-gasp.ps1; edit in the editor to try
/// another clip).
UCLASS()
class RUNLINE_API URunlineAnimSet : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Anim") TArray<FRunlineAnimSlotDef> Slots;
	/// /Game/Runline/Animation/DA_RunlineAnimSet, or null.
	static URunlineAnimSet* Get();
};
