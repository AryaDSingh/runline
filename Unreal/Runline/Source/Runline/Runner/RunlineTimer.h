#pragma once
#include "CoreMinimal.h"

/// faith-runner's time trial (src/main.rs): the clock starts when you leave the start region
/// (checkpoint 0) on the ground, stops in the finish region, and resets when you respawn at the start.
struct FRunlineTimer
{
	/// Seconds, while a run is on.
	TOptional<float> Running;
	/// The last finished run, and the best this session.
	TOptional<float> Last;
	TOptional<float> Best;

	/// One frame. bInStart: she's in checkpoint 0's region; Checkpoint: the checkpoint she'd respawn
	/// at. Returns true on the frame she finishes.
	bool Update(float Dt, bool bInStart, bool bOnGround, bool bInFinish, int32 Checkpoint)
	{
		if (!Running && Checkpoint == 0 && !bInStart && bOnGround) Running = 0.0f;
		if (!Running) return false;
		Running = *Running + Dt;
		if (!bInFinish) return false;
		Last = *Running;
		if (!Best || *Last < *Best) Best = *Last;
		Running.Reset();
		return true;
	}

	/// A respawn (R, the menu): at the start the run is off.
	void OnRespawn(int32 Checkpoint)
	{
		if (Checkpoint == 0) Running.Reset();
	}

	/// mm:ss.cc like faith-runner's fmt_time, but truncated (its rounding can show 00:60.00).
	static FString Format(float Seconds)
	{
		const int32 Centis = FMath::FloorToInt(Seconds * 100.0f);
		return FString::Printf(TEXT("%02d:%02d.%02d"), Centis / 6000, (Centis / 100) % 60, Centis % 100);
	}
};
