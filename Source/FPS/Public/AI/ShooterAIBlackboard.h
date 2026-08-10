// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AI/ShooterAITypes.h"
#include "ShooterAIBlackboard.generated.h"

class AShooterAIController;
class AShooterCharacter;

/**
 * The bot's single shared knowledge store.
 *
 * Adapted from Game AI Pro 3 ch.10. Every AI module - the controller's action selection, the aim layer, the
 * movement-tech layer - reads the world through this one object and never through each other. That is what
 * keeps three systems that all care about "where is the player" from each holding three subtly different
 * answers, which is most of what made the old bot look incoherent.
 *
 * Two kinds of value live here, and the distinction matters:
 *
 *  - **Sampled** values are written once per perception update: line of sight, last known position, the
 *    smoothed stillness metric. They are sampled rather than queried because they are either expensive
 *    (a trace) or historical (an average) and must not change mid-frame.
 *  - **Function-based** values are recomputed on every call and cached nowhere: GetDistanceToTarget,
 *    GetAngleToTargetDegrees, GetPredictedTargetLocation. The chapter's `angle_to_player` is exactly this.
 *    Caching them is how a decision layer ends up acting on a number that was true two frames ago.
 *
 * It also owns the interrupt queue. Interrupts are raised from anywhere (perception transitions here, the
 * damage delegate on the controller) and consumed exactly once by the action-selection pass.
 *
 * Server-only, like everything else under AI/: it is a default subobject of AShooterAIController, and an
 * AI controller only ever exists on the authority. Nothing here replicates and nothing should.
 *
 * Ticked by hand from AShooterAIController::Tick so the within-frame order is fixed.
 */
UCLASS()
class FPS_API UShooterAIBlackboard : public UActorComponent
{
	GENERATED_BODY()

public:
	UShooterAIBlackboard();

	void TickBlackboard(float DeltaTime);

	/** Wipes everything about the previous life. Called on possess/unpossess - a respawn reuses the controller. */
	void ResetKnowledge();

	// --- Sampled knowledge ------------------------------------------------------------------------

	APawn* GetTargetPawn() const { return TargetPawn.Get(); }
	bool HasTarget() const;

	/**
	 * True while the bot has an unbroken view of its target, after the loss debounce.
	 *
	 * Debounced by LineOfSightGraceTime rather than reported raw: a raw eye-to-eye trace against a player
	 * strafing behind a railing flickers every frame, and sprint, ADS and the trigger all keyed off it - so
	 * the bot visibly stuttered. Raw is still available as HasRawLineOfSight for the things that genuinely
	 * need this frame's answer.
	 */
	bool HasLineOfSight() const { return bLineOfSightHeld; }

	/** This frame's untouched trace result. Only perception and the tactical-destination search want this. */
	bool HasRawLineOfSight() const { return bRawLineOfSight; }

	/**
	 * True once line of sight has been held for the stillness-scaled reaction time. This - not
	 * HasLineOfSight - is what the trigger is allowed to act on, and the split is the bot's reaction time.
	 */
	bool HasAcquiredTarget() const { return bTargetAcquired; }

	/**
	 * True while the target is worth keeping the gun pointed at: visible now, or seen inside TargetMemoryTime.
	 *
	 * This is the condition that owns the "never turn your back on the player" rule. The old bot only kept
	 * its aim on a remembered position while its state happened to be Hunt, so retreating or repositioning
	 * behind cover made it face its route instead of the player.
	 */
	bool IsTargetLive() const;

	FVector GetLastKnownTargetLocation() const { return LastKnownTargetLocation; }
	FVector GetLastKnownTargetEyeLocation() const { return LastKnownTargetEyeLocation; }
	FVector GetLastKnownTargetVelocity() const { return LastKnownTargetVelocity; }
	float GetTimeSinceLineOfSight() const { return TimeSinceLineOfSight; }

	/**
	 * How still the target is, 0 (fast and using movement tech) to 1 (perfectly stationary).
	 *
	 * The core mechanic: this is what the bot's accuracy, turn rate and reaction time are all functions of,
	 * so standing still is punished and sliding, jumping and wall running are the player's defence. Smoothed
	 * asymmetrically - slow to rise, fast to fall - so moving again pays off immediately and legibly.
	 */
	float GetTargetStillness() const { return SmoothedStillness; }

	/** Raw, unsmoothed stillness for this frame. Debug only - decisions must use the smoothed value. */
	float GetInstantTargetStillness() const { return InstantStillness; }

	// --- Function-based knowledge (recomputed per call, never cached) ------------------------------

	/** Distance from the bot's eye to the target's eye. Large sentinel when there is no target. */
	float GetDistanceToTarget() const;

