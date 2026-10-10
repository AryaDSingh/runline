#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RunlineCoreBridge.h"
#include "RunlineCourse.generated.h"

/// A faith-runner course built at runtime from Data/Levels/<name>.csv (exported by tools/golden from
/// faith-runner's greybox.rs and rooftops.rs): one blocking UBoxComponent per solid, tagged with its
/// level order (FUnrealWorld keeps BoxWorld's order on ties), a coloured cube per box, and a sun and
/// sky light so an empty map is lit. M2c bakes the same data into a real map.
UCLASS()
class ARunlineCourse : public AActor
{
	GENERATED_BODY()

public:
	ARunlineCourse();

	struct FCheckpoint
	{
		FString Name;
		runline::Aabb Region;
		runline::Vec3 Spawn;
		float Yaw = 0.0f;
	};

	/// Builds the named course; false (with an error logged) if its file is missing or malformed.
	bool Load(const FString& LevelName);
	int32 SolidCount() const { return Solids; }
	const TArray<FCheckpoint>& Checkpoints() const { return CheckpointList; }
	/// The first checkpoint whose region contains `Feet` (faith-runner's Level::checkpoint_at), or INDEX_NONE.
	int32 CheckpointAt(runline::Vec3 Feet) const;

private:
	void AddBox(const runline::Aabb& Box, const FString& Look, bool bSolid);
	void AddLights();

	int32 Solids = 0;
	TArray<FCheckpoint> CheckpointList;
	UPROPERTY() TObjectPtr<class UStaticMesh> Cube;
	UPROPERTY() TObjectPtr<class UMaterialInterface> ShapeMaterial;
	UPROPERTY() TMap<FString, TObjectPtr<class UMaterialInstanceDynamic>> LookMaterials;
};
