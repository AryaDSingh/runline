#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "RunlineTestWorld.h"
#include "UnrealWorld.h"
THIRD_PARTY_INCLUDES_START
#include "Replay.h"
THIRD_PARTY_INCLUDES_END
#include <bit>
#include <fstream>
#include <memory>
#include <sstream>

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/// A golden CSV (header row skipped) as rows of floats.
	std::vector<std::vector<float>> ReadCsv(const char* Name)
	{
		std::ifstream In(std::string(RUNLINE_GOLDEN_DIR) + "/" + Name);
		std::vector<std::vector<float>> Rows;
		std::string Line;
		std::getline(In, Line);
		while (std::getline(In, Line))
		{
			std::vector<float> R;
			std::stringstream S(Line);
			std::string Cell;
			while (std::getline(S, Cell, ',')) R.push_back(std::stof(Cell));
			Rows.push_back(R);
		}
		return Rows;
	}

	bool Same(float A, float B) { return std::bit_cast<uint32>(A) == std::bit_cast<uint32>(B); }
	bool Same(runline::Vec3 A, runline::Vec3 B) { return Same(A.x, B.x) && Same(A.y, B.y) && Same(A.z, B.z); }
	bool SameHit(const std::optional<runline::SweepHit>& A, const std::optional<runline::SweepHit>& B)
	{
		return A.has_value() == B.has_value() && (!A || (Same(A->t, B->t) && Same(A->normal, B->normal) && Same(A->point, B->point)));
	}
}

// The M0 golden scene and sweeps (touching, sliding along faces, starting inside): FUnrealWorld
// must give BoxWorld's answer bit for bit.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineWorldGoldenSweeps, "Runline.World.GoldenSweeps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineWorldGoldenSweeps::RunTest(const FString&)
{
	FRunlineTestWorld TW;
	runline::BoxWorld Ref;
	const auto Scene = ReadCsv("scene.csv");
	TestTrue(TEXT("scene has boxes"), Scene.size() > 0);
	for (int32 I = 0; I < int32(Scene.size()); ++I)
	{
		const runline::Aabb B{{Scene[I][0], Scene[I][1], Scene[I][2]}, {Scene[I][3], Scene[I][4], Scene[I][5]}};
		Ref.Add(B);
		TW.AddBox(B, I);
	}
	const FUnrealWorld W(TW.World, nullptr);
	int32 Mismatches = 0, Count = 0, Overlaps = 0;
	for (const auto& R : ReadCsv("sweeps.csv"))
	{
		const runline::Vec3 H{R[0], R[1], R[2]}, S{R[3], R[4], R[5]}, D{R[6], R[7], R[8]};
		++Count;
		if (!SameHit(W.Sweep(H, S, D), Ref.Sweep(H, S, D)) && ++Mismatches <= 5)
			AddError(FString::Printf(TEXT("sweep from (%g,%g,%g) by (%g,%g,%g) differs"), S.x, S.y, S.z, D.x, D.y, D.z));
		// The same box as an overlap query, at the start and at the end.
		for (const runline::Vec3 C : {S, S + D})
		{
			const runline::Aabb Q = runline::Aabb::New(C - H, C + H);
			if (W.Overlaps(Q) != Ref.Overlaps(Q) && ++Mismatches <= 5)
				AddError(FString::Printf(TEXT("overlap at (%g,%g,%g) differs"), C.x, C.y, C.z));
			Overlaps += Ref.Overlaps(Q) ? 1 : 0;
		}
	}
	AddInfo(FString::Printf(TEXT("%d sweeps, %d overlapping boxes checked"), Count, Overlaps));
	TestTrue(TEXT("some sweeps"), Count > 100);
	TestEqual(TEXT("mismatches"), Mismatches, 0);
	return true;
}

