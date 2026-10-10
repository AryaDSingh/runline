#pragma once
#include "CoreMinimal.h"
#include "CollisionQueryParams.h"
#include "RunlineCoreBridge.h"
#include <vector>

class AActor;
class UPrimitiveComponent;
class UWorld;

/// The level as RunlineCore sees it, answered from an Unreal world. Unreal's overlap query finds
/// the blocking UBoxComponents near the query (the broadphase); the core's own box maths answers
/// it (the narrowphase), so results are bit-identical to a BoxWorld of the same boxes. Only
/// axis-aligned UBoxComponents count (M2a); any other blocking primitive in range is ignored with
/// a one-time warning.
class RUNLINE_API FUnrealWorld final : public runline::World
{
public:
	/// `Ignore` (usually the runner) is never seen.
	FUnrealWorld(const UWorld* InWorld, const AActor* Ignore);

	std::optional<runline::SweepHit> Sweep(runline::Vec3 Half, runline::Vec3 Start, runline::Vec3 Delta) const override;
	bool Overlaps(const runline::Aabb& Region) const override;

	/// The boxes the broadphase finds overlapping `Region` (grown by Margin), ordered by their
	/// level order (the RunlineOrder<N> component tag), then by min corner: BoxWorld keeps list
	/// order on exact ties, so the order must not depend on what Unreal returns first.
	void GatherBoxes(const runline::Aabb& Region, std::vector<runline::Aabb>& Out) const;

	/// How far past the query region the broadphase looks (m): more than BoxWorld's 0.01 m hair.
	static constexpr float Margin = 0.05f;

private:
	void WarnUnsupported(const UPrimitiveComponent& C) const;

	const UWorld* World;
	FCollisionQueryParams Params;
	mutable TSet<TWeakObjectPtr<const UPrimitiveComponent>> Warned;
};
