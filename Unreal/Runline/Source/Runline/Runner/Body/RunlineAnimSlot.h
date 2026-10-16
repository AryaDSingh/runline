#pragma once
// The body's animation slots (spec 2026-10-11 M3b, §3): one clip each, filled from
// Data/Animation/gasp-clips.txt into URunlineAnimSet (Task 3), whose Slots array is indexed by
// these values. Plain C++ so the body chooser (RunlineBodyChooser.h) is testable outside Unreal.
#include <cstdint>

enum class ERunlineAnimSlot : std::uint8_t
{
	Idle, Walk, Run, Sprint, CrouchIdle, CrouchWalk,
	JumpStand, JumpRun, JumpSprint, Fall,
	LandStandLight, LandStandHeavy, LandRunLight, LandRunHeavy, LandSprintLight, LandSprintHeavy,
	Stumble, Roll, SlideInto, SlideLoop, SlideOut,
	Vault, Mantle, StepUp, PullUp, Springboard, Hang,
	Count
};
