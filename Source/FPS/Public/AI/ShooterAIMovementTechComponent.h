// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShooterAIMovementTechComponent.generated.h"

class AShooterAIController;
class AShooterCharacter;
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
 * A consequence worth understanding: the thresholds below are duplicates of the character's, not reads of
 * them. The character's FPS|Movement values are protected (UShooterMovementComponent is a friend of it; this
 * is not), so these are the bot's own heuristics for when an attempt is worth making. They should sit at or
 * above the character's real thresholds - if they sit below, the bot simply makes attempts that the movement
 * component refuses, which costs nothing but noise.
 *
 * Ticked explicitly from AShooterAIController::Tick, before the aim component, because a wall-run attempt has
 * to be able to claim the yaw for that frame before aim writes the control rotation.
 */
UCLASS()
class FPS_API UShooterAIMovementTechComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UShooterAIMovementTechComponent();

	void TickMovementTech(float DeltaTime);

	/** True while a wall-run attempt or an active wall run needs the bot facing along its travel direction. */
	bool WantsYawLockedToTravel() const;

	/** The direction the bot is actually travelling - path-following input if there is any, else velocity. */
	FVector GetTravelDirection() const;

	/** Clears sprint intent and any pending tech. Called when the bot dies or the state machine resets. */
	void ResetTech();

protected:
	// --- Sprint -----------------------------------------------------------------------------------

	/** Below this distance to its goal the bot stops sprinting, so it arrives able to shoot rather than
	 *  skidding past. Sprint blocks firing by design, so this is a tactical value, not a cosmetic one. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Movement", meta = (ClampMin = "0.0"))
	float SprintMinGoalDistance;

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

	// --- Wall run ---------------------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Movement")
	bool bAllowWallRun;

	/** Sideways probe length when looking for a wall worth running. Should roughly match the character's
	 *  WallRunTraceDistance - shorter and the bot commits too late, longer and it commits to walls the
	 *  movement component will not accept. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Movement", meta = (ClampMin = "10.0"))
	float WallProbeDistance;

	/** Horizontal speed the bot requires before attempting a wall run. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Movement", meta = (ClampMin = "0.0"))
	float WallRunAttemptMinSpeed;

	/** |ImpactNormal.Z| above this is a ramp or a ceiling, not a wall. Mirrors WallRunMaxWallNormalZ. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Movement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WallProbeMaxNormalZ;

	/** How long the bot holds its yaw along its travel direction after jumping at a wall, waiting for the
	 *  movement component to attach. Too short and it looks away before the attach test runs; too long and
	 *  it stares down a corridor it has already left. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Movement", meta = (ClampMin = "0.05"))
	float WallRunCommitTime;

	/** Minimum spacing between wall-run attempts, on top of the movement component's own cooldowns. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Movement", meta = (ClampMin = "0.0"))
	float WallRunAttemptInterval;

	/** Fraction of a wall run's expected duration after which the bot jumps off rather than sliding down.
	 *  Ending a run with a wall jump is the whole reason to be on the wall, so this defaults high. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI|Movement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WallJumpAtRunFraction;

	// --- Jumping ----------------------------------------------------------------------------------

	/** Evasive jumps while engaging. Off makes the bot a much easier target - it is the main reason a
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
	UShooterMovementComponent* GetShooterMovement() const;

	void UpdateSprint();
	void UpdateSlide(float DeltaTime);
	void UpdateWallRun(float DeltaTime);
	void UpdateJump(float DeltaTime);

	/** Single-frame jump press. ACharacter::Jump latches bPressedJump, and CheckJumpInput will happily spend
	 *  the air jump on the very next frame while it is still held - which would burn the double jump
	 *  instantly. So the press is released on the following tick, never in the same one (releasing in the
	 *  same frame can beat CheckJumpInput to it and produce no jump at all). */
	void PressJump();
	void ReleaseJumpIfPending();

	/** Sideways probe for a wall worth running. True only for a near-vertical surface within range. */
	bool ProbeForWall(bool bRightSide, FHitResult& OutHit) const;

	bool RollTechChance() const;

	float SlideCheckTimer;
	float WallRunAttemptTimer;
	float WallRunCommitTimer;
	float JumpDodgeTimer;
	float StuckTimer;
	bool bJumpPressedLastTick;
	bool bWallRunJumpQueued;
};
