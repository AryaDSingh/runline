#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RunlineCoreBridge.h"
#include "RunlineCourseActors.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
struct FRunlineCourseData;

/// How a box is drawn (faith-runner's greybox Look). Runner is Mirror's Edge's red "runner vision".
UENUM()
enum class ERunlineLook : uint8 { Roof, Wall, Runner, Prop, Finish, Skyline };

/// One box of a course. The box component is what the core collides with (blocking, tagged with
/// its level order, which breaks exact ties as faith-runner's list order does); the cube follows it
/// (OnConstruction), so moving or resizing the box in the editor changes both.
UCLASS()
class RUNLINE_API ARunlineBox : public AActor
{
	GENERATED_BODY()

public:
	ARunlineBox();

	UPROPERTY(VisibleAnywhere, Category = "Runline") TObjectPtr<UBoxComponent> Box;
	UPROPERTY(VisibleAnywhere, Category = "Runline") TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY(EditAnywhere, Category = "Runline") ERunlineLook Look = ERunlineLook::Roof;
	/// False: drawn only (painted markings), never collided with.
	UPROPERTY(EditAnywhere, Category = "Runline") bool bSolid = true;
	/// faith-runner's level order (solids); used to break exact ties. -1 (a box you placed): after
	/// every level box.
	UPROPERTY(EditAnywhere, Category = "Runline") int32 Order = -1;

	/// Places the actor and sizes the box to a core box (metres).
	void SetFromCore(const runline::Aabb& B);
	runline::Aabb CoreBox() const;
	/// Fits the cube to the box, colours it by Look, and sets collision and the order tag.
	void UpdateVisual();
	virtual void OnConstruction(const FTransform& Transform) override;

	static ERunlineLook LookFromName(const FString& Name);
};

/// A checkpoint: walking into Region makes the spawn point (with its yaw) the respawn point.
UCLASS()
class RUNLINE_API ARunlineCheckpoint : public AActor
{
	GENERATED_BODY()

public:
	ARunlineCheckpoint();

	/// The region (no collision; drawn in the editor).
	UPROPERTY(VisibleAnywhere, Category = "Runline") TObjectPtr<UBoxComponent> Region;
	UPROPERTY(EditAnywhere, Category = "Runline") FString CheckpointName;
	/// Order along the course (0 is the start). -1 (one you placed): after the numbered ones.
	UPROPERTY(EditAnywhere, Category = "Runline") int32 Index = -1;
	/// Where she respawns, relative to the actor.
	UPROPERTY(EditAnywhere, Category = "Runline", meta = (MakeEditWidget)) FVector SpawnPoint = FVector::ZeroVector;
	/// Which way she faces when she respawns (Unreal yaw, degrees).
	UPROPERTY(EditAnywhere, Category = "Runline") float SpawnYawDegrees = 0.0f;

	runline::Aabb CoreRegion() const;
	runline::Vec3 CoreSpawn() const;
	float CoreYaw() const;
	virtual void OnConstruction(const FTransform& Transform) override;
};

/// The finish: reaching its region stops the time trial.
UCLASS()
class RUNLINE_API ARunlineFinish : public AActor
{
	GENERATED_BODY()

public:
	ARunlineFinish();
	UPROPERTY(VisibleAnywhere, Category = "Runline") TObjectPtr<UBoxComponent> Region;
	runline::Aabb CoreRegion() const;
	virtual void OnConstruction(const FTransform& Transform) override;
};

namespace RunlineCourses
{
	/// Spawns a course's actors into World (and a sun, sky light and atmosphere if bLights).
	RUNLINE_API void Spawn(UWorld* World, const FRunlineCourseData& Data, bool bLights);
	/// The world's checkpoints, sorted by Index.
	RUNLINE_API TArray<ARunlineCheckpoint*> Checkpoints(const UWorld* World);
	/// The world's finish, if it has one.
	RUNLINE_API ARunlineFinish* Finish(const UWorld* World);
	/// The first checkpoint whose region contains Feet (faith-runner's checkpoint_at), or INDEX_NONE.
	RUNLINE_API int32 CheckpointAt(const TArray<ARunlineCheckpoint*>& Checkpoints, runline::Vec3 Feet);
	/// Whether a core point is inside a core region (faith-runner's `inside`, faces included).
	RUNLINE_API bool Inside(const runline::Aabb& Region, runline::Vec3 P);
}
