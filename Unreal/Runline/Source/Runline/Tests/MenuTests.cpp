#include "Misc/AutomationTest.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "RunlineSettings.h"
#include "RunlineTestRunner.h"
#include "RunlineTestWorld.h"
#include "SRunlineMenu.h"

#if WITH_DEV_AUTOMATION_TESTS

// Each control writes the player's settings, clamped.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineMenuHandlers, "Runline.Menu.HandlersWriteSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineMenuHandlers::RunTest(const FString&)
{
	URunlineSettings& S = URunlineSettings::Get();
	const float Mouse = S.MouseSensitivity, Pad = S.PadSensitivity, Fov = S.FieldOfView, Motion = S.CameraMotion;
	const bool InvM = S.bInvertMouseY, InvP = S.bInvertPadY;
	TSharedRef<SRunlineMenu> Menu = SNew(SRunlineMenu);
	Menu->SetMouseSensitivity(3.0f);
	Menu->SetPadSensitivity(0.01f);
	Menu->SetFieldOfView(200.0f);
	Menu->SetCameraMotion(0.25f);
	Menu->SetInvertMouseY(true);
	Menu->SetInvertPadY(true);
	TestEqual(TEXT("mouse"), S.MouseSensitivity, 3.0f);
	TestEqual(TEXT("pad clamped"), S.PadSensitivity, 0.1f);
	TestEqual(TEXT("FOV clamped"), S.FieldOfView, 120.0f);
	TestEqual(TEXT("motion"), S.CameraMotion, 0.25f);
	TestTrue(TEXT("invert mouse"), S.bInvertMouseY);
	TestTrue(TEXT("invert pad"), S.bInvertPadY);
	S.MouseSensitivity = Mouse; S.PadSensitivity = Pad; S.FieldOfView = Fov; S.CameraMotion = Motion;
	S.bInvertMouseY = InvM; S.bInvertPadY = InvP;
	return true;
}

// Review Focus 2: while the game is paused (the menu is open) the core doesn't step. Unreal
// doesn't tick components while paused unless they opt in (bTickEvenWhenPaused), and the first
// frame after resuming carries a normal delta, so this holds as long as the runner's movement
// never opts in; and whatever a resume frame carries, the hitch cap bounds the steps it runs.
// (Ticking a bare test world by hand ticks components only once, so the live pause is checked
// in the game: open the menu mid-fall, wait, resume.)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineMenuPause, "Runline.Menu.PauseFreezesTheCore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineMenuPause::RunTest(const FString&)
{
	FRunlineTestWorld TW;
	ARunlineTestRunner* Runner = TW.World->SpawnActor<ARunlineTestRunner>();
	TestFalse(TEXT("movement doesn't tick while paused"), Runner->Runline().PrimaryComponentTick.bTickEvenWhenPaused);
	TestFalse(TEXT("nor does the runner"), Runner->PrimaryActorTick.bTickEvenWhenPaused);
	Runner->Runline().SpawnAt({0.0f, 0.0f, 0.0f}, 0.0f);
	Runner->Runline().Advance(10.0f);   // a frame after a long pause, if one ever carried it
	TestEqual(TEXT("still at most MaxStepsPerFrame"), Runner->Runline().StepCount(), int64(URunlineMovementComponent::MaxStepsPerFrame));
	return true;
}

#endif
