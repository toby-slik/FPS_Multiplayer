// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Interfaces/PlayerInterface.h"
#include "ShooterTypes/ShooterTypes.h"
#include "ShooterCharacter.generated.h"

class UHealthComponent;
class UCombatComponent;
class UCameraComponent;
class USpringArmComponent;
class UInputAction;
class UShooterMovementComponent;
class AWeapon;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWeaponFirstReplicated, AWeapon*, Weapon);

UCLASS()
class FPS_API AShooterCharacter : public ACharacter, public IPlayerInterface
{
	GENERATED_BODY()

	// Owns the predicted sprint/slide/wall run simulation and reads its tuning straight off this
	// class, so every FPS|Movement value stays where it already is on BP_ShooterCharacter.
	friend class UShooterMovementComponent;
	friend class FSavedMove_Shooter;

public:
	AShooterCharacter(const FObjectInitializer& ObjectInitializer);

	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;

	/** PlayerInterface */
	virtual FName GetWeaponAttachPoint_Implementation(const FGameplayTag& WeaponType) const override;
	virtual USkeletalMeshComponent* GetMesh1P_Implementation() const override;
	virtual USkeletalMeshComponent* GetMesh3P_Implementation() const override;
	virtual void WeaponReplicated_Implementation() override;
	virtual AWeapon* GetCurrentWeapon_Implementation() override;
	virtual bool IsSprinting_Implementation() const override;
	virtual void CancelSprint_Implementation() override;
	virtual bool IsSliding_Implementation() const override;
	virtual void CancelSlide_Implementation() override;
	virtual bool IsWallRunning_Implementation() const override;
	virtual int32 GetReserveAmmo_Implementation() const override;
	virtual void Notify_CycleWeapon_Implementation() override;
	virtual void Notify_ReloadWeapon_Implementation() override;
	virtual bool DoDamage_Implementation(float DamageAmount, AActor* DamageInstigator) override;
	virtual bool IsAlive_Implementation() const override;
	virtual TArray<FName> GetHeadshotBones_Implementation() const override;
	virtual void AddCameraShake_Implementation(float Amplitude, float Frequency, float Duration, TSubclassOf<UCameraShakeBase> ShakeClass) override;
	virtual bool IsMovingFasterThan_Implementation(float Speed) const override;
	virtual bool IsAirborne_Implementation() const override;
	virtual bool IsFirstPersonViewer_Implementation() const override;
	/** ~PlayerInterface */

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Applies the directional air jump. The wall jump lives in UShooterMovementComponent::DoJump. */
	virtual void OnJumped_Implementation() override;

	/** Lifts the base "can't jump while crouched" rule for the slide case only. */
	virtual bool CanJumpInternal_Implementation() const override;

	UFUNCTION(BlueprintPure, Category = "FPS|Movement")
	UShooterMovementComponent* GetShooterMovement() const { return ShooterMovement; }
	
	UFUNCTION(BlueprintCallable)
	FRotator GetFixedRotation() const;
	
	UPROPERTY(BlueprintReadOnly, Category = "FPS|FABRIK")
	FTransform FABRIK_SocketTransform;
	
	UFUNCTION(BlueprintCallable)
	bool HasCurrentWeapon() const;

	/**
	 * The FOV the equipped weapon aims down, with attachments applied, falling back to DefaultFOV when there
	 * is no weapon. This is the far end of the ADS blend, so it deliberately does not care whether the player
	 * is currently aiming - the blend alpha owns that.
	 *
	 * Drive the aim blend from this rather than reaching through Combat->CurrentWeapon in Blueprint. There are
	 * two ordinary moments with no weapon at all - between spawn and the inventory arriving, and from death
	 * until the respawned pawn is equipped - and a graph that reads the weapon directly logs an
	 * "Accessed None" every frame through both of them.
	 */
	UFUNCTION(BlueprintPure, Category = "FPS|Aiming")
	float GetWeaponAimFieldOfView() const;
	
	UPROPERTY(BlueprintAssignable)
	FWeaponFirstReplicated OnWeaponFirstReplicated;
	
	bool HasWeaponFirstReplicated() const { return bWeaponFirstReplicated; }
	
	UPROPERTY(EditDefaultsOnly, Category = "FPS|HitReact")
	TArray<TObjectPtr<UAnimMontage>> HitReacts;