// Resting on a floor, flush against a wall, starting inside: the core's contact rules, exactly.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineWorldTouching, "Runline.World.TouchingAndInside",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineWorldTouching::RunTest(const FString&)
{
	FRunlineTestWorld TW;
	runline::BoxWorld Ref;
	const runline::Aabb Floor = runline::Aabb::New({-5.0f, -1.0f, -5.0f}, {5.0f, 0.0f, 5.0f});
	const runline::Aabb Wall = runline::Aabb::New({1.0f, 0.0f, -5.0f}, {1.5f, 3.0f, 5.0f});
	Ref.Add(Floor).Add(Wall);
	TW.AddBox(Floor, 0);
	TW.AddBox(Wall, 1);
	const FUnrealWorld W(TW.World, nullptr);
	const runline::Vec3 Half{0.3f, 0.9f, 0.3f};
	const runline::Vec3 OnFloor{0.0f, 0.9f, 0.0f};          // resting exactly on the floor
	const runline::Vec3 AtWall{0.7f, 0.9f, 0.0f};           // flush against the wall too
	const runline::Vec3 Sunk{0.0f, 0.5f, 0.0f};             // started inside the floor
	const runline::Aabb Rest = runline::Aabb::New(OnFloor - Half, OnFloor + Half);
	TestFalse(TEXT("resting on the floor isn't overlapping"), W.Overlaps(Rest));
	TestEqual(TEXT("resting: same as BoxWorld"), W.Overlaps(Rest), Ref.Overlaps(Rest));
	struct FCase { const TCHAR* Name; runline::Vec3 Start, Delta; };
	const FCase Cases[] = {
		{TEXT("down into the floor"), OnFloor, {0.0f, -0.1f, 0.0f}},
		{TEXT("along the floor"), OnFloor, {0.5f, 0.0f, 0.5f}},
		{TEXT("into the wall when flush"), AtWall, {0.2f, 0.0f, 0.0f}},
		{TEXT("along the wall when flush"), AtWall, {0.0f, 0.0f, 1.0f}},
		{TEXT("away from the wall when flush"), AtWall, {-0.5f, 0.0f, 0.0f}},
		{TEXT("sideways from inside the floor"), Sunk, {1.0f, 0.0f, 0.0f}},
		{TEXT("up out of the floor"), Sunk, {0.0f, 1.0f, 0.0f}},
	};
	for (const FCase& C : Cases)
		TestTrue(C.Name, SameHit(W.Sweep(Half, C.Start, C.Delta), Ref.Sweep(Half, C.Start, C.Delta)));
	const auto Down = W.Sweep(Half, OnFloor, {0.0f, -0.1f, 0.0f});
	TestTrue(TEXT("touching ahead is hit at once"), Down.has_value() && Down->t == 0.0f && Down->normal == runline::Vec3{0.0f, 1.0f, 0.0f});
	return true;
}

// Only what blocks the runner is seen: not her own actor, not triggers.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineWorldIgnores, "Runline.World.IgnoresOwnerAndTriggers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineWorldIgnores::RunTest(const FString&)
{
	FRunlineTestWorld TW;
	runline::BoxWorld Ref;
	const runline::Aabb Floor = runline::Aabb::New({-5.0f, -1.0f, -5.0f}, {5.0f, 0.0f, 5.0f});
	Ref.Add(Floor);
	TW.AddBox(Floor, 0);
	// A trigger right where she'll fall.
	AActor* Trigger = TW.AddBox(runline::Aabb::New({-1.0f, 1.0f, -1.0f}, {1.0f, 1.2f, 1.0f}), 1);
	UPrimitiveComponent* T = Cast<UPrimitiveComponent>(Trigger->GetRootComponent());
	T->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	T->SetCollisionResponseToAllChannels(ECR_Overlap);
	// Her own actor, a box around her.
	AActor* Self = TW.AddBox(runline::Aabb::New({-0.5f, 2.0f, -0.5f}, {0.5f, 2.6f, 0.5f}), 2);
	const FUnrealWorld W(TW.World, Self);
	const runline::Vec3 Half{0.3f, 0.9f, 0.3f};
	const runline::Vec3 Start{0.0f, 3.5f, 0.0f}, Delta{0.0f, -4.0f, 0.0f};
	const auto Hit = W.Sweep(Half, Start, Delta);
	TestTrue(TEXT("hits only the floor"), SameHit(Hit, Ref.Sweep(Half, Start, Delta)));
	TestFalse(TEXT("own box isn't an overlap"), W.Overlaps(runline::Aabb::New({-0.2f, 2.1f, -0.2f}, {0.2f, 2.5f, 0.2f})));
	TestFalse(TEXT("trigger isn't an overlap"), W.Overlaps(runline::Aabb::New({-0.2f, 1.05f, -0.2f}, {0.2f, 1.15f, 0.2f})));
	return true;
}

