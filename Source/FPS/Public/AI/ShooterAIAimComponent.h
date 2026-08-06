// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShooterAIAimComponent.generated.h"

class AShooterAIController;
class AShooterCharacter;
class AWeapon;
class UCombatComponent;

/**
 * Where the bot is looking and when it pulls the trigger.
 *
 * The bot aims by writing the controller's control rotation - exactly the channel a human's mouse writes -
 * because AWeapon::WeaponTrace resolves its direction from the pawn's view rotation, which for a locally
 * controlled pawn is the control rotation. So there is no separate "AI aim" path in the weapon code: the bot
 * is genuinely pointing the gun, and every rule that applies to a player's shot applies to its shot.
 *
 * Everything that makes the bot beatable lives here, and all of it is a human limitation rather than a stat:
 *
 *  - it cannot turn faster than MaxTurnRateDegrees, so it can be flanked;
 *  - it aims at a deliberately stale target position, so it mis-tracks a strafing player;
 *  - it carries a random aim offset that only shrinks the longer it holds the same target, so first contact
 *    is inaccurate and a long duel is not;
 *  - it holds the trigger in bursts with rests between them, so there is always a window to push into;
 *  - it eats its own weapon's recoil, pushed onto the same control rotation, so sustained fire climbs for it
 *    exactly as it does for the player.
 *
 * Ticked explicitly from AShooterAIController::Tick rather than as a component tick, so perception, state and
 * aim always run in that order within a frame.
 */
UCLASS()
class FPS_API UShooterAIAimComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UShooterAIAimComponent();

	void TickAim(float DeltaTime);

	/** Drops the trigger and abandons the current burst. Called on state changes that must stop shooting. */
	void HoldFire();

	/**
	 * Asks the bot to face along a travel direction instead of at its target, keeping only its pitch on the
	 * target. This is the resolution of the one genuine conflict between aiming and the movement tech: a wall
	 * run will not start unless movement input points along the *capsule's* forward vector
	 * (WallRunMinForwardInputDot), and the capsule's yaw follows control rotation. A bot looking at its target
	 * while running past a wall therefore fails the test and can never wall run at all.
	 *
	 * The cost is honest and bounded: while yaw-locked the bot's aim is off-target, so its own
	 * FireAngleToleranceDegrees check refuses to fire. It trades shooting for traversal for the length of the
	 * run - which is the same trade the player makes, and the reason wall-running fire is penalised by
	 * AirborneSpreadMultiplier in the first place.
	 */
	void SetYawLockToTravel(bool bLock, const FVector& TravelDirection);

	/** Degrees between where the bot is looking and where its target is. Large while turning onto a target. */
	float GetAimErrorToTargetDegrees() const;

	/** Debug only: the world point the bot is currently trying to put its crosshair on. */
	FVector GetDebugAimPoint() const { return DebugAimPoint; }

	bool IsTriggerHeld() const { return bTriggerHeld; }

protected:
	/**
	 * How far past its preferred range the bot has to be engaging before it aims down sights. ADS costs it
	 * turn speed in exchange for AimSpreadMultiplier, so it is worth it at range and a liability up close -
	 * the same trade the player makes.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Aim", meta = (ClampMin = "0.0"))
	float AimDownSightsMinRange;

	/** Fraction of the weapon's view punch the bot suffers. 1 = exactly what the player gets. Below 1 only
	 *  as a last-resort difficulty lever; prefer changing aim error, which is more legible. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Aim", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ViewPunchScale;

	/** How quickly the bot pulls its aim back down after a shot, degrees per second. This is its recoil
	 *  control, and it is deliberately finite - a bot that recovers instantly does not have recoil. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Aim", meta = (ClampMin = "0.0"))
	float ViewPunchRecoveryDegreesPerSecond;

private:
	AShooterAIController* GetAIController() const;
	AShooterCharacter* GetShooterPawn() const;
	UCombatComponent* GetCombat() const;
	AWeapon* GetWeapon() const;

	/** The point the bot wants its crosshair on: target position, deliberately stale, partially lead-compensated. */
	bool ComputeAimPoint(FVector& OutAimPoint);

	void UpdateAimError(float DeltaTime);
	void UpdateRotation(float DeltaTime, const FVector& AimPoint, bool bHaveAimPoint);
	void UpdateTrigger(float DeltaTime);
	void UpdateAimDownSights();

	void PressTrigger();
	void ReleaseTrigger();

	/** Called after a round is confirmed to have left the gun. Adds this weapon's punch to the bot's aim. */
	void ApplyViewPunch();

	// --- Aim error state ---
	float TrackedTime;
	float ErrorRefreshTimer;
	FVector2D CurrentErrorDegrees;
	FVector2D TargetErrorDegrees;

	// --- Recoil the bot is fighting ---
	float AccumulatedPunchPitch;

	// --- Trigger state ---
	bool bTriggerHeld;

	/** Inside a burst. Tracked separately from bTriggerHeld because a semi-automatic weapon releases the
	 *  trigger after every round, so the held flag cannot mark the burst. */
	bool bBurstActive;
	bool bResting;
	float BurstTimer;

	/** Last observed mag count, used to spot rounds fired by UCombatComponent's own auto-fire loop. */
	int32 AmmoAtLastCheck;

	// --- Aiming down sights ---
	bool bAimingDownSights;

	// --- Yaw lock (wall running) ---
	bool bYawLocked;
	FVector LockedTravelDirection;

	FVector DebugAimPoint;
};
