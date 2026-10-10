#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "RunlineSettings.generated.h"

/// The player's settings, saved to GameUserSettings.ini ([/Script/Runline.RunlineSettings]).
/// Every value is clamped on load and on save, so a hand-edited file can't break the game.
UCLASS(Config = GameUserSettings, ConfigDoNotCheckDefaults)
class URunlineSettings : public UObject
{
	GENERATED_BODY()

public:
	/// The settings in use (the class default object, loaded from config at startup).
	static URunlineSettings& Get();

	/// Multiplier on faith-runner's mouse look (0.0022 rad per count).
	UPROPERTY(Config) float MouseSensitivity = 1.0f;
	/// Multiplier on faith-runner's stick look (3.2 rad/s at full deflection).
	UPROPERTY(Config) float PadSensitivity = 1.0f;
	UPROPERTY(Config) bool bInvertMouseY = false;
	UPROPERTY(Config) bool bInvertPadY = false;
	/// Horizontal field of view, degrees (faith-runner's CameraFx fov_base).
	UPROPERTY(Config) float FieldOfView = 90.0f;
	/// Scales the camera's bob, sway, roll and shake (0 = none; FOV and the swan neck stay).
	UPROPERTY(Config) float CameraMotion = 1.0f;

	static constexpr float MouseRadiansPerCount = 0.0022f;
	static constexpr float PadRadiansPerSecond = 3.2f;

	/// Clamps every value into its range; NaN goes back to the default.
	void Sanitize();
	/// Sanitize, then write GameUserSettings.ini.
	void Save();

	/// Look in core radians (x positive turns left, y positive looks up) for a raw mouse delta in
	/// counts (Unreal: x right, y up).
	FVector2D MouseLook(FVector2D RawDelta) const;
	/// Look for a stick reading over `DeltaSeconds`: faith-runner's pad response (nothing inside
	/// 0.12, then r * |r|).
	FVector2D PadLook(FVector2D Stick, float DeltaSeconds) const;
	/// The move from a stick: faith-runner's hard 0.15 dead zone, the raw value outside it.
	static FVector2D PadMove(FVector2D Stick);

	virtual void PostInitProperties() override;
};