	/**
	 * Bones whose physics bodies count as a headshot, on SKM_Manny / PA_Mannequin.
	 * "head" alone leaves a dead band under the chin: the head capsule only covers the skull, and the
	 * neck bodies fill everything between it and spine_05, so a visually perfect throat shot would pay
	 * body damage. Both neck bones are included for that reason. Remove neck_01 here if the lower neck
	 * ends up feeling too generous - it sits close to the collarbone.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "FPS|Damage")
	TArray<FName> HeadshotBones;
	
	UPROPERTY(EditDefaultsOnly, Category = "FPS|Respawn")
	float RespawnTime;

	/**
	 * Cancels any in-progress shake and settles the camera. Used on death.
	 *
	 * The shake itself is started through IPlayerInterface::AddCameraShake. It is implemented on the
	 * character rather than on UCombatComponent because the first-person camera's relative rotation has
	 * exactly one owner - see UpdateCameraOffsets. A second system calling SetRelativeRotation on it would
	 * silently stomp the wall-run roll on alternating frames.
	 */
	void ClearCameraShake();

protected:
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FPS|Mesh")
	TObjectPtr<USkeletalMeshComponent> Mesh1P;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FPS|Health")
	TObjectPtr<UHealthComponent> Health;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FPS|Combat")
	TObjectPtr<UCombatComponent> Combat;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FPS|Camera")
	TObjectPtr<UCameraComponent> FirstPersonCamera;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Aiming")
	float DefaultFOV;
	
	UFUNCTION(BlueprintImplementableEvent)
	void OnAim(bool bIsAiming);
	
	UPROPERTY(BlueprintReadOnly, Category = "FPS|TurnInPlace")
	float AO_Yaw;
	
	UPROPERTY(BlueprintReadOnly, Category = "FPS|Strafing")
	float MovementOffsetYaw;
	
	UPROPERTY(BlueprintReadOnly, Category = "FPS|TurnInPlace")
	ETurnInPlace TurningStatus;

