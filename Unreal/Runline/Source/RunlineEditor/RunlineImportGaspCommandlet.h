#pragma once
#include "Commandlets/Commandlet.h"
#include "RunlineImportGaspCommandlet.generated.h"

/// Pass 2 of scripts/import-gasp.ps1: imports build/gasp-fbx/<Slot>.fbx onto our Manny skeleton as
/// /Game/ThirdParty/GASP/A_<Slot> (in place: root locked, no root motion) for every slot in
/// Data/Animation/gasp-clips.txt. -List=<file> -Fbx=<folder>.
UCLASS()
class URunlineImportGaspCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	URunlineImportGaspCommandlet();
	virtual int32 Main(const FString& Params) override;
};