// Two boxes hit at exactly the same t (a diagonal sweep into an inside corner made of two
// boxes): BoxWorld keeps the earlier box in level order, so FUnrealWorld must too, whatever
// order Unreal returns them in and however their coordinates sort.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineWorldTies, "Runline.World.TiesKeepLevelOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineWorldTies::RunTest(const FString&)
{
	const runline::Vec3 Half{0.3f, 0.9f, 0.3f};
	const runline::Vec3 Start{0.0f, 1.0f, 0.0f}, Delta{1.0f, 0.0f, 1.0f};
	// Faces at x = 0.8 and z = 0.8: the box (half 0.3) touches both at t = 0.5.
	const runline::Aabb WallX = runline::Aabb::New({0.8f, 0.0f, -5.0f}, {1.0f, 3.0f, 0.4f});
	const runline::Aabb WallZ = runline::Aabb::New({-5.0f, 0.0f, 0.8f}, {0.4f, 3.0f, 1.0f});
	for (const bool bXFirst : {true, false})
	{
		FRunlineTestWorld TW;
		runline::BoxWorld Ref;
		// WallZ sorts first by min corner (x = -5), so level order must win when WallX is first.
		if (bXFirst) { Ref.Add(WallX).Add(WallZ); TW.AddBox(WallX, 0); TW.AddBox(WallZ, 1); }
		else { Ref.Add(WallZ).Add(WallX); TW.AddBox(WallZ, 0); TW.AddBox(WallX, 1); }
		const FUnrealWorld W(TW.World, nullptr);
		const auto Want = Ref.Sweep(Half, Start, Delta);
		TestTrue(TEXT("a tie"), Want.has_value() && Want->t == 0.5f);
		TestTrue(bXFirst ? TEXT("x wall first: same hit as BoxWorld") : TEXT("z wall first: same hit as BoxWorld"), SameHit(W.Sweep(Half, Start, Delta), Want));
	}
	return true;
}

// Every committed box trace, replayed through Unreal: the whole adapter, bit-exact.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineWorldTraces, "Runline.World.EveryBoxTraceReplays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineWorldTraces::RunTest(const FString&)
{
	const FString Dir = FString(RUNLINE_GOLDEN_DIR) / TEXT("traces");
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.trace")), true, false);
	Files.Sort();
	int32 Replayed = 0, Skipped = 0, Failed = 0;
	for (const FString& File : Files)
	{
		std::ifstream In(TCHAR_TO_UTF8(*(Dir / File)));
		const trace::Trace T = trace::Parse(In, TCHAR_TO_UTF8(*File));
		bool bMesh = false;
		for (const auto& Wd : T.worlds) bMesh |= !Wd.tris.empty();
		if (bMesh) { ++Skipped; continue; }
		std::vector<std::unique_ptr<FRunlineTestWorld>> TestWorlds;
		std::vector<std::unique_ptr<FUnrealWorld>> Worlds;
		for (const auto& Wd : T.worlds)
		{
			TestWorlds.push_back(std::make_unique<FRunlineTestWorld>());
			for (int32 I = 0; I < int32(Wd.boxes.size()); ++I) TestWorlds.back()->AddBox(Wd.boxes[I], I);
			Worlds.push_back(std::make_unique<FUnrealWorld>(TestWorlds.back()->World, nullptr));
		}
		const std::string R = replay::Replay(T, [&](std::size_t I) -> const runline::World& { return *Worlds[I]; });
		if (!R.empty() && ++Failed <= 10) AddError(FString::Printf(TEXT("%s: %hs"), *File, R.c_str()));
		++Replayed;
	}
	AddInfo(FString::Printf(TEXT("%d box traces replayed, %d mesh traces skipped"), Replayed, Skipped));
	TestTrue(TEXT("box traces found"), Replayed > 100);
	TestEqual(TEXT("divergent traces"), Failed, 0);
	return true;
}

#endif
