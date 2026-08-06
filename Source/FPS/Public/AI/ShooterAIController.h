// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "AI/ShooterAITypes.h"
#include "ShooterAIController.generated.h"

class AShooterCharacter;
class AWeapon;
class UCombatComponent;
class UShooterAIAimComponent;
class UShooterAIMovementTechComponent;
class UShooterMovementComponent;

/**
 * The enemy bot's brain. Server-only: an AI controller only ever exists on the authority, so nothing in this
 * class or its components should be reachable from a client, and none of it needs replicating - the pawn's
 * existing replicated state (position, bSprinting / bSliding / bWallRunning, CurrentWeapon) is all a client
 * needs to draw the result.
 *
 * ---------------------------------------------------------------------------------------------------------
 * Why there is no Behaviour Tree
 *
 * A BT would be the conventional choice, and the tactical layer here is written as a plain C++ state machine
 * instead. Two reasons, in order of weight:
 *
 *  1. A BT needs a BehaviourTree asset and a Blackboard asset, both binary .uasset files that have to be
 *     authored in the editor. Building the bot as a state machine means the entire AI works from a fresh
 *     compile with no asset authoring at all - which is the difference between "run it and see" and "follow
 *     nine editor steps first".
 *  2. A 1v1 bot has five mutually exclusive states and no task-tree structure worth speaking of. A BT's
 *     value is composition and reuse across many behaviours; at this size it is ceremony, and the decision
 *     logic ends up spread across a dozen tiny UBTTask_ classes plus a graph you cannot read in a diff.
 *
 * If the state list ever grows past roughly a dozen states, or if designer-authored behaviour becomes the
 * point, this is the class to port to a State Tree - the per-state Decide/Drive split below maps onto one
 * almost directly.
 * ---------------------------------------------------------------------------------------------------------
 *
 * Responsibilities are split three ways: this class owns perception, target memory and the state machine;
 * UShooterAIAimComponent owns where the bot is looking and when it pulls the trigger;
 * UShooterAIMovementTechComponent owns sprint / slide / jump / wall run. The two components read state from
 * here and never talk to each other.
 */
UCLASS()
class FPS_API AShooterAIController : public AAIController
{
	GENERATED_BODY()

public:
	AShooterAIController();

	virtual void Tick(float DeltaTime) override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

	// --- Shared state the components read ---------------------------------------------------------

	const FShooterBotDifficulty& GetDifficulty() const { return Difficulty; }

	EShooterBotState GetBotState() const { return BotState; }

	/** The pawn this bot is fighting. Null when it has never seen anyone. */
	APawn* GetTargetPawn() const { return TargetPawn.Get(); }

	/** True only while an unbroken line of sight exists right now. Ignores the reaction delay. */
	bool HasLineOfSight() const { return bHasLineOfSight; }

	/**
	 * True once line of sight has been held for at least ReactionTime. This - not HasLineOfSight - is what
	 * the aim and fire logic is allowed to act on, and the split is the whole of the bot's reaction time.
	 */
	bool HasAcquiredTarget() const { return bTargetAcquired; }

	/** Where the target was last seen. Valid whenever GetTargetPawn() is non-null. */
	FVector GetLastKnownTargetLocation() const { return LastKnownTargetLocation; }

	/** Target velocity sampled at the last frame line of sight was held. Used for aim leading. */
	FVector GetLastKnownTargetVelocity() const { return LastKnownTargetVelocity; }

	/** Seconds since line of sight was last held. Compare against Difficulty.TargetMemoryTime to decide
	 *  whether the last known position is still worth acting on. */
	float GetTimeSinceLineOfSight() const { return TimeSinceLineOfSight; }

	/** The bot's own eye position - the origin AWeapon::WeaponTrace will fire from. */
	FVector GetEyeLocation() const;

	AShooterCharacter* GetShooterPawn() const;
	UCombatComponent* GetCombat() const;
	UShooterMovementComponent* GetShooterMovement() const;

	/** Where the bot is currently trying to get to. Movement tech reads this to decide about sliding. */
	FVector GetMoveGoal() const { return MoveGoal; }
	bool HasMoveGoal() const { return bHasMoveGoal; }

	/** Range the bot tries to hold, adjusted for the equipped weapon's fire type. */
	float GetPreferredRange() const;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Debug")
	bool bDebugDrawAI;

protected:
	virtual void BeginPlay() override;

