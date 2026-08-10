// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AI/ShooterAITypes.h"
#include "ShooterAIMovementTechComponent.generated.h"

class AShooterAIController;
class AShooterCharacter;
class UShooterAIBlackboard;
class UShooterMovementComponent;

/**
 * Teaches the bot to move like a player rather than like a nav-mesh agent.
 *
 * This is the layer a nav mesh cannot provide. Path following produces a steering input and nothing else; it
 * has no concept of sprinting, sliding, double jumping or wall running. So this component watches the path
 * the bot is already following and decides when to spend movement tech on it.
 *
 * It never writes velocity, never teleports and never sets a movement mode. Every decision goes through
 * UShooterMovementComponent's public intent API (SetWantsToSprint / RequestSlide) or ACharacter::Jump, so the
 * bot is subject to every gate the player is: slide needs sprint plus ground plus SlideMinStartSpeed and
 * respects SlideCooldown; the air jump is the same directional redirect off GetCurrentAcceleration; wall run
 * runs its own detection and its own cooldowns. That is the point - the movement component stays the single
 * authority on what is legal, and this component only decides what to *try*.
 *
 * ---------------------------------------------------------------------------------------------------------
 * Why the bot never wall ran, and what a wall run now is
 *
 * UShooterMovementComponent::TryStartWallRun has a gate that no other movement tech has: movement input must
 * point along the *capsule's* forward vector (WallRunMinForwardInputDot, 0.5) for the whole attach window,
 * and the capsule's yaw follows the control rotation, which the aim layer is writing onto the target. Four
 * things then went wrong, and all four had to be fixed:
 *
 *  1. The old code pressed jump first and asked the aim layer to swing the yaw onto travel afterwards, at
 *     MaxTurnRateDegrees (220 deg/s). A 90-degree swing takes 0.41s and a 180-degree swing 0.8s, but the
 *     attach window is only the ~0.7s of airtime during which the bot is falling. It usually arrived facing
 *     the right way a moment after the opportunity had passed. A wall run is now a committed multi-phase
 *     action that lines the yaw up on the ground FIRST and only jumps once it is aligned.
 *  2. The wall probe traced along the capsule's right vector - i.e. sideways relative to where the bot was
 *     *looking*, not where it was *going*. With the aim on the player that is an essentially unrelated
 *     direction, so the bot committed to walls it was about to run head-first into. The probe is now taken
 *     perpendicular to the travel direction, and the surface must additionally be roughly parallel to travel.
 *  3. WallProbeDistance defaulted above the character's WallRunTraceDistance, so a "found" wall could be out
 *     of the movement component's own attach range. It now defaults just inside it.
 *  4. The scan was excluded from the engagement states, which is where a duelling bot spends the match. It
 *     now runs during any traversal action, including Approach.
 * ---------------------------------------------------------------------------------------------------------
 *
 * A consequence worth understanding: the thresholds below are duplicates of the character's, not reads of
 * them. The character's FPS|Movement values are protected (UShooterMovementComponent is a friend of it; this
 * is not), so these are the bot's own heuristics for when an attempt is worth making. They should sit at or
 * inside the character's real thresholds - outside them, the bot commits to attempts the movement component
 * will refuse, and a refused wall run still costs the bot its aim for the length of the commit window.
 *
 * Ticked explicitly from AShooterAIController::Tick, before the aim component, because a traversal yaw claim
 * has to be raised before aim writes the control rotation for the frame.
 */
UCLASS()
class FPS_API UShooterAIMovementTechComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UShooterAIMovementTechComponent();

	void TickMovementTech(float DeltaTime);

	/** Clears sprint intent, any pending tech and any yaw claim. Called when the bot dies or is re-possessed. */
	void ResetTech();

	/** The direction the bot is actually travelling - path-following input if there is any, else velocity. */
	FVector GetTravelDirection() const;

	EShooterTechAction GetTechAction() const { return TechAction; }

	void DrawTechDebug() const;

