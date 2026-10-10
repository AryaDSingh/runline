#pragma once
#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "RunlineCoreBridge.h"
#include "UnrealWorld.h"
#include <memory>
#include "RunlineMovementComponent.generated.h"

/// Faith's movement in Unreal: owns a runline::Controller and steps it at a fixed 60 Hz.
///
/// It is a UCharacterMovementComponent so the template's ACharacter (capsule, camera, input) works
/// unchanged, but none of CMC's physics runs: the movement mode stays MOVE_Custom and the tick
/// goes straight to the core. Input is latched between steps (a tap between two steps still
/// reaches the next one, once); look is fed to the first step of a frame and also shown at once,
/// so it never lags. The capsule sits at the real stepped feet; only the view is interpolated.
UCLASS(ClassGroup = Runline)
class URunlineMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	URunlineMovementComponent();
	virtual ~URunlineMovementComponent() override;

	static constexpr float StepSeconds = 1.0f / 60.0f;
	/// After a hitch, at most this many steps run in one frame; the rest of the time is dropped.
	static constexpr int32 MaxStepsPerFrame = 5;

	/// Put the runner at a core spawn point (also its respawn point).
	void SpawnAt(runline::Vec3 Feet, float Yaw);
	/// Change the respawn point without moving (checkpoints).
	void SetRespawn(runline::Vec3 Feet, float Yaw);
	void Respawn();

	// Input, latched until the next step takes it. Move: x right, y forward. Look: core radians
	// (x positive turns left, y positive looks up).
	void SetMove(FVector2D RightForward);
	void AddLook(FVector2D YawLeftPitchUp);
	void PressJump();
	void SetJumpHeld(bool bHeld);
	void PressCrouch();
	void SetCrouchHeld(bool bHeld);
	void PressTurn();

	/// Run `DeltaSeconds` of game time: whole steps, the rest carried over to the next frame.
	void Advance(float DeltaSeconds);

	/// The eye to render this frame (cm): between the last two steps.
	FVector GetRenderEye() const;
	/// The view to render this frame: the stepped view plus the look not yet stepped.
	FRotator GetRenderRotation() const;

	const runline::Controller& Core() const { return *Controller; }
	runline::Controller& MutableCore() { return *Controller; }
	int64 StepCount() const { return Steps; }
	float GetAccumulator() const { return Accumulator; }
	/// How many times a step reached code the core hasn't ported (and was turned into a respawn).
	int32 NotPortedCount() const { return NotPorted; }

	/// Called after every step with the controller and the input it took (events, audio, tests).
	TFunction<void(const runline::Controller&, const runline::Input&)> OnStep;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	runline::Input TakeInput();
	void StepOnce();
	void SyncCapsule();
	const runline::World& CoreWorld();

	std::unique_ptr<runline::Controller> Controller;
	std::unique_ptr<FUnrealWorld> World;
	float Accumulator = 0.0f;
	int64 Steps = 0;
	int32 NotPorted = 0;
	TSet<FString> NotPortedSeen;
	runline::Vec3 PrevEye{};

	FVector2D Move = FVector2D::ZeroVector;
	FVector2D PendingLook = FVector2D::ZeroVector;
	bool bJumpLatched = false;
	bool bJumpHeld = false;
	bool bCrouchLatched = false;
	bool bCrouchHeld = false;
	bool bTurnLatched = false;
};
