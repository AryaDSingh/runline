#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RunlineBakeCourseCommandlet.generated.h"

/// Bakes a faith-runner course into a map:
///   UnrealEditor-Cmd.exe Runline.uproject -run=RunlineBakeCourse -course=rooftops -map=/Game/Runline/Maps/Rooftops
/// (scripts/bake-courses.ps1 bakes both courses).
UCLASS()
class URunlineBakeCourseCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