	/** Pick a preset on the Blueprint and the numbers arrive with it. Set to Custom to hand-author them. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Difficulty")
	EShooterBotSkill Skill;

	/**
	 * Applied at BeginPlay from Skill unless Skill is Custom. Editable so a preset can be dialled in place -
	 * but note that changing Skill away from Custom overwrites whatever is here.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Difficulty")
	FShooterBotDifficulty Difficulty;

	/** How far the bot can see. Also caps the LOS trace, so it is a real perception limit, not just a filter. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Perception", meta = (ClampMin = "100.0"))
	float SightRange;

	/** Half-angle of the bot's forward vision cone, in degrees. Outside it the target is unseen. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Perception", meta = (ClampMin = "5.0", ClampMax = "180.0"))
	float SightHalfAngleDegrees;

	/**
	 * Gunfire heard within this range acquires the shooter's position even with no line of sight. In a 1v1
	 * a fired shot is the loudest possible tell, and a bot that ignores being shot at from behind reads as
	 * broken rather than as easy. Set to 0 to make the bot sight-only.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Perception", meta = (ClampMin = "0.0"))
	float HearingRange;

	/** How often the bot re-checks who its opponent is. Cheap, but there is no reason to do it per frame. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Perception", meta = (ClampMin = "0.1"))
	float TargetRefreshInterval;

	/** Radius the bot searches within when picking a reposition or retreat destination. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Tactics", meta = (ClampMin = "100.0"))
	float RepositionSearchRadius;

	/** How close counts as "arrived" for a reposition or hunt goal. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Tactics", meta = (ClampMin = "10.0"))
	float GoalAcceptanceRadius;

	/** Longest a reload may take before the watchdog forces completion. Must sit above the slowest authored
	 *  reload montage or it will cut reloads short. See CompleteReloadIfStalled for why this exists. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Combat", meta = (ClampMin = "0.2"))
	float ReloadWatchdogTime;

	/** How often a hunting bot picks a fresh search destination even if it has not arrived yet. Without this
	 *  a hunt only re-evaluates on arrival, so an unreachable goal parks the bot. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Tactics", meta = (ClampMin = "0.2"))
	float HuntRepathInterval;

	/**
	 * Longest any state may pursue one move goal before it is abandoned and the state machine re-decides.
	 *
	 * This is a deliberate backstop against a whole class of bug rather than one instance of it: a state that
	 * latches on "I still have a goal I have not reached" stops responding to anything if the goal turns out
	 * to be unreachable - partial paths, a nav link the capsule cannot take, geometry that moved. A bot stood
	 * still is the single worst failure mode here, because it looks exactly like broken AI.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Tactics", meta = (ClampMin = "0.5"))
	float MoveGoalTimeout;

	/**
	 * When the bot has lost its target and no visible-from candidate position can be found, head toward the
	 * target's actual current position rather than standing still.
	 *
	 * This is knowingly a small piece of omniscience, and it is a playability decision, not an oversight: the
	 * alternative is a bot that can be permanently lost by walking round one corner, which in a 1v1 practice
	 * opponent is worse than a bot that walks your way. Its reaction time, turn rate and aim error all still
	 * apply on arrival, so it does not get a free kill out of it - it only gets to find you. Turn off for a
	 * strictly fair bot that can be genuinely evaded.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Tactics")
	bool bSearchTowardTargetWhenLost;

private:
	UPROPERTY()
	TObjectPtr<UShooterAIAimComponent> AimLogic;

	UPROPERTY()
	TObjectPtr<UShooterAIMovementTechComponent> MovementTech;

	// --- Perception -------------------------------------------------------------------------------

	void RefreshTarget();
	void UpdatePerception(float DeltaTime);

	/** Eye-to-eye trace on the visibility channel. Both the pawn capsule and the character mesh ignore that
	 *  channel, so a clear shot registers as "no blocking hit" rather than as a hit on the target. */
	bool TraceLineOfSight(const APawn* Target) const;

	TWeakObjectPtr<APawn> TargetPawn;
	bool bHasLineOfSight;
	bool bTargetAcquired;
	float LineOfSightHeldTime;
	float TimeSinceLineOfSight;
	float TargetRefreshTimer;
	FVector LastKnownTargetLocation;
	FVector LastKnownTargetVelocity;

	// --- State machine ----------------------------------------------------------------------------

	void UpdateStateMachine(float DeltaTime);

	/** Chooses the state for this decision tick. Pure decision - issues no orders. */
	EShooterBotState DecideState() const;

	/** Called once on entry, so per-state setup (picking a destination, dropping the trigger) happens once. */
	void EnterState(EShooterBotState NewState);

	/** Called every frame for the active state. This is where move orders are issued. */
	void DriveState(float DeltaTime);

	void DriveEngage(float DeltaTime);
	void DriveHunt(float DeltaTime);
	void DriveReposition();
	void DriveRetreat();

	EShooterBotState BotState;
	float DecisionTimer;
	float StateTime;

	// --- Movement ---------------------------------------------------------------------------------

	/** Wraps MoveToLocation and records the goal so the movement-tech layer can reason about the approach. */
	void RequestMoveTo(const FVector& Location);
	void StopMoving();

	/** A navigable point roughly AwayFrom-relative-to-target, used for repositioning and retreating. */
	bool FindTacticalDestination(bool bBreakLineOfSight, FVector& OutLocation) const;

	/** Perpendicular strafe destination at the preferred range, alternating side on a timer. */
	FVector ComputeStrafeDestination() const;

	FVector MoveGoal;
	bool bHasMoveGoal;

	/** Time spent pursuing the current move goal, against MoveGoalTimeout. */
	float MoveGoalElapsed;

	/** Counts down to the next hunt re-path. */
	float HuntRepathTimer;

	float StrafeTimer;
	int32 StrafeSign;

	// --- Combat -----------------------------------------------------------------------------------

	/** Ammo/health housekeeping that is not the aim component's business: the tactical reload and the
	 *  watchdog that guarantees a bot's reload finishes even if the 3P montage carries no notify. */
	void UpdateWeaponHousekeeping(float DeltaTime);

	bool ShouldReloadNow() const;
	float HealthFraction() const;

	/**
	 * Reload completion for the bot is driven from here rather than from the animation.
	 *
	 * UCombatComponent::Notify_ReloadWeapon is called by a notify inside the *first-person* reload montage,
	 * and the bot reloads on its 3P mesh. If the third-person montage for a weapon carries no equivalent
	 * notify, the notify never lands, WeaponStatus stays Reloading forever and the bot never fires again.
	 * This timer is the belt to that braces: when it expires the reload is completed through exactly the
	 * same interface call the notify would have made, so an authored 3P notify and this path cannot
	 * double-refill (the second call finds the weapon already Idle).
	 */
	void CompleteReloadIfStalled();

	float ReloadWatchdogTimer;

	void DrawDebug() const;
};
