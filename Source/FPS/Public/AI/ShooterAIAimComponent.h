// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AI/ShooterAITypes.h"
#include "ShooterAIAimComponent.generated.h"

class AShooterAIController;
class AShooterCharacter;
class AWeapon;
class UCombatComponent;
class UShooterAIBlackboard;

/**
 * Where the bot is looking and when it pulls the trigger.
 *
 * ---------------------------------------------------------------------------------------------------------
 * Independent of the locomotion action, on purpose
 *
 * Game AI Pro 3 ch.10 handles aiming and shooting as additive layers driven by their own small state machine,
 * influenced by the behaviour layer only through blackboard values - never *chosen* by it. This class is
 * that layer, and it has its own three-state machine (EShooterAimState) which nothing in
 * AShooterAIController::SelectAction can set.
 *
 * That decoupling is the fix for "the bot looks at me and then turns its back". The old version aimed at a
 * remembered position only while the tactical state happened to be Hunt, so retreating or repositioning
 * behind cover made the bot face its own route instead of the player. Now:
 *
 *  - Track is entered from one thing only: the blackboard says the target is live. While in Track the view
 *    is on the target and no other system may take it.
 *  - Search is what runs when there is genuinely nothing to look at.
 *  - Traverse is the single, explicit, time-bounded exception, and locomotion has to *ask* for it (see
 *    RequestTravelYawClaim). It is never entered as a side effect of moving.
 * ---------------------------------------------------------------------------------------------------------
 *
 * The bot aims by writing the controller's control rotation - exactly the channel a human's mouse writes -
 * because AWeapon::WeaponTrace resolves its direction from the pawn's view rotation, which for a locally
 * controlled pawn is the control rotation. So there is no separate "AI aim" path in the weapon code: the bot
 * is genuinely pointing the gun, and every rule that applies to a player's shot applies to its shot.
 *
 * Everything that makes the bot beatable lives here, and all of it is a human limitation rather than a stat:
 *
 *  - it cannot turn faster than MaxTurnRateDegrees, so it can be flanked;
 *  - its turn rate and its aim error are both functions of how *still* its target is, so a player who stops
 *    moving is punished and a player who slides, jumps and wall runs is not;
 *  - it aims at a deliberately stale target position, so it mis-tracks a strafing player;
 *  - it holds the trigger in bursts with rests between them, so there is always a window to push into;
 *  - it eats its own weapon's recoil, pushed onto the same control rotation.
 *
 * Ticked explicitly from AShooterAIController::Tick, last, so a traversal yaw claim raised by the movement
 * layer earlier in the frame is honoured on the same frame it is made.
 */
UCLASS()
class FPS_API UShooterAIAimComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UShooterAIAimComponent();

	void TickAim(float DeltaTime);

	/** Drops the trigger and abandons the current burst. */
	void HoldFire();

	/**
	 * Locomotion telling the aim layer to hold fire while it spends a beat on movement.
	 *
	 * The same arbitration shape as the yaw claim, and for the same reason. An evasive slide has to sprint to
	 * reach SlideMinStartSpeed, but UCombatComponent::Initiate_FireWeapon_Pressed cancels the owner's sprint,
	 * so a bot that kept shooting through the prime would cancel its own slide *and* drop every round it
	 * fired (Local_FireWeapon returns early on IsOwnerSprinting). Suppressing the trigger for the length of
	 * the burst makes it one deliberate choice - movement now, shooting in a moment - instead of two systems
	 * cancelling each other.
	 */
	void SetFireSuppressed(bool bSuppressed);

	EShooterAimState GetAimState() const { return AimState; }

	/**
	 * Locomotion asking to point the bot's yaw along a travel direction instead of at its target, for one
	 * frame. The claim must be renewed every frame it is wanted; a frame without a renewal drops it.
	 *
	 * This exists because of one hard conflict. UShooterMovementComponent::TryStartWallRun will not attach
	 * unless movement input points along the *capsule's* forward vector (WallRunMinForwardInputDot, 0.5), and
	 * the capsule's yaw follows the control rotation - so a bot looking at its target while running past a
	 * wall fails the test and can never wall run at all.
	 *
	 * The claim is a request rather than an order because "never turn your back on a live target" outranks
	 * "would like to wall run". With no live target it is always granted. With one it is granted only while
	 * both of these hold:
	 *
	 *  - the travel direction is within TraverseMaxLookAwayDegrees of the direction to the target, so the bot
	 *    is running *across or toward* the player rather than away from them, and
	 *  - the claim has been held for less than TraverseMaxLookAwayTime.
	 *
	 * bForce skips both tests and is for one case only: a wall run that has already attached. Breaking the
	 * yaw mid-run does not return the bot's aim to the player, it drops it off the wall - PhysWallRun derives
	 * its along-wall direction from the capsule's forward vector. That run is already bounded by the
	 * character's own WallRunMaxDuration.
	 *
	 * Returns true only if the claim was granted this frame.
	 */
	bool RequestTravelYawClaim(const FVector& TravelDirection, bool bForce);

	void ReleaseTravelYawClaim();
	bool HasTravelYawClaim() const { return bTravelYawClaimed; }

	/** True once the capsule's forward vector is within DotThreshold of the claimed travel direction. */
	bool IsTravelYawAligned(float DotThreshold) const;

	/** True when the bot has a shot it wants and its aim has arrived. Debug and the fire gate. */
	bool IsTriggerHeld() const { return bTriggerHeld; }

	void DrawAimDebug() const;

