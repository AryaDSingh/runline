#pragma once
#include "CoreMinimal.h"

class UWorld;

namespace RunlineBake
{
	/// Builds a fresh map from Data/Levels/<CourseName>.csv (RunlineCourses::Spawn, with lights), adds
	/// a PlayerStart at checkpoint 0, sets the World Settings game mode to ARunlineRunGameMode, and
	/// saves it to MapPath (e.g. /Game/Runline/Maps/Rooftops). Returns the saved world, or nullptr
	/// (with an error logged).
	UWorld* BakeCourse(const FString& CourseName, const FString& MapPath);
}
