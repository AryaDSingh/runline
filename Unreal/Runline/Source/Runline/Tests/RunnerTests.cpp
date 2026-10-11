#include "Misc/AutomationTest.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "RunlineRunner.h"
#include "RunlineTestWorld.h"
#include "RunlineSettings.h"
#include "RunlineMovementComponent.h"
#include "RunlineCourseActors.h"
#include "RunlineCourseData.h"
#include "RunlineAnimSet.h"
#include "RunlineBodyAnimInstance.h"
#include "Animation/AnimSequence.h"

#if WITH_DEV_AUTOMATION_TESTS

// The runner carries the template's Manny: a body running ABP_Unarmed (hidden from her) and arms
// running ABP_FP_Copy attached to it (the copy-pose node reads the attached parent).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineRunnerArms, "Runline.Runner.ArmsAndBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineRunnerArms::RunTest(const FString&)
{
	FRunlineTestWorld TW;
	ARunlineRunner* R = TW.World->SpawnActor<ARunlineRunner>();
	TestNotNull(TEXT("runner"), R);
	if (!R) return false;
	TArray<USkeletalMeshComponent*> Meshes;
	R->GetComponents<USkeletalMeshComponent>(Meshes);
	USkeletalMeshComponent* Arms = nullptr;
	for (USkeletalMeshComponent* M : Meshes)
		if (M != R->GetMesh()) Arms = M;
	TestNotNull(TEXT("arms"), Arms);
	if (!Arms) return false;
	TestNotNull(TEXT("body mesh loaded"), R->GetMesh()->GetSkeletalMeshAsset());
	TestNotNull(TEXT("arms mesh loaded"), Arms->GetSkeletalMeshAsset());
	// ABP_Unarmed, or (M3b) the GASP body when its clips are imported.
	TestTrue(TEXT("body runs ABP_Unarmed or the GASP body"), R->GetMesh()->GetAnimClass() && (R->GetMesh()->GetAnimClass()->GetName().Contains(TEXT("ABP_Unarmed")) || R->GetMesh()->GetAnimClass()->IsChildOf(URunlineBodyAnimInstance::StaticClass())));
	TestTrue(TEXT("arms run ABP_FP_Copy"), Arms->GetAnimClass() && Arms->GetAnimClass()->GetName().Contains(TEXT("ABP_FP_Copy")));
	TestTrue(TEXT("arms hang off the body"), Arms->GetAttachParent() == R->GetMesh());
	TestTrue(TEXT("she doesn't see the body"), R->GetMesh()->bOwnerNoSee);
	TestTrue(TEXT("only she sees the arms"), Arms->bOnlyOwnerSee);
	return true;
}

// Review Important 4: crouched (or sliding) her eye drops; the body, and so the arms, drop with it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineRunnerBodyTracksEye, "Runline.Runner.BodyTracksTheEye",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineRunnerBodyTracksEye::RunTest(const FString&)
{
	FRunlineTestWorld TW;
	TW.AddBox(runline::Aabb::New({-50.0f, -1.0f, -50.0f}, {50.0f, 0.0f, 50.0f}), 0);
	ARunlineRunner* R = TW.World->SpawnActor<ARunlineRunner>();
	R->Runline().SpawnAt({0.0f, 0.0f, 0.0f}, 0.0f);
	R->Runline().PressCrouch();
	R->Runline().SetCrouchHeld(true);
	for (int32 I = 0; I < 60; ++I) R->Runline().Advance(1.0f / 60.0f);
	R->UpdateBodyPlacement();
	const runline::Controller& C = R->Runline().Core();
	TestTrue(TEXT("crouched"), C.crouched);
	const float Standing = C.tuning.standHeight - C.tuning.eyeFromTop;
	const float Drop = (C.GetView().eye.y - C.feet.y) - Standing;
	TestTrue(TEXT("her eye dropped"), Drop < -0.3f);
	const double WantZ = RunlineUnits::ToUnrealPosition(C.feet).Z + Drop * 100.0;
	TestEqual(TEXT("the body's root sits that much below her feet"), R->GetMesh()->GetComponentLocation().Z, WantZ, 0.5);
	return true;
}