protected:
	// --- Sprint -----------------------------------------------------------------------------------

	/** Below this distance to its goal the bot stops sprinting, so it arrives able to shoot rather than
	 *  skidding past. Sprint blocks firing by design, so this is a tactical value, not a cosmetic one. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Movement", meta = (ClampMin = "0.0"))
	float SprintMinGoalDistance;

	/**
	 * Shortest time a sprint decision stands before it may be reversed.
	 *
	 * Sprint blocks firing and UCombatComponent::Initiate_FireWeapon_Pressed cancels the sprint on the
	 * shooter's behalf, so the two layers can end up fighting over the flag frame by frame - which is visible
	 * as a run speed that flickers between WalkSpeed and SprintSpeed. Commitment here, exactly as in the
	 * tactical layer, is what makes the bot's travel read as deliberate.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Movement", meta = (ClampMin = "0.0"))
	float SprintMinHoldTime;

	// --- Slide ------------------------------------------------------------------------------------

	/** Ground speed the bot requires before it will even try to slide. Should sit at or above the
	 *  character's SlideMinStartSpeed; the movement component still has the final say. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Movement", meta = (ClampMin = "0.0"))
	float SlideAttemptMinSpeed;

	/** The bot slides when its remaining distance to the goal falls inside this window - i.e. it slides
	 *  *into* its destination, which is what a player does, rather than sliding at random mid-route. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Movement", meta = (ClampMin = "0.0"))
	float SlideApproachDistanceMin;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Movement", meta = (ClampMin = "0.0"))
	float SlideApproachDistanceMax;

	/** Sliding also fires as an evasive move while under fire in the open, independent of the goal window. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Movement")
	bool bSlideToEvade;

	/**
	 * How long an evasive slide may sprint to buy the speed a slide needs before giving up.
	 *
	 * A steering bot fighting at close range has no move goal and is deliberately never sprinting, because
	 * sprint blocks firing. So an in-fight slide has to prime itself: this is the window during which the bot
	 * has chosen movement over shooting, and it is exactly the trade a player makes when they sprint-cancel
	 * into a slide mid-duel. Keep it short - it is a beat, not a retreat.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Movement", meta = (ClampMin = "0.05", ClampMax = "2.0"))
	float SlideSprintPrimeTime;

	// --- Wall run ---------------------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Movement")
	bool bAllowWallRun;

	/**
	 * Sideways probe length when looking for a wall worth running.
	 *
	 * Must sit at or just *inside* the character's WallRunTraceDistance (75 by default). Longer and the bot
	 * commits its yaw, its jump and its aim to a wall UShooterMovementComponent::FindRunnableWall will then
	 * refuse to attach to, which is a silent and expensive failure.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Movement", meta = (ClampMin = "10.0"))
	float WallProbeDistance;

	/** Horizontal speed the bot requires before attempting a wall run. At or above the character's
	 *  WallRunMinSpeed, since a run that attaches below it ends on the same frame. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Movement", meta = (ClampMin = "0.0"))
	float WallRunAttemptMinSpeed;

	/** |ImpactNormal.Z| above this is a ramp or a ceiling, not a wall. Mirrors WallRunMaxWallNormalZ. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Movement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WallProbeMaxNormalZ;

	/**
	 * How head-on a wall may be and still count as runnable, as |wall normal . travel direction|.
	 *
	 * The check the old probe was missing. A surface can be perfectly vertical, within range and on the
	 * correct side, and still be a wall the bot is about to run straight into - which produces a jump into a
	 * dead stop rather than a wall run. Low values demand a wall genuinely parallel to the route.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Movement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WallProbeMaxTravelDot;

	/** How closely the capsule must face along travel before the bot commits and jumps, as a dot product.
	 *  Comfortably above the character's WallRunMinForwardInputDot (0.5) so the attach test cannot fail on
	 *  the very margin this phase exists to satisfy. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Movement", meta = (ClampMin = "0.5", ClampMax = "1.0"))
	float WallRunLineUpDot;

	/** Longest the bot will spend turning onto its travel direction before giving the attempt up. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Movement", meta = (ClampMin = "0.05"))
	float WallRunLineUpMaxTime;

	/** How long the bot holds its yaw after jumping at a wall, waiting for the movement component to attach.
	 *  Roughly the airtime between clearing WallRunMaxStartVerticalSpeed and landing again. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Movement", meta = (ClampMin = "0.05"))
	float WallRunCommitTime;

	/** Minimum spacing between wall-run attempts, on top of the movement component's own cooldowns. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Movement", meta = (ClampMin = "0.0"))
	float WallRunAttemptInterval;

	/** How far into a wall run the bot jumps off. Ending a run with a wall jump is the whole reason to be on
	 *  the wall, so this sits well inside the character's WallRunMaxDuration. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Movement", meta = (ClampMin = "0.05"))
	float WallJumpAfterTime;

	/** The bot will not start a wall run when its destination is nearer than this - a wall run that ends past
	 *  the goal costs more than it saves. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Movement", meta = (ClampMin = "0.0"))
	float WallRunMinGoalDistance;

	// --- Jumping ----------------------------------------------------------------------------------

	/** Evasive jumps while fighting. Off makes the bot a much easier target - it is the main reason a
	 *  Veteran bot is hard to track. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Movement")
	bool bJumpDodge;

	/** How long the bot must be blocked - moving far slower than it is asking to - before it jumps to try to
	 *  clear whatever is in the way. This is what gets it over ledges the nav mesh routed it into. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Movement", meta = (ClampMin = "0.05"))
	float StuckTimeBeforeJump;

private:
	AShooterAIController* GetAIController() const;
	AShooterCharacter* GetShooterPawn() const;
	UShooterAIBlackboard* GetBlackboard() const;
	UShooterMovementComponent* GetShooterMovement() const;

	void UpdateSprint(float DeltaTime);
	void UpdateSlide(float DeltaTime);
	void UpdateWallRun(float DeltaTime);
	void UpdateJump(float DeltaTime);

	/** Ends the current wall-run action, releases the yaw and starts the attempt cooldown. */
	void AbortWallRun();

	/** Looks for a runnable wall alongside the current route. Sets TechAction to WallRunLineUp on success. */
	void TryBeginWallRun();

	/** Single-frame jump press. ACharacter::Jump latches bPressedJump, and CheckJumpInput will happily spend
	 *  the air jump on the very next frame while it is still held - which would burn the double jump
	 *  instantly. So the press is released on the following tick, never in the same one (releasing in the
	 *  same frame can beat CheckJumpInput to it and produce no jump at all). */
	void PressJump();
	void ReleaseJumpIfPending();

	/**
	 * Probe perpendicular to Travel for a wall worth running. True only for a near-vertical surface, within
	 * range, that is roughly parallel to the direction of travel.
	 */
	bool ProbeForWall(const FVector& Travel, bool bRightSide, FHitResult& OutHit) const;

	bool RollTechChance() const;

	// --- Committed wall-run action ---
	EShooterTechAction TechAction;

	/** Time spent in the current TechAction phase. */
	float TechPhaseTime;

	/** Cooldown before another wall-run opportunity is even looked for. */
	float WallRunAttemptTimer;

	/** Rolled once at attach so the bot either commits to the wall jump exit or does not, rather than
	 *  re-rolling it every frame of the run. */
	bool bWallJumpPlanned;

	/** Side the probe found the wall on, kept so line-up can keep re-checking the same side. */
	bool bWallOnRight;

	// --- Sprint ---
	bool bSprintIntent;
	float SprintHoldTime;

	/** True while the bot is deliberately sprinting to buy the speed an evasive slide needs. */
	bool bEvadeSprintActive;
	float EvadeSprintTimer;

	// --- Other tech timers ---
	float SlideCheckTimer;
	float JumpDodgeTimer;
	float StuckTimer;
	bool bJumpPressedLastTick;
};
