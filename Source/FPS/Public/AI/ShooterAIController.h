// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "AI/ShooterAITypes.h"
#include "ShooterAIController.generated.h"

class AShooterCharacter;
class AWeapon;
class UCombatComponent;
class UHealthComponent;
class UShooterAIAimComponent;
class UShooterAIBlackboard;
class UShooterAIMovementTechComponent;
class UShooterMovementComponent;

/**
 * The enemy bot's brain. Server-only: an AI controller only ever exists on the authority, so nothing in this
 * class or its components should be reachable from a client, and none of it needs replicating - the pawn's
 * existing replicated state (position, bSprinting / bSliding / bWallRunning, CurrentWeapon) is all a client
 * needs to draw the result.
 *
 * ---------------------------------------------------------------------------------------------------------
 * The decision model: commitment plus invalidation
 *
 * Adapted from Game AI Pro 3 ch.10, "From Behavior to Animation". The bot does NOT re-decide what it is doing
 * every frame, or even on a fixed interval. Instead:
 *
 *  1. Selection picks one long-lived **action** and records the set of **latched conditions** it was chosen
 *     under (EShooterAILatch). That is the chapter's `parallel` node, made explicit: the only part of the
 *     decision that gets re-examined is the part that had to be true for it to be the right decision.
 *  2. The action then runs, untouchable, for at least ActionMinCommitTime. Inside that window nothing but an
 *     interrupt can change the bot's mind.
 *  3. After that window the latches are checked each tick. One failure invalidates the action and selection
 *     runs again from the top. A max duration backstops anything that would otherwise latch forever.
 *  4. **Interrupts** (EShooterAIInterrupt) bypass the commitment window entirely and force an immediate
 *     re-selection. They are edge-triggered, live exactly one update, and resolve by a fixed priority list.
 *
 * That model is the direct fix for a bot that "flits between directions frame to frame". The old version
 * re-ran a full decision every 0.25s with no memory of having just made one, so any two states whose
 * conditions sat near a boundary swapped back and forth indefinitely - and every swap reset the strafe.
 *
 * Why there is no Behaviour Tree: a BT needs two binary .uasset files authored in the editor before the bot
 * does anything at all, and a 1v1 bot has six mutually exclusive actions and no task-tree structure worth
 * composing. Keeping it in C++ means the entire decision layer is diffable and works from a fresh compile.
 * If the action list ever grows past roughly a dozen, this is the class to port to a State Tree - the
 * Select/Enter/Drive split below maps onto one almost directly.
 * ---------------------------------------------------------------------------------------------------------
 *
 * Responsibilities are split four ways. UShooterAIBlackboard is the shared knowledge store and the only thing
 * that touches perception; this class owns action selection and move goals; UShooterAIAimComponent owns where
 * the bot looks and when it shoots; UShooterAIMovementTechComponent owns sprint / slide / jump / wall run.
 * The three components never talk to each other - they read the blackboard and route requests through here.
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

	UShooterAIBlackboard* GetKnowledge() const { return Knowledge; }

	EShooterBotAction GetAction() const { return CurrentAction; }

	/** Seconds the current action has been running. */
	float GetActionElapsed() const { return ActionElapsed; }

	// Convenience forwarders onto the blackboard. Kept on the controller because every module already has a
	// pointer to it, and because "the bot knows X" reads better than reaching through two objects for it.
	APawn* GetTargetPawn() const;
	bool HasLineOfSight() const;
	bool HasAcquiredTarget() const;
	bool IsTargetLive() const;
	float GetTargetStillness() const;
	FVector GetLastKnownTargetLocation() const;

	/** The bot's own eye position - the origin AWeapon::WeaponTrace will fire from. */
	FVector GetEyeLocation() const;

	AShooterCharacter* GetShooterPawn() const;
	UCombatComponent* GetCombat() const;
	UShooterMovementComponent* GetShooterMovement() const;

	/**
	 * Where the bot is currently trying to get to, when it is *pathing*.
	 *
	 * Only meaningful while HasMoveGoal() is true. In a fight the bot steers instead - see UpdateSteering -
	 * and there is deliberately no goal at all, because "pick a point, path to it, arrive, pick another" is
	 * what made the old bot read as point-and-click rather than as a player.
	 */
	FVector GetMoveGoal() const { return MoveGoal; }
	bool HasMoveGoal() const { return bHasMoveGoal; }
	float GetDistanceToMoveGoal() const;

	/** True while the bot is driving itself with continuous steering input instead of following a path. */
	bool IsSteering() const { return bSteering; }

	/** The smoothed world-space direction the steering layer is currently pushing. Zero when pathing. */
	FVector GetSteeringDirection() const { return bSteering ? SteeringDirection : FVector::ZeroVector; }

	/** Range the bot tries to hold, adjusted for the equipped weapon's fire type. */
	float GetPreferredRange() const;

	/**
	 * True when the bot has a shot it wants to take right now, ignoring whether its aim has arrived yet.
	 *
	 * Asked by the movement-tech layer, not just by the trigger. Sprinting blocks firing, and
	 * UCombatComponent::Initiate_FireWeapon_Pressed cancels the sprint on the shooter's behalf - so a
	 * movement layer that keeps re-asserting sprint intent while the aim layer keeps firing produces a
	 * per-frame tug of war over the sprint flag, which is visible as a stuttering run speed. Both layers read
	 * this one answer instead.
	 */
	bool WantsToShoot() const;

	// --- Yaw arbitration between locomotion and aim ------------------------------------------------

	/**
	 * Locomotion asking the aim layer to point the bot's yaw along a travel direction instead of at its
	 * target. Returns true only if the claim was granted.
	 *
	 * Routed through the controller rather than component-to-component so the two AI components keep their
	 * one-way dependency on this class. See UShooterAIAimComponent::RequestTravelYawClaim for the grant rules
	 * - the short version is that a live target can only be looked away from deliberately, briefly, and by a
	 * small angle, which is what stops "the bot turns its back on me" from being an emergent side effect.
	 */
	bool RequestTravelYawClaim(const FVector& TravelDirection, bool bForce);
	void ReleaseTravelYawClaim();
	bool HasTravelYawClaim() const;

	/** True once the capsule's forward vector is within DotThreshold of the claimed travel direction. */
	bool IsTravelYawAligned(float DotThreshold) const;

	/** Locomotion asking the aim layer to hold fire while it spends a beat on movement tech. Same arbitration
	 *  shape as the yaw claim - see UShooterAIAimComponent::SetFireSuppressed. */
	void SetFireSuppressed(bool bSuppressed);

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

	/** Radius the bot searches within when picking a reposition or retreat destination. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Tactics", meta = (ClampMin = "100.0"))
	float RepositionSearchRadius;

	/** How close counts as "arrived" for a move goal. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Tactics", meta = (ClampMin = "10.0"))
	float GoalAcceptanceRadius;

	/** Longest a reload may take before the watchdog forces completion. Must sit above the slowest authored
	 *  reload montage or it will cut reloads short. See CompleteReloadIfStalled for why this exists. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Combat", meta = (ClampMin = "0.2"))
	float ReloadWatchdogTime;

	/** How often a hunting bot picks a fresh search destination even if it has not arrived yet. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Tactics", meta = (ClampMin = "0.2"))
	float HuntRepathInterval;

	/** How often an approaching bot refreshes its destination as the target moves. Long enough that the bot
	 *  commits to a route rather than re-pathing onto every frame's new target position. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Tactics", meta = (ClampMin = "0.1"))
	float ApproachRepathInterval;

	/**
	 * Longest any action may pursue one move goal before it is abandoned and the bot re-selects.
	 *
	 * A deliberate backstop against a whole class of bug rather than one instance of it: an action latched on
	 * GoalPending stops responding to anything if the goal turns out to be unreachable - partial paths, a nav
	 * link the capsule cannot take, geometry that moved. A bot stood still is the single worst failure mode
	 * here, because it looks exactly like broken AI.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Tactics", meta = (ClampMin = "0.5"))
	float MoveGoalTimeout;

	/**
	 * When the bot has lost its target and no visible-from candidate position can be found, head toward the
	 * target's actual current position rather than standing still.
	 *
	 * Knowingly a small piece of omniscience, and a playability decision rather than an oversight: the
	 * alternative is a bot that can be permanently lost by walking round one corner, which in a 1v1 practice
	 * opponent is worse than a bot that walks your way. Its reaction time, turn rate and aim error all still
	 * apply on arrival, so it only gets to find you, never to shoot you better. Turn off for a strictly fair
	 * bot that can be genuinely evaded.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Tactics")
	bool bSearchTowardTargetWhenLost;

	// --- Steering -------------------------------------------------------------------------------------
	//
	// Fighting locomotion. A human circling an opponent applies continuous sideways input; they do not walk
	// between survey markers. So inside engagement range the bot has no move goal at all and builds a desired
	// direction every tick from a handful of weighted terms, fed straight into AddMovementInput.
	//
	// Pathfinding is still used for everything outside a fight (Hunt, a long Approach, Retreat), because
	// crossing an arena genuinely is a pathfinding problem and pure steering cannot solve concave geometry.

	/** How hard the bot corrects toward its preferred range, per unit of normalised range error. Higher
	 *  closes and backs off more urgently; too high and the bot yo-yos on the radial axis. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Steering", meta = (ClampMin = "0.0", ClampMax = "8.0"))
	float SteerRadialGain;

	/** Orbit strength while strafing. The side is committed by the strafe leg; this is how hard it circles. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Steering", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float SteerTangentWeightStrafe;

	/** Orbit strength while closing. Low, so an approach mostly closes rather than spiralling in. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Steering", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float SteerTangentWeightApproach;

	/** Extra tangential weight at the start of a reposition arc, decaying to zero across its duration. This
	 *  is what turns a reposition from a waypoint into a visible swing wide off the current angle. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Steering", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float SteerRepositionTangentBoost;

	/** How much a reposition leans toward the cover direction FindTacticalDestination scored. The search is
	 *  still doing the thinking; its answer is just consumed as a direction rather than as a destination. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Steering", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float SteerRepositionBiasWeight;

	/** Multiple of the preferred range inside which an Approach steers instead of pathing. Beyond it the bot
	 *  is crossing the map, which is a pathfinding problem. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Steering", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float SteerRangeMultiplier;

	/** How quickly the steering direction may change, as an interp speed. This is the bot's body inertia:
	 *  low reads as heavy and deliberate, high reads as twitchy. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Steering", meta = (ClampMin = "0.5"))
	float SteerTurnInterpSpeed;

	/** Peak wander applied to the steering direction, in degrees. Without it the bot orbits like a turret
	 *  platform on rails, which is readable and trivially pre-aimed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Steering", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float SteerNoiseMaxDegrees;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Steering", meta = (ClampMin = "0.05"))
	float SteerNoiseIntervalMin;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Steering", meta = (ClampMin = "0.05"))
	float SteerNoiseIntervalMax;

	/** Length of the forward avoidance whisker. Side whiskers are 70% of it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Steering", meta = (ClampMin = "20.0"))
	float SteerWhiskerLength;

	/** Splay of the side whiskers off the intended direction, in degrees. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Steering", meta = (ClampMin = "5.0", ClampMax = "89.0"))
	float SteerWhiskerAngleDegrees;

	/** How strongly a whisker hit pushes the steering direction away along the surface normal. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Steering", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float SteerAvoidWeight;

	/** How far ahead the intended step is projected onto the nav mesh. This is the containment check that
	 *  keeps a steered bot on navigable ground without needing a path - it is what stops it steering off a
	 *  terrace or into a wall the whiskers happened to miss. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Steering", meta = (ClampMin = "20.0"))
	float SteerNavProbeDistance;

	/** Largest height change between the bot's feet and the projected step that still counts as navigable.
	 *  Sized above the nav mesh's own offset from the floor, or every step reads as a drop. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Steering", meta = (ClampMin = "10.0"))
	float SteerNavMaxStepHeight;

	/**
	 * How long the steering layer may find no navigable deflection before it gives up and asks for a path.
	 *
	 * The explicit answer to the known trade-off: steering does not pathfind, so a steered bot can wedge
	 * itself in concave geometry. Rather than pretend that cannot happen, this detects it and hands the
	 * problem to the nav mesh for a bounded window.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Steering", meta = (ClampMin = "0.1"))
	float SteerBlockedRecoveryTime;

	/** How long the recovery path is allowed to run before steering is attempted again. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Steering", meta = (ClampMin = "0.2"))
	float SteerRecoveryPathTime;

private:
	UPROPERTY()
	TObjectPtr<UShooterAIBlackboard> Knowledge;

	UPROPERTY()
	TObjectPtr<UShooterAIAimComponent> AimLogic;

	UPROPERTY()
	TObjectPtr<UShooterAIMovementTechComponent> MovementTech;

	// --- Action selection: commitment plus invalidation --------------------------------------------

	void UpdateActionSelection(float DeltaTime);

	/**
	 * Chooses the action for this selection pass and, via OutLatches, the conditions it is committing to.
	 * Pure decision - issues no orders and touches no timers.
	 */
	EShooterBotAction SelectAction(EShooterAIInterrupt Interrupt, EShooterAILatch& OutLatches,
		float& OutMinCommit, float& OutMaxDuration) const;

	/** True while every latched condition in Mask still holds. */
	bool EvaluateLatches(EShooterAILatch Mask) const;

	/** Called once on entry, so per-action setup (picking a destination, dropping the trigger) happens once. */
	void EnterAction(EShooterBotAction NewAction, EShooterAILatch Latches, float MinCommit, float MaxDuration);

	/** Called every frame for the active action. This is where move orders are issued. */
	void DriveAction(float DeltaTime);

	void DriveHunt(float DeltaTime);
	void DriveApproach(float DeltaTime);
	void DriveStrafe(float DeltaTime);
	void DriveReposition(float DeltaTime);
	void DriveRetreat();

	EShooterBotAction CurrentAction;
	EShooterAILatch CurrentLatches;
	float ActionElapsed;
	float ActionMinCommit;
	float ActionMaxDuration;

	/** Suppresses health-driven retreats for RetreatCooldown after one finishes, so a low-health bot comes
	 *  back and fights instead of kiting for the rest of the match. */
	float RetreatSuppressedTimer;

	/** Time since the reposition die was last rolled, so a per-second chance stays per-second even though
	 *  selection now runs at irregular intervals. */
	float TimeSinceRepositionRoll;

	// --- Movement ---------------------------------------------------------------------------------

	/** Wraps MoveToLocation and records the goal so the movement-tech layer can reason about the approach. */
	void RequestMoveTo(const FVector& Location);
	void StopMoving();

	bool HasReachedMoveGoal() const;

	// --- Steering ---------------------------------------------------------------------------------

	/**
	 * Switches locomotion to continuous steering. Aborts any path in progress, because path following and
	 * the steering layer both write AddMovementInput and would otherwise fight for the frame.
	 */
	void BeginSteering();
	void EndSteering();

	/** Builds this frame's desired direction and applies it. Called from the Drive functions that steer. */
	void UpdateSteering(float DeltaTime);

	/**
	 * Deflects Desired away from whisker hits and then onto navigable ground, trying progressively wider
	 * angles. Sets bOutBlocked when nothing within +/-110 degrees lands on the nav mesh - which is the
	 * signal that steering has wedged and the nav mesh needs to take over.
	 */
	FVector ResolveSteeringObstacles(const FVector& Desired, bool& bOutBlocked) const;

	void UpdateSteerNoise(float DeltaTime);

	/** True while the bot is close enough to a fight to steer rather than path. Hysteretic, so the two
	 *  locomotion models cannot swap every time the range wobbles across one line. */
	bool ShouldActionSteer() const;

	bool bSteering;
	FVector SteeringDirection;
	float SteerBlockedTime;
	float SteerRecoveryTimer;

	float SteerNoiseCurrent;
	float SteerNoiseTarget;
	float SteerNoiseTimer;

	/** Remaining time on the reposition arc's extra tangential weight. Zero outside a reposition. */
	float RepositionArcRemaining;

	/** Direction FindTacticalDestination scored as the best new angle, consumed as a lean rather than as a
	 *  destination. Zero when the search found nothing. */
	FVector RepositionBiasDirection;

	/** A navigable point roughly AwayFrom-relative-to-target, used for repositioning and retreating. */
	bool FindTacticalDestination(bool bBreakLineOfSight, FVector& OutLocation) const;

	/** Perpendicular strafe destination at the preferred range, on the currently committed side. */
	FVector ComputeStrafeDestination() const;

	/** A point on the bot-to-target line at the preferred range. */
	FVector ComputeApproachDestination() const;

	FVector MoveGoal;
	bool bHasMoveGoal;

	/** Time spent pursuing the current move goal, against MoveGoalTimeout. */
	float MoveGoalElapsed;

	/** Counts down to the next hunt re-path. */
	float HuntRepathTimer;

	/** Counts down to the next approach re-path. */
	float ApproachRepathTimer;

	/**
	 * Remaining time on the current strafe leg, and the side it is committed to.
	 *
	 * A leg is the unit of commitment for strafing: the side is chosen once, one destination is issued, and
	 * neither is reconsidered until the leg expires. The old bot flipped StrafeSign on entry to Engage, which
	 * meant every bounce out of Reposition reversed its direction - the "flits between directions" complaint
	 * in one line.
	 */
	float StrafeLegRemaining;
	int32 StrafeSign;

	// --- Combat -----------------------------------------------------------------------------------

	/** Ammo housekeeping that is not the aim component's business: the tactical reload and the watchdog that
	 *  guarantees a bot's reload finishes even if the 3P montage carries no notify. */
	void UpdateWeaponHousekeeping(float DeltaTime);

	bool ShouldReloadNow() const;

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

	/** Bound to the pawn's health component so being shot raises the highest-priority interrupt. */
	UFUNCTION()
	void OnPawnHealthChanged(UHealthComponent* HealthComponent, float OldValue, float NewValue, AActor* DamageInstigator);

	TWeakObjectPtr<UHealthComponent> BoundHealth;

	void DrawDebug() const;
	void DrawSteeringDebug() const;
};