	/**
	 * Anim-facing mirrors of UShooterMovementComponent's predicted state, refreshed each Tick.
	 * The movement component is the source of truth; these exist so ABP_FirstPerson /
	 * ABP_ThirdPerson keep working unchanged, and so simulated proxies (which never run the
	 * prediction) still have something to drive their state machines from.
	 */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "FPS|Movement")
	bool bSprinting;

	/** Drives the slide state in ABP_FirstPerson / ABP_ThirdPerson. Independent of bSprinting on purpose. */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "FPS|Movement")
	bool bSliding;

	/** Drives the wall run state in ABP_FirstPerson / ABP_ThirdPerson. Independent of bSprinting. */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "FPS|Movement")
	bool bWallRunning;

	/** Which side the wall is on. Anim BPs pick the left/right wall run pose from this. */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "FPS|Movement")
	EWallRunSide WallRunSide;

	// --- Sprint tuning ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float WalkSpeed;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float SprintSpeed;

	/** Sprint auto-cancels when the player has no movement input and has slowed below this. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float SprintStopSpeed;

	// --- Slide tuning ---

	/** Minimum ground speed required to enter a slide. Stops slide-on-the-spot the instant sprint is toggled. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float SlideMinStartSpeed;

	/** Speed the slide launches at. Should sit above SprintSpeed to feel like a burst. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float SlideLaunchSpeed;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float SlideDuration;

	/** Slide ends early once it decays below this speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float SlideEndSpeed;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float SlideGroundFriction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float SlideBrakingDeceleration;

	/** Minimum time between slides. Second half of the anti-spam guard. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float SlideCooldown;

	/**
	 * How long a crouch press made while airborne stays queued. Land inside this window with the
	 * normal slide conditions met and the slide fires immediately on touchdown. Set to 0 to disable.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float SlideInputBufferTime;

	// --- Jump tuning ---

	/** Total jumps before touching the ground again. 2 = one ground jump plus one air jump. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	int32 MaxJumpCount;

	/** Launch speed of the air jump. Kept separate from JumpZVelocity so the second jump can be tuned on its own. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float DoubleJumpZVelocity;

	/**
	 * How much of the existing horizontal momentum is rotated onto the movement input direction
	 * when the air jump fires. 1 = fully committed to the new direction (snappy, lets you reverse
	 * mid-air), 0 = momentum untouched (floaty). This is the main knob for directional control.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DoubleJumpRedirectAlpha;

	/** Flat extra speed added along the input direction on the air jump, on top of the redirect. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float DoubleJumpDirectionalBoost;

	/** Floor on the redirected speed, so an air jump from near-stationary still travels. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float DoubleJumpMinRedirectSpeed;

	/**
	 * Airborne steering authority. UE's stock 0.05 is almost none, which is what makes default
	 * jumps feel locked-in.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DefaultAirControl;

	// --- Wall run tuning ---

	/** Minimum horizontal speed to attach to a wall, and the speed the run ends at once it decays below it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float WallRunMinSpeed;

	/** Speed the run accelerates toward while forward input is held. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float WallRunSpeed;

	/** How quickly speed interps up to WallRunSpeed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float WallRunAccelInterpSpeed;

	/** Speed lost per second when forward input is released. This is what makes the speed-floor exit reachable. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float WallRunDeceleration;

	/** GravityScale while attached. Low but non-zero so the run visibly sinks. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float WallRunGravityScale;

	/** Downward speed is clamped to this while attached, so long runs don't build fall speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float WallRunMaxFallSpeed;

	/**
	 * UNUSED since the wall run became a real movement mode - PhysWallRun writes velocity
	 * directly, so there is no air control stage to scale. Kept so the value on
	 * BP_ShooterCharacter isn't lost; safe to delete once you're happy with the feel.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float WallRunAirControl;

	/** Hard time limit on a single wall run. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float WallRunMaxDuration;

	/** Sideways trace length from the capsule centre when looking for a wall. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float WallRunTraceDistance;

	/** A surface only counts as a wall if |ImpactNormal.Z| is below this. Keeps ramps and ceilings out. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float WallRunMaxWallNormalZ;

	/** Required clearance below the capsule to attach, so you can't wall run while grazing the floor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float WallRunMinGroundClearance;

	/** Can't attach while still rising faster than this - stops instant attach on the way up. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float WallRunMaxStartVerticalSpeed;

	/** Movement input must point this much along the character's forward vector to attach. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float WallRunMinForwardInputDot;

	/** Constant velocity pushed into the wall each tick to keep the capsule glued to it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float WallRunStickSpeed;

	/** Dominant component of the wall jump: along the player's direction of travel. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float WallJumpForwardSpeed;

	/** Smaller outward (along the wall normal) component, just enough to clear the surface. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float WallJumpOutwardSpeed;

	/** Upward component of the wall jump. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float WallJumpUpwardSpeed;

	/** Minimum time before attaching to any wall again. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float WallRunCooldown;

	/** Longer guard against re-attaching to the wall just left. Stops climbing one wall forever. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float WallRunSameWallCooldown;

	/** Camera roll while attached, in degrees. Negate if the tilt feels inverted. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float WallRunCameraRoll;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float WallRunCameraRollInterpSpeed;
	
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_HitReact(int32 MontageIndex);
	
	UFUNCTION()
	void OnDeathStarted();
	
	UFUNCTION(BlueprintImplementableEvent)
	void DeathEffects();

private:

	void Input_CycleWeapon();
	void Input_ReloadWeapon();
	void Input_FireWeapon_Pressed();
	void Input_FireWeapon_Released();
	void Input_Aim_Pressed();
	void Input_Aim_Released();
	void Input_Sprint_Toggle();
	void Input_Slide_Pressed();

	/** Copies the movement component's predicted state onto the replicated anim mirrors. */
	void UpdateMovementState();

	/**
	 * The one and only writer of FirstPersonCamera's relative rotation. Composes the wall-run roll and the
	 * weapon-fire shake into a single write so the two can never stomp each other.
	 */
	void UpdateCameraOffsets(float DeltaTime);
	float CurrentCameraRoll;

	/** Advances the shake clock and returns the rotation it contributes this frame. */
	FRotator AdvanceCameraShake(float DeltaTime);

	float CameraShakeAmplitude;
	float CameraShakeFrequency;
	float CameraShakeTotalDuration;
	float CameraShakeTimeRemaining;

	/**
	 * Randomised per shot so consecutive rounds never shake along the same path. Without this a held burst
	 * at a fixed frequency reads as a single smooth oscillation rather than as repeated impacts.
	 */
	float CameraShakePhasePitch;
	float CameraShakePhaseYaw;
	float CameraShakePhaseRoll;

	/** Cached from GetCharacterMovement(); the class is swapped in via the FObjectInitializer. */
	UPROPERTY()
	TObjectPtr<UShooterMovementComponent> ShooterMovement;

	void CalculateFABRIKSocketTransform();
	void CalculateTurnInPlaceParameters(float DeltaTime);
	void TurnInPlace(float DeltaTime);
	
	bool bWeaponFirstReplicated;
	FRotator StartingAimRotation;
	float InterpAO_Yaw;
	FTimerHandle DeathTimer;
	
	void DeathTimerFinished();
	

	
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USpringArmComponent> SpringArm;
	
	UPROPERTY(EditAnywhere, Category = "FPS|Input")
	TObjectPtr<UInputAction> CycleWeaponAction;
	
	UPROPERTY(EditAnywhere, Category = "FPS|Input")
	TObjectPtr<UInputAction> FireWeaponAction;
	
	UPROPERTY(EditAnywhere, Category = "FPS|Input")
	TObjectPtr<UInputAction> ReloadWeaponAction;
	
	UPROPERTY(EditAnywhere, Category = "FPS|Input")
	TObjectPtr<UInputAction> AimWeaponAction;

	UPROPERTY(EditAnywhere, Category = "FPS|Input")
	TObjectPtr<UInputAction> SprintAction;

	UPROPERTY(EditAnywhere, Category = "FPS|Input")
	TObjectPtr<UInputAction> SlideAction;
};
