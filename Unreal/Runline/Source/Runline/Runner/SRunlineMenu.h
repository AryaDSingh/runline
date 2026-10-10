#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/// The pause menu: Resume, the player's settings (sensitivity, invert Y, FOV, camera motion),
/// Respawn and Quit. Built in code, no widget assets. Changes apply at once; the runner saves
/// them when the menu closes.
class SRunlineMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRunlineMenu) {}
		SLATE_EVENT(FSimpleDelegate, OnResume)
		SLATE_EVENT(FSimpleDelegate, OnRespawn)
		SLATE_EVENT(FSimpleDelegate, OnQuit)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// What each control does; public so tests can drive them. Each writes URunlineSettings::Get()
	// and clamps.
	void SetMouseSensitivity(float V);
	void SetPadSensitivity(float V);
	void SetInvertMouseY(bool B);
	void SetInvertPadY(bool B);
	void SetFieldOfView(float V);
	void SetCameraMotion(float V);

	// Esc / pad Start close the menu (while it's open, Slate has the keyboard, not Enhanced Input).
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;

private:
	TSharedRef<SWidget> SliderRow(const FText& Label, float Min, float Max, TFunction<float()> Get, TFunction<void(float)> Set, TFunction<FText(float)> Show);
	TSharedRef<SWidget> CheckRow(const FText& Label, TFunction<bool()> Get, TFunction<void(bool)> Set);
	TSharedRef<SWidget> Button(const FText& Label, FSimpleDelegate OnClick);
	FSimpleDelegate OnResume;
};