// Review Important 3: the settings are saved when the runner goes away (Quit, map change), not
// only when the menu closes.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineRunnerSavesOnEnd, "Runline.Runner.SavesSettingsOnEndPlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineRunnerSavesOnEnd::RunTest(const FString&)
{
	URunlineSettings& S = URunlineSettings::Get();
	const float Old = S.MouseSensitivity;
	{
		FRunlineTestWorld TW;
		ARunlineRunner* R = TW.World->SpawnActor<ARunlineRunner>();
		S.MouseSensitivity = 3.25f;
		R->EndPlay(EEndPlayReason::Quit);   // what quitting does (this bare world never began play, so Destroy wouldn't)
	}
	URunlineSettings* Fresh = NewObject<URunlineSettings>();
	Fresh->LoadConfig();
	TestEqual(TEXT("saved"), Fresh->MouseSensitivity, 3.25f);
	S.MouseSensitivity = Old;
	S.Save();
	return true;
}

// The arms (via the body they hang off) are interpolated like the camera, not left on the capsule's
// 60 Hz steps: between steps they keep a fixed offset from the rendered eye, so at high frame rates
// they don't hop against a camera that glides.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineRunnerArmsSmooth, "Runline.Runner.ArmsMoveWithTheCamera",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineRunnerArmsSmooth::RunTest(const FString&)
{
	FRunlineTestWorld TW;
	TW.AddBox(runline::Aabb::New({-50.0f, -1.0f, -50.0f}, {50.0f, 0.0f, 50.0f}), 0);
	ARunlineRunner* R = TW.World->SpawnActor<ARunlineRunner>();
	R->Runline().SpawnAt({0.0f, 0.0f, 0.0f}, 0.0f);
	R->Runline().SetMove(FVector2D(0.0, 1.0));
	for (int32 F = 0; F < 120; ++F) R->Runline().Advance(1.0f / 60.0f);   // up to speed
	double Worst = 0.0;
	FVector Last = FVector::ZeroVector;
	for (int32 F = 0; F < 144; ++F)
	{
		R->Runline().Advance(1.0f / 144.0f);
		R->UpdateBodyPlacement();
		const FVector Offset = R->GetMesh()->GetComponentLocation() - R->Runline().GetRenderBaseEye();
		if (F > 0) Worst = FMath::Max(Worst, (Offset - Last).Size());
		Last = Offset;
	}
	TestTrue(FString::Printf(TEXT("body keeps its place under the rendered eye (worst hop %.2f cm)"), Worst), Worst < 0.5);
	return true;
}

// Runline plays 25% faster than Mirror's Edge (user's call, 2026-10-10): the runner sets it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineRunnerGameSpeed, "Runline.Runner.PlaysAtRunlineSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineRunnerGameSpeed::RunTest(const FString&)
{
	FRunlineTestWorld TW;
	ARunlineRunner* R = TW.World->SpawnActor<ARunlineRunner>();
	TestEqual(TEXT("game speed"), R->Runline().GameSpeed, 1.25f);
	return true;
}

