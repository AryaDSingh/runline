#include "SRunlineMenu.h"
#include "RunlineSettings.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RunlineMenu"

namespace
{
	FSlateFontInfo Font(int32 Size, const char* Weight = "Regular") { return FCoreStyle::GetDefaultFontStyle(Weight, Size); }
}

void SRunlineMenu::SetMouseSensitivity(float V) { URunlineSettings::Get().MouseSensitivity = V; URunlineSettings::Get().Sanitize(); }
void SRunlineMenu::SetPadSensitivity(float V) { URunlineSettings::Get().PadSensitivity = V; URunlineSettings::Get().Sanitize(); }
void SRunlineMenu::SetInvertMouseY(bool B) { URunlineSettings::Get().bInvertMouseY = B; }
void SRunlineMenu::SetInvertPadY(bool B) { URunlineSettings::Get().bInvertPadY = B; }
void SRunlineMenu::SetFieldOfView(float V) { URunlineSettings::Get().FieldOfView = V; URunlineSettings::Get().Sanitize(); }
void SRunlineMenu::SetCameraMotion(float V) { URunlineSettings::Get().CameraMotion = V; URunlineSettings::Get().Sanitize(); }

TSharedRef<SWidget> SRunlineMenu::SliderRow(const FText& Label, float Min, float Max, TFunction<float()> Get, TFunction<void(float)> Set, TFunction<FText(float)> Show)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(Label).Font(Font(16))
		]
		+ SHorizontalBox::Slot().FillWidth(0.45f).VAlign(VAlign_Center).Padding(8.0f, 0.0f)
		[
			SNew(SSlider)
			.MinValue(Min)
			.MaxValue(Max)
			.Value_Lambda([Get] { return Get(); })
			.OnValueChanged_Lambda([Set](float V) { Set(V); })
		]
		+ SHorizontalBox::Slot().FillWidth(0.15f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(Font(16)).Text_Lambda([Get, Show] { return Show(Get()); })
		];
}

TSharedRef<SWidget> SRunlineMenu::CheckRow(const FText& Label, TFunction<bool()> Get, TFunction<void(bool)> Set)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(Label).Font(Font(16))
		]
		+ SHorizontalBox::Slot().FillWidth(0.6f).VAlign(VAlign_Center).Padding(8.0f, 0.0f)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([Get] { return Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([Set](ECheckBoxState S) { Set(S == ECheckBoxState::Checked); })
		];
}

TSharedRef<SWidget> SRunlineMenu::Button(const FText& Label, FSimpleDelegate OnClick)
{
	return SNew(SButton)
		.HAlign(HAlign_Center)
		.OnClicked_Lambda([OnClick] { OnClick.ExecuteIfBound(); return FReply::Handled(); })
		[
			SNew(STextBlock).Text(Label).Font(Font(18, "Bold"))
		];
}

FReply SRunlineMenu::OnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	if (KeyEvent.GetKey() == EKeys::Escape || KeyEvent.GetKey() == EKeys::Gamepad_Special_Right)
	{
		OnResume.ExecuteIfBound();
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(Geometry, KeyEvent);
}

void SRunlineMenu::Construct(const FArguments& InArgs)
{
	OnResume = InArgs._OnResume;
	URunlineSettings& S = URunlineSettings::Get();
	const auto Times = [](float V) { return FText::FromString(FString::Printf(TEXT("x%.2f"), V)); };
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	const auto Add = [&Rows](TSharedRef<SWidget> W) { Rows->AddSlot().AutoHeight().Padding(0.0f, 6.0f)[W]; };

	Add(SNew(STextBlock).Text(LOCTEXT("Paused", "PAUSED")).Font(Font(32, "Bold")).Justification(ETextJustify::Center));
	Add(SliderRow(LOCTEXT("Mouse", "Mouse sensitivity"), 0.1f, 10.0f, [&S] { return S.MouseSensitivity; }, [this](float V) { SetMouseSensitivity(V); }, Times));
	Add(CheckRow(LOCTEXT("InvertMouse", "Invert mouse Y"), [&S] { return S.bInvertMouseY; }, [this](bool B) { SetInvertMouseY(B); }));
	Add(SliderRow(LOCTEXT("Pad", "Controller sensitivity"), 0.1f, 5.0f, [&S] { return S.PadSensitivity; }, [this](float V) { SetPadSensitivity(V); }, Times));
	Add(CheckRow(LOCTEXT("InvertPad", "Invert controller Y"), [&S] { return S.bInvertPadY; }, [this](bool B) { SetInvertPadY(B); }));
	Add(SliderRow(LOCTEXT("Fov", "Field of view"), 70.0f, 120.0f, [&S] { return S.FieldOfView; }, [this](float V) { SetFieldOfView(V); },
		[](float V) { return FText::FromString(FString::Printf(TEXT("%.0f deg"), V)); }));
	Add(SliderRow(LOCTEXT("Motion", "Camera motion"), 0.0f, 1.0f, [&S] { return S.CameraMotion; }, [this](float V) { SetCameraMotion(V); },
		[](float V) { return FText::FromString(FString::Printf(TEXT("%.0f%%"), V * 100.0f)); }));
	Add(Button(LOCTEXT("Resume", "Resume"), InArgs._OnResume));
	Add(Button(LOCTEXT("Respawn", "Respawn at checkpoint"), InArgs._OnRespawn));
	Add(Button(LOCTEXT("Quit", "Quit to desktop"), InArgs._OnQuit));

	ChildSlot
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		SNew(SBox)
		.WidthOverride(720.0f)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.025f, 0.92f))
			.Padding(32.0f)
			[
				Rows
			]
		]
	];
}

#undef LOCTEXT_NAMESPACE
