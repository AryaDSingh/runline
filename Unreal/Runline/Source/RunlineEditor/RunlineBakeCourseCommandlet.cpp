#include "RunlineBakeCourseCommandlet.h"
#include "Misc/Parse.h"
#include "RunlineBake.h"

int32 URunlineBakeCourseCommandlet::Main(const FString& Params)
{
	FString Course, Map;
	if (!FParse::Value(*Params, TEXT("course="), Course) || !FParse::Value(*Params, TEXT("map="), Map))
	{
		UE_LOG(LogTemp, Error, TEXT("Usage: -run=RunlineBakeCourse -course=<name in Data/Levels> -map=/Game/Path/To/Map"));
		return 1;
	}
	return RunlineBake::BakeCourse(Course, Map) ? 0 : 1;
}