	/** Degrees between where the bot is looking and where the target actually is. */
	float GetAngleToTargetDegrees() const;

	/** Degrees between the bot's facing and the direction it would have to look to see the last known spot. */
	float GetAngleToLastKnownDegrees() const;

	/** Where the target's head will be LeadSeconds from now, from the last observed velocity. */
	FVector GetPredictedTargetEyeLocation(float LeadSeconds) const;

	/** The bot's own health, normalised. 1 when it has no health component. */
	float GetSelfHealthFraction() const;

	/** Rounds in the equipped mag, and whether a reload is possible at all. */
	int32 GetMagAmmo() const;
	bool CanReload() const;

	// --- Interrupts -------------------------------------------------------------------------------

	/** Raises an interrupt. Only the highest-priority one raised before the next consume survives. */
	void RaiseInterrupt(EShooterAIInterrupt Interrupt);

	/**
	 * Records a round landing on the bot and raises the TookDamage interrupt.
	 *
	 * Does two things RaiseInterrupt alone cannot:
	 *
	 *  1. **Rate limits.** An automatic weapon lands a round every FireTime, and one interrupt per round would
	 *     invalidate the current action several times a second - which is the commitment model switched off at
	 *     exactly the moment the bot most needs to hold a plan. DamageInterruptCooldown bounds it.
	 *  2. **Folds the shot into the knowledge block.** Being shot tells you roughly where from, so when the
	 *     round came from the known opponent this refreshes the last-known position and resets the sight
	 *     timer. That is what makes the target live again, which makes the aim layer turn onto an attacker
	 *     the bot never saw - the difference between reacting to being flanked and flinching at nothing.
	 */
	void RegisterDamageFrom(AActor* DamageInstigator);

	/** True when the last damage taken arrived while the bot had no line of sight. */
	bool WasLastDamageUnseen() const { return bLastDamageUnseen; }

	/** Reads and clears the pending interrupt. Called once per frame by action selection. */
	EShooterAIInterrupt ConsumeInterrupt();

	EShooterAIInterrupt PeekInterrupt() const { return PendingInterrupt; }

	/** The interrupt that caused the current action to be selected. Debug and drive-time context. */
	EShooterAIInterrupt GetLastConsumedInterrupt() const { return LastConsumedInterrupt; }

protected:
	/** How far the bot can see. Also caps the LOS trace, so it is a real perception limit, not just a filter. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Perception", meta = (ClampMin = "100.0"))
	float SightRange;

	/** Half-angle of the bot's forward vision cone, in degrees. Outside it the target is unseen. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Perception", meta = (ClampMin = "5.0", ClampMax = "180.0"))
	float SightHalfAngleDegrees;

	/**
	 * Gunfire heard within this range refreshes the shooter's position with no line of sight. In a 1v1 a
	 * fired shot is the loudest possible tell, and a bot that ignores being shot at from behind reads as
	 * broken rather than as easy. Set to 0 to make the bot sight-only.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Perception", meta = (ClampMin = "0.0"))
	float HearingRange;

	/** How often the bot re-checks who its opponent is. Cheap, but there is no reason to do it per frame. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Perception", meta = (ClampMin = "0.1"))
	float TargetRefreshInterval;

private:
	AShooterAIController* GetAIController() const;

	void RefreshTarget();
	void UpdateLineOfSight(float DeltaTime);
	void UpdateStillness(float DeltaTime);
	void UpdateInterruptTriggers();

	/** Eye-to-eye trace on the visibility channel. Both the pawn capsule and the character mesh ignore that
	 *  channel, so a clear shot registers as "no blocking hit" rather than as a hit on the target. */
	bool TraceLineOfSight(const APawn* Target) const;

	TWeakObjectPtr<APawn> TargetPawn;

	bool bRawLineOfSight;
	bool bLineOfSightHeld;
	bool bTargetAcquired;

	float LineOfSightHeldTime;
	float LineOfSightLostTime;
	float TimeSinceLineOfSight;
	float TargetRefreshTimer;

	FVector LastKnownTargetLocation;
	FVector LastKnownTargetEyeLocation;
	FVector LastKnownTargetVelocity;

	float InstantStillness;
	float SmoothedStillness;

	// --- Edge-detection state for the interrupt triggers ---
	bool bWasTargetLive;
	bool bWasTargetAcquired;
	bool bWasMagEmpty;
	bool bWasHealthCritical;

	bool bLastDamageUnseen;
	float DamageInterruptCooldownRemaining;

	EShooterAIInterrupt PendingInterrupt;
	EShooterAIInterrupt LastConsumedInterrupt;
};
