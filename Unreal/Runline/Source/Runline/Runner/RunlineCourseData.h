#pragma once
#include "CoreMinimal.h"
#include "RunlineCoreBridge.h"

/// A faith-runner course from Data/Levels/<name>.csv (exported by tools/golden from faith-runner's
/// greybox.rs and rooftops.rs), parsed. Core metres, Y up.
struct RUNLINE_API FRunlineCourseData
{
	struct FBoxEntry
	{
		runline::Aabb Box;
		FString Look;
		bool bSolid = true;
		/// Solids: their index in the level (faith-runner's order, which breaks exact ties). Decor: -1.
		int32 Order = -1;
	};
	struct FCheckpointEntry
	{
		FString Name;
		runline::Aabb Region;
		runline::Vec3 Spawn;
		float Yaw = 0.0f;
	};

	FString Name;
	TArray<FBoxEntry> Boxes;
	TArray<FCheckpointEntry> Checkpoints;
	TOptional<runline::Aabb> Finish;
	/// Ziplines, poles, beams: not ported until M4.
	int32 SkippedFixtures = 0;

	/// False (and an error logged) if the file is missing or a line can't be read.
	static bool Load(const FString& LevelName, FRunlineCourseData& Out);
};
