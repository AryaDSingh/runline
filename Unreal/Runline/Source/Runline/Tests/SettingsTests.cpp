#include "Misc/AutomationTest.h"
#include "RunlineSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/// A settings object at the shipped defaults (a new one starts from the player's saved config).
	URunlineSettings* Defaults()
	{
		URunlineSettings* S = NewObject<URunlineSettings>();
		S->MouseSensitivity = 1.0f;
		S->PadSensitivity = 1.0f;
		S->bInvertMouseY = false;
		S->bInvertPadY = false;
		S->FieldOfView = 90.0f;
		S->CameraMotion = 1.0f;
		return S;
	}
}

// Review Focus 1: a hand-edited or broken config loads as sane values.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineSettingsClamp, "Runline.Settings.ClampsAndDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineSettingsClamp::RunTest(const FString&)
{
	URunlineSettings* S = Defaults();
	S->MouseSensitivity = 0.0f;
	S->PadSensitivity = std::numeric_limits<float>::quiet_NaN();
	S->FieldOfView = 500.0f;
	S->CameraMotion = 2.0f;
	S->Sanitize();
	TestEqual(TEXT("mouse sensitivity clamped up"), S->MouseSensitivity, 0.1f);
	TestEqual(TEXT("NaN pad sensitivity back to default"), S->PadSensitivity, 1.0f);
	TestEqual(TEXT("FOV clamped down"), S->FieldOfView, 120.0f);
	TestEqual(TEXT("camera motion clamped"), S->CameraMotion, 1.0f);
	S->MouseSensitivity = -3.0f;
	S->FieldOfView = 10.0f;
	S->CameraMotion = -1.0f;
	S->Sanitize();
	TestEqual(TEXT("negative sensitivity"), S->MouseSensitivity, 0.1f);
	TestEqual(TEXT("tiny FOV"), S->FieldOfView, 70.0f);
	TestEqual(TEXT("negative motion"), S->CameraMotion, 0.0f);
	return true;
}

// faith-runner: look = (-dx, -dy_down) * 0.0022 * sensitivity; Unreal's mouse Y is up-positive.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineSettingsMouse, "Runline.Settings.MouseLook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineSettingsMouse::RunTest(const FString&)
{
	URunlineSettings* S = Defaults();
	const FVector2D L = S->MouseLook(FVector2D(10.0, 5.0));
	TestEqual(TEXT("x: mouse right turns right (core yaw negative)"), L.X, -0.022, 1e-7);
	TestEqual(TEXT("y: mouse up looks up"), L.Y, 0.011, 1e-7);
	S->bInvertMouseY = true;
	TestEqual(TEXT("inverted y"), S->MouseLook(FVector2D(10.0, 5.0)).Y, -0.011, 1e-7);
	S->bInvertMouseY = false;
	S->MouseSensitivity = 2.0f;
	TestEqual(TEXT("sensitivity doubles it"), S->MouseLook(FVector2D(10.0, 5.0)).X, -0.044, 1e-7);
	return true;
}

// faith-runner's pad look: no look inside 0.12, then r * |r| at 3.2 rad/s.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineSettingsPad, "Runline.Settings.PadLook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineSettingsPad::RunTest(const FString&)
{
	URunlineSettings* S = Defaults();
	TestTrue(TEXT("inside the dead zone"), S->PadLook(FVector2D(0.1, 0.0), 0.1f).IsZero());
	const FVector2D L = S->PadLook(FVector2D(0.5, 0.0), 0.1f);
	TestEqual(TEXT("quadratic response"), L.X, -0.08, 1e-6);
	TestEqual(TEXT("no y"), L.Y, 0.0, 1e-9);
	TestEqual(TEXT("stick up looks up"), S->PadLook(FVector2D(0.0, 0.5), 0.1f).Y, 0.08, 1e-6);
	S->bInvertPadY = true;
	TestEqual(TEXT("inverted pad y"), S->PadLook(FVector2D(0.0, 0.5), 0.1f).Y, -0.08, 1e-6);
	TestTrue(TEXT("move: inside 0.15 is nothing"), URunlineSettings::PadMove(FVector2D(0.1, 0.1)).IsZero());
	TestEqual(TEXT("move: outside passes through"), URunlineSettings::PadMove(FVector2D(0.3, 0.4)), FVector2D(0.3, 0.4));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineSettingsSave, "Runline.Settings.SaveLoadRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineSettingsSave::RunTest(const FString&)
{
	URunlineSettings& S = URunlineSettings::Get();
	const float OldMouse = S.MouseSensitivity, OldFov = S.FieldOfView;
	const bool OldInvert = S.bInvertMouseY;
	S.MouseSensitivity = 2.5f;
	S.FieldOfView = 100.0f;
	S.bInvertMouseY = true;
	S.Save();
	URunlineSettings* Fresh = NewObject<URunlineSettings>();
	Fresh->LoadConfig();
	TestEqual(TEXT("mouse saved"), Fresh->MouseSensitivity, 2.5f);
	TestEqual(TEXT("FOV saved"), Fresh->FieldOfView, 100.0f);
	TestTrue(TEXT("invert saved"), Fresh->bInvertMouseY);
	S.MouseSensitivity = OldMouse;
	S.FieldOfView = OldFov;
	S.bInvertMouseY = OldInvert;
	S.Save();
	return true;
}

// Review Important 6: whatever the config loaded into the settings in use, Get() hands back sane
// values (the load path itself is the engine's).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunlineSettingsGetSane, "Runline.Settings.GetIsAlwaysSane",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunlineSettingsGetSane::RunTest(const FString&)
{
	URunlineSettings* Cdo = GetMutableDefault<URunlineSettings>();
	const float Mouse = Cdo->MouseSensitivity, Fov = Cdo->FieldOfView;
	Cdo->MouseSensitivity = 0.0f;   // as a hand-edited GameUserSettings.ini would load
	Cdo->FieldOfView = 500.0f;
	TestEqual(TEXT("mouse"), URunlineSettings::Get().MouseSensitivity, 0.1f);
	TestEqual(TEXT("FOV"), URunlineSettings::Get().FieldOfView, 120.0f);
	Cdo->MouseSensitivity = Mouse;
	Cdo->FieldOfView = Fov;
	return true;
}

#endif