// Review Critical 1: the time trial runs again and again. The finish sits inside the last
// checkpoint's region; after finishing, she must not re-take it, and a respawn puts her at the
// start with a clock that starts again.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineRunnerTrialAgain, "Runline.Runner.TimeTrialRunsAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineRunnerTrialAgain::RunTest(const FString&)
{
	FRunlineTestWorld TW;
	FRunlineCourseData Data;
	FRunlineCourseData::Load(TEXT("rooftops"), Data);
	RunlineCourses::Spawn(TW.World, Data, false);
	ARunlineRunner* R = TW.World->SpawnActor<ARunlineRunner>();
	R->FindCourse();
	runline::Controller& C = R->Runline().MutableCore();
	const auto PutAt = [&](runline::Vec3 P) { C.feet = P; C.state = runline::Ground{}; };
	const runline::Aabb Fin = *Data.Finish;
	const runline::Vec3 InFinish = (Fin.min + Fin.max) * 0.5f;
	for (int32 Run = 0; Run < 2; ++Run)
	{
		PutAt({0.0f, 0.0f, -10.0f});   // out of the start, on the roof
		R->UpdateCourse(0.5f);
		TestTrue(FString::Printf(TEXT("run %d: the clock starts"), Run), R->Timer().Running.IsSet());
		PutAt(InFinish);
		R->UpdateCourse(0.5f);
		TestTrue(FString::Printf(TEXT("run %d: finished"), Run), R->Timer().Last.IsSet() && !R->Timer().Running.IsSet());
		R->UpdateCourse(0.5f);   // still standing in the finish (and in the last checkpoint)
		TestEqual(FString::Printf(TEXT("run %d: still on the start checkpoint"), Run), R->CurrentCheckpoint(), 0);
		R->RespawnHere();
		TestTrue(FString::Printf(TEXT("run %d: respawned at the start"), Run), R->Runline().Core().feet == Data.Checkpoints[0].Spawn);
	}
	// faith-runner's number keys: 3 puts her at the third checkpoint.
	R->RespawnAt(2);
	TestTrue(TEXT("key 3: checkpoint 3"), R->Runline().Core().feet == Data.Checkpoints[2].Spawn);
	return true;
}

// M3b: with the GASP clips the body runs the native anim instance; the legs mesh copies it, only
// she sees it, it casts no shadow, and everything from spine_03 up is hidden.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineRunnerBody, "Runline.Runner.BodyAndLegs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineRunnerBody::RunTest(const FString&)
{
	FRunlineTestWorld TW;
	ARunlineRunner* R = TW.World->SpawnActor<ARunlineRunner>();
	USkeletalMeshComponent* Legs = R->GetLegsMesh();
	TestNotNull(TEXT("legs"), Legs);
	if (!Legs) return false;
	TestTrue(TEXT("only she sees the legs"), Legs->bOnlyOwnerSee);
	TestFalse(TEXT("no shadow from the legs"), Legs->CastShadow);
	TestTrue(TEXT("the legs copy the body"), Legs->GetAttachParent() == R->GetMesh() && Legs->GetAnimClass() && Legs->GetAnimClass()->GetName().Contains(TEXT("ABP_FP_Copy")));
	TestTrue(TEXT("no torso, arms or head on the legs"), Legs->IsBoneHiddenByName(TEXT("spine_03")));
	const bool bClips = URunlineAnimSet::Get() && LoadObject<UAnimSequence>(nullptr, TEXT("/Game/ThirdParty/GASP/A_Run.A_Run"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!bClips) { AddWarning(TEXT("No GASP clips: the GASP half of this test skipped")); return true; }
	TestTrue(TEXT("the body runs the GASP anim instance"), R->GetMesh()->GetAnimInstance() && R->GetMesh()->GetAnimInstance()->IsA<URunlineBodyAnimInstance>());
	return true;
}

// Review Focus 1: no clips, no anim set: the body keeps ABP_Unarmed (a fresh clone).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineRunnerFallback, "Runline.Body.FallsBackWithoutClips",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineRunnerFallback::RunTest(const FString&)
{
	FRunlineTestWorld TW;
	ARunlineRunner::bIgnoreAnimSetForTests = true;
	ARunlineRunner* R = TW.World->SpawnActor<ARunlineRunner>();
	ARunlineRunner::bIgnoreAnimSetForTests = false;
	TestTrue(TEXT("ABP_Unarmed"), R->GetMesh()->GetAnimClass() && R->GetMesh()->GetAnimClass()->GetName().Contains(TEXT("ABP_Unarmed")));
	return true;
}

#endif
