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
class RUNLINE_API URunlineMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	URunlineMovementComponent();
	virtual ~URunlineMovementComponent() override;

	static constexpr float StepSeconds = 1.0f / 60.0f;
	/// After a hitch, at most this many steps run in one frame; the rest of the time is dropped.
	static constexpr int32 MaxStepsPerFrame = 5;

	/// Game seconds per real second. The core still steps 1/60 s of game time (bit-exact with
	/// faith-runner); a higher speed just runs more steps a second. Look stays real-time.
	UPROPERTY(EditAnywhere, Category = "Runline", meta = (ClampMin = "0.25", ClampMax = "3.0"))
	float GameSpeed = 1.0f;

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

	/// The eye to render this frame (cm): between the last two steps, with the camera effects
	/// (scaled by the player's camera-motion setting) and the swan neck.
	FVector GetRenderEye() const;
	/// The same without the camera effects or swan neck: where the core's eye is, interpolated.
	FVector GetRenderBaseEye() const;
	/// Horizontal field of view to render (degrees): CameraFx's, between the last two steps.
	float GetRenderFov() const;
	/// The camera effects after the last step (faith-runner's CameraFx shot).
	const runline::Shot& LastShot() const { return Shot; }
	/// The view to render this frame: between the last two steps, plus the look not yet stepped
	/// that the next step will take.
	FRotator GetRenderRotation() const;

	const runline::Controller& Core() const { return *Controller; }
	runline::Controller& MutableCore() { return *Controller; }
	int64 StepCount() const { return Steps; }
	/// The move input the next step will take (x right, y forward).
	FVector2D GetMoveInput() const { return Move; }
	float GetAccumulator() const { return Accumulator; }
	/// How many times a step reached code the core hasn't ported (and was turned into a respawn).
	int32 NotPortedCount() const { return NotPorted; }
	/// Every event of the steps the last Advance ran, in order (the arms; a hitch runs several).
	const TArray<runline::Event>& GetFrameEvents() const { return FrameEvents; }
	/// How far the render is between the last step and the next (0..1).
	float GetStepAlpha() const { return Accumulator / StepSeconds; }
	/// How many times the view has been cut (spawn, respawn, death): a change means don't blend.
	int32 GetSnapCount() const { return SnapCount; }
	/// Dev only (?poses=1): no steps run; the next frame's events are DevEvents instead.
	bool bFrozen = false;
	TArray<runline::Event> DevEvents;

	/// Called after every step with the controller and the input it took (events, audio, tests).
	TFunction<void(const runline::Controller&, const runline::Input&)> OnStep;

	virtual void BeginPlay() override;
	/// CMC calls this on possession and restart; set the label directly (see UpdateAnimLabels).
	virtual void SetDefaultMovementMode() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	runline::Input TakeInput();
	void StepOnce();
	void SyncCapsule();
	/// What the template's anim BP reads (Velocity, Acceleration, IsFalling), from the core's state.
	/// MovementMode is assigned directly, never through SetMovementMode: its Walking mode runs
	/// CMC's floor adjustment, which can move the capsule.
	void UpdateAnimLabels();
	/// Start interpolating from where the core is now (after a respawn or teleport: no sweep).
	void SnapView();
	/// The effects that run after each step, as faith-runner's app runs them.
	void RunEffects(const runline::Input& In);
	const runline::World& CoreWorld();

	std::unique_ptr<runline::Controller> Controller;
	std::unique_ptr<FUnrealWorld> World;
	float Accumulator = 0.0f;
	int64 Steps = 0;
	int32 NotPorted = 0;
	TArray<runline::Event> FrameEvents;
	int32 SnapCount = 0;
	TSet<FString> NotPortedSeen;
	// The view at the start of the last step, with the look that step took: the render view
	// moves from here to the stepped view, so look is never delayed and core turns are smooth.
	runline::Vec3 PrevEye{};
	float PrevYaw = 0.0f;
	float PrevPitch = 0.0f;

	// faith-runner's camera layers (look.rs, camera.rs), stepped with the core. Their output is
	// kept as offsets from the core's view, so the render view can interpolate them like the eye.
	runline::LookLimiter Limiter;
	runline::CameraFx Fx;
	runline::SwanNeck Swan;
	runline::Shot Shot;
	struct FEffects
	{
		runline::Vec3 Eye{};
		float Yaw = 0.0f, Pitch = 0.0f, Roll = 0.0f, Fov = 90.0f;
		float SwanForward = 0.0f, SwanDown = 0.0f;
	};
	FEffects PrevFx, CurFx;

	FVector2D Move = FVector2D::ZeroVector;
	/// The move the last step took (for the anim BP's acceleration).
	runline::Vec2 LastMoveAxis{};
	FVector2D PendingLook = FVector2D::ZeroVector;
	bool bJumpLatched = false;
	bool bJumpHeld = false;
	bool bCrouchLatched = false;
	bool bCrouchHeld = false;
	bool bTurnLatched = false;
};
