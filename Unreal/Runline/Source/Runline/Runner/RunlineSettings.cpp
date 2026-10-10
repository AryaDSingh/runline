#include "RunlineSettings.h"

namespace
{
	float Sane(float V, float Lo, float Hi, float Default)
	{
		return FMath::IsNaN(V) ? Default : FMath::Clamp(V, Lo, Hi);
	}
}

URunlineSettings& URunlineSettings::Get()
{
	return *GetMutableDefault<URunlineSettings>();
}

void URunlineSettings::PostInitProperties()
{
	Super::PostInitProperties();
	Sanitize();
}

void URunlineSettings::Sanitize()
{
	MouseSensitivity = Sane(MouseSensitivity, 0.1f, 10.0f, 1.0f);
	PadSensitivity = Sane(PadSensitivity, 0.1f, 5.0f, 1.0f);
	FieldOfView = Sane(FieldOfView, 70.0f, 120.0f, 90.0f);
	CameraMotion = Sane(CameraMotion, 0.0f, 1.0f, 1.0f);
}

void URunlineSettings::Save()
{
	Sanitize();
	SaveConfig();
}

FVector2D URunlineSettings::MouseLook(FVector2D RawDelta) const
{
	// faith-runner: look = (-dx, -dy) * mouse_sens with Bevy's y pointing down; Unreal's points up.
	const double K = double(MouseRadiansPerCount) * MouseSensitivity;
	return FVector2D(-RawDelta.X * K, RawDelta.Y * K * (bInvertMouseY ? -1.0 : 1.0));
}

FVector2D URunlineSettings::PadLook(FVector2D Stick, float DeltaSeconds) const
{
	const double R = Stick.Size();
	if (R <= 0.12) return FVector2D::ZeroVector;
	const FVector2D Curved = Stick * R;
	const double K = double(PadRadiansPerSecond) * PadSensitivity * DeltaSeconds;
	return FVector2D(-Curved.X * K, Curved.Y * K * (bInvertPadY ? -1.0 : 1.0));
}

FVector2D URunlineSettings::PadMove(FVector2D Stick)
{
	return Stick.Size() > 0.15 ? Stick : FVector2D::ZeroVector;
}
