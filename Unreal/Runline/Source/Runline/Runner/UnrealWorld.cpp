#include "UnrealWorld.h"
#include "Components/BoxComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "RunlineUnits.h"

DEFINE_LOG_CATEGORY_STATIC(LogRunlineWorld, Log, All);

namespace
{
	/// The box's index in its level (tag RunlineOrder<N>), or after every tagged box.
	int32 OrderOf(const UPrimitiveComponent& C)
	{
		for (const FName& Tag : C.ComponentTags)
		{
			const FString S = Tag.ToString();
			if (S.StartsWith(TEXT("RunlineOrder"))) return FCString::Atoi(*S.RightChop(12));
		}
		return MAX_int32;
	}

	runline::Aabb Grow(const runline::Aabb& B, float By)
	{
		const runline::Vec3 M = runline::Vec3::Splat(By);
		return runline::Aabb::New(B.min - M, B.max + M);
	}
}

FUnrealWorld::FUnrealWorld(const UWorld* InWorld, const AActor* Ignore)
	: World(InWorld), Params(SCENE_QUERY_STAT(RunlineWorld), false)
{
	if (Ignore) Params.AddIgnoredActor(Ignore);
}

void FUnrealWorld::GatherBoxes(const runline::Aabb& Region, std::vector<runline::Aabb>& Out) const
{
	Out.clear();
	const FBox Q = RunlineUnits::ToUnrealBox(Grow(Region, Margin));
	TArray<FOverlapResult> Hits;
	World->OverlapMultiByObjectType(Hits, Q.GetCenter(), FQuat::Identity,
		FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects), FCollisionShape::MakeBox(Q.GetExtent()), Params);
	struct FCandidate { int32 Order; runline::Aabb Box; };
	TArray<FCandidate> Found;
	for (const FOverlapResult& H : Hits)
	{
		const UPrimitiveComponent* C = H.GetComponent();
		if (!C || !C->IsQueryCollisionEnabled()) continue;
		// The runner is a pawn: whatever blocks pawns is what she collides with (triggers overlap).
		if (C->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block) continue;
		const UBoxComponent* Box = Cast<UBoxComponent>(C);
		if (!Box || !C->GetComponentQuat().Equals(FQuat::Identity, 1e-6))
		{
			WarnUnsupported(*C);
			continue;
		}
		const FVector Ext = Box->GetScaledBoxExtent().GetAbs();
		const FVector Center = Box->GetComponentLocation();
		Found.Add({OrderOf(*C), RunlineUnits::ToCoreBox(FBox(Center - Ext, Center + Ext))});
	}
	Found.StableSort([](const FCandidate& A, const FCandidate& B)
	{
		if (A.Order != B.Order) return A.Order < B.Order;
		const runline::Vec3 a = A.Box.min, b = B.Box.min;
		if (a.x != b.x || a.y != b.y || a.z != b.z) return a.x != b.x ? a.x < b.x : a.y != b.y ? a.y < b.y : a.z < b.z;
		const runline::Vec3 c = A.Box.max, d = B.Box.max;
		return c.x != d.x ? c.x < d.x : c.y != d.y ? c.y < d.y : c.z < d.z;
	});
	Out.reserve(Found.Num());
	for (const FCandidate& F : Found) Out.push_back(F.Box);
}

std::optional<runline::SweepHit> FUnrealWorld::Sweep(runline::Vec3 Half, runline::Vec3 Start, runline::Vec3 Delta) const
{
	// The same candidates BoxWorld::Sweep keeps (boxes overlapping the swept region grown by a
	// 0.01 m hair); the core does the rest.
	const runline::Aabb A = runline::Aabb::New(Start - Half, Start + Half);
	const runline::Aabb Region = A.Union(A.Translated(Delta));
	std::vector<runline::Aabb> Boxes;
	GatherBoxes(Region, Boxes);
	const runline::Aabb Grown = Grow(Region, 0.01f);
	std::vector<runline::Aabb> Candidates;
	for (const runline::Aabb& B : Boxes)
		if (B.Overlaps(Grown)) Candidates.push_back(B);
	return runline::SweepBoxes(Candidates, Half, Start, Delta);
}

bool FUnrealWorld::Overlaps(const runline::Aabb& Region) const
{
	std::vector<runline::Aabb> Boxes;
	GatherBoxes(Region, Boxes);
	return runline::OverlapsBoxes(Boxes, Region);
}

void FUnrealWorld::WarnUnsupported(const UPrimitiveComponent& C) const
{
	if (Warned.Contains(&C)) return;
	Warned.Add(&C);
	UE_LOG(LogRunlineWorld, Warning, TEXT("%s blocks but isn't an axis-aligned UBoxComponent; RunlineCore ignores it (M2a supports boxes only)"), *C.GetPathName());
}
