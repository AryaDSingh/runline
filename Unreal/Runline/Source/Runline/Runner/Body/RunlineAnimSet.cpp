#include "RunlineAnimSet.h"

const TCHAR* RunlineAnimSlotName(ERunlineAnimSlot Slot)
{
	static const TCHAR* Names[] = {TEXT("Idle"), TEXT("Walk"), TEXT("Run"), TEXT("Sprint"), TEXT("CrouchIdle"), TEXT("CrouchWalk"),
		TEXT("JumpStand"), TEXT("JumpRun"), TEXT("JumpSprint"), TEXT("Fall"), TEXT("LandStandLight"), TEXT("LandStandHeavy"),
		TEXT("LandRunLight"), TEXT("LandRunHeavy"), TEXT("LandSprintLight"), TEXT("LandSprintHeavy"), TEXT("Stumble"), TEXT("Roll"),
		TEXT("SlideInto"), TEXT("SlideLoop"), TEXT("SlideOut"), TEXT("Vault"), TEXT("Mantle"), TEXT("StepUp"), TEXT("PullUp"),
		TEXT("Springboard"), TEXT("Hang")};
	static_assert(UE_ARRAY_COUNT(Names) == int32(ERunlineAnimSlot::Count));
	return int32(Slot) < int32(ERunlineAnimSlot::Count) ? Names[int32(Slot)] : TEXT("?");
}

bool RunlineAnimSlotFromName(const FString& Name, ERunlineAnimSlot& Out)
{
	for (int32 I = 0; I < int32(ERunlineAnimSlot::Count); ++I)
		if (Name == RunlineAnimSlotName(ERunlineAnimSlot(I))) { Out = ERunlineAnimSlot(I); return true; }
	return false;
}

URunlineAnimSet* URunlineAnimSet::Get()
{
	return LoadObject<URunlineAnimSet>(nullptr, TEXT("/Game/Runline/Animation/DA_RunlineAnimSet.DA_RunlineAnimSet"), nullptr, LOAD_NoWarn | LOAD_Quiet);
}
