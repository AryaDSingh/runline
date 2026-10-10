#include "RunlineCourseData.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogRunlineCourse, Log, All);

namespace
{
	bool ParseBox(const TArray<FString>& F, int32 At, runline::Aabb& Out)
	{
		if (F.Num() < At + 6) return false;
		Out = {{FCString::Atof(*F[At]), FCString::Atof(*F[At + 1]), FCString::Atof(*F[At + 2])},
		       {FCString::Atof(*F[At + 3]), FCString::Atof(*F[At + 4]), FCString::Atof(*F[At + 5])}};
		return true;
	}
}

bool FRunlineCourseData::Load(const FString& LevelName, FRunlineCourseData& Out)
{
	Out = FRunlineCourseData{};
	Out.Name = LevelName;
	const FString Path = FPaths::ProjectDir() / TEXT("Data/Levels") / LevelName + TEXT(".csv");
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *Path))
	{
		UE_LOG(LogRunlineCourse, Error, TEXT("Runline course: no level file %s"), *Path);
		return false;
	}
	int32 Solids = 0;
	for (const FString& Line : Lines)
	{
		TArray<FString> F;
		Line.ParseIntoArray(F, TEXT(","), false);
		if (F.Num() == 0) continue;
		runline::Aabb Box;
		if ((F[0] == TEXT("solid") || F[0] == TEXT("decor")) && ParseBox(F, 2, Box))
		{
			const bool bSolid = F[0] == TEXT("solid");
			Out.Boxes.Add({Box, F[1], bSolid, bSolid ? Solids++ : -1});
		}
		else if (F[0] == TEXT("checkpoint") && F.Num() == 12 && ParseBox(F, 2, Box))
		{
			Out.Checkpoints.Add({F[1], Box, {FCString::Atof(*F[8]), FCString::Atof(*F[9]), FCString::Atof(*F[10])}, FCString::Atof(*F[11])});
		}
		else if (F[0] == TEXT("finish") && ParseBox(F, 1, Box))
		{
			Out.Finish = Box;
		}
		else if (F[0] == TEXT("skipped_fixtures") && F.Num() == 2)
		{
			Out.SkippedFixtures = FCString::Atoi(*F[1]);
			if (Out.SkippedFixtures > 0)
				UE_LOG(LogRunlineCourse, Warning, TEXT("Runline course %s: %d fixtures (ziplines, poles, beams) skipped until M4"), *LevelName, Out.SkippedFixtures);
		}
		else if (F[0] != TEXT("level"))
		{
			UE_LOG(LogRunlineCourse, Error, TEXT("Runline course %s: can't read line '%s'"), *LevelName, *Line);
			return false;
		}
	}
	if (Solids == 0 || Out.Checkpoints.Num() == 0)
	{
		UE_LOG(LogRunlineCourse, Error, TEXT("Runline course %s: no solids or no checkpoints"), *LevelName);
		return false;
	}
	return true;
}