protected:
	/**
	 * How far past its preferred range the bot has to be before it aims down sights. ADS costs it turn speed
	 * in exchange for AimSpreadMultiplier, so it is worth it at range and a liability up close - the same
	 * trade the player makes.
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

	// --- Traversal yaw claim ----------------------------------------------------------------------

	/**
	 * Widest angle away from a live target the bot will accept in order to traverse.
	 *
	 * Not a cosmetic limit. Above roughly 90 degrees the bot is genuinely facing away from the player, which
	 * is the exact complaint this rework exists to fix. Kept under it so a granted claim always still has the
	 * player somewhere in front.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Aim", meta = (ClampMin = "0.0", ClampMax = "120.0"))
	float TraverseMaxLookAwayDegrees;

	/** Hardest cap on how long locomotion may hold the yaw while a target is live. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Aim", meta = (ClampMin = "0.1"))
	float TraverseMaxLookAwayTime;

	/**
	 * Turn-rate multiplier while a traversal claim is held.
	 *
	 * Above 1, and that is not a stat advantage: while the yaw is claimed the bot's aim is off its target, so
	 * its own FireAngleToleranceDegrees check refuses to fire for the whole claim. The multiplier buys
	 * traversal and can never buy accuracy. It has to be above 1 because the attach window is short - a wall
	 * run has to be lined up inside a few tenths of a second or the opportunity is gone, which is precisely
	 * why the old bot never wall ran.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Aim", meta = (ClampMin = "1.0", ClampMax = "6.0"))
	float TraverseTurnRateMultiplier;

private:
	AShooterAIController* GetAIController() const;
	AShooterCharacter* GetShooterPawn() const;
	UShooterAIBlackboard* GetBlackboard() const;
	UCombatComponent* GetCombat() const;
	AWeapon* GetWeapon() const;

	/** Resolves the aim state for this frame. Nothing outside this function may write AimState. */
	void UpdateAimState(float DeltaTime);

	/** The point the bot wants its crosshair on: target position, deliberately stale, partially lead-compensated. */
	bool ComputeLookPoint(FVector& OutLookPoint) const;

	void UpdateAimError(float DeltaTime);
	void UpdateRotation(float DeltaTime, const FVector& LookPoint, bool bHaveLookPoint);
	void UpdateTrigger(float DeltaTime);
	void UpdateAimDownSights();

	void PressTrigger();
	void ReleaseTrigger();

	/** Called after a round is confirmed to have left the gun. Adds this weapon's punch to the bot's aim. */
	void ApplyViewPunch();

	EShooterAimState AimState;

	// --- Aim error state ---
	float TrackedTime;
	float ErrorRefreshTimer;
	FVector2D CurrentErrorDegrees;
	FVector2D TargetErrorDegrees;

	/** Last evaluated error multiplier from target stillness. Cached for the debug readout only. */
	float DebugErrorScale;

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

	/** Set by the movement layer through the controller while it is spending a beat on movement tech. */
	bool bFireSuppressed;

	// --- Aiming down sights ---
	bool bAimingDownSights;

	// --- Traversal yaw claim ---
	bool bTravelYawClaimed;

	/** Set by RequestTravelYawClaim, cleared at the end of TickAim. A frame with no renewal drops the claim,
	 *  so a movement layer that stops asking can never leave the bot staring down a corridor. */
	bool bTravelYawRenewedThisFrame;

	FVector ClaimedTravelDirection;
	float TravelYawClaimElapsed;

	FVector DebugLookPoint;
};
