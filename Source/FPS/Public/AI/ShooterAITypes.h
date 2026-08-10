// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "Misc/EnumClassFlags.h"
#include "ShooterAITypes.generated.h"

/**
 * The bot's tactical action.
 *
 * "Action", not "state", on purpose. Following Game AI Pro 3 ch.10, an action here is *long-lived*: once
 * chosen it runs for at least its minimum commitment time and is only abandoned when one of its latched
 * validity conditions fails or an interrupt fires. That commitment is the whole reason the bot reads as
 * having a plan rather than as re-rolling a direction every frame.
 *
 * Deliberately exclusive - a 1v1 bot is only ever doing one of these - and deliberately plain C++ rather
 * than a Behaviour Tree asset. See the note at the top of AShooterAIController.
 */
UENUM(BlueprintType)
enum class EShooterBotAction : uint8
{
	/** No target known at all. Holds position and scans. */
	Idle UMETA(DisplayName = "Idle"),

	/** Target exists but has not been seen recently. Moves to take an angle on its last known position. */
	Hunt UMETA(DisplayName = "Hunt"),

	/** Target visible but too far for the equipped weapon. Closes to the preferred range. */
	Approach UMETA(DisplayName = "Approach"),

	/** Target visible and at a workable range. Circles on a committed side, trading. */
	Strafe UMETA(DisplayName = "Strafe"),

	/** Deliberately breaking the current angle - crossing to a new one rather than trading in place. */
	Reposition UMETA(DisplayName = "Reposition"),

	/** Hurt or dry. Breaks line of sight and reloads. */
	Retreat UMETA(DisplayName = "Retreat")
};

/** True for the actions that mean "I am in a fight, holding an angle on my target". */
inline bool IsShooterBotFightingAction(EShooterBotAction Action)
{
	return Action == EShooterBotAction::Approach || Action == EShooterBotAction::Strafe;
}

/** True for the actions whose point is to cover ground - the ones worth spending movement tech on. */
inline bool IsShooterBotTraversalAction(EShooterBotAction Action)
{
	return Action == EShooterBotAction::Hunt
		|| Action == EShooterBotAction::Approach
		|| Action == EShooterBotAction::Reposition
		|| Action == EShooterBotAction::Retreat;
}

/**
 * High-priority events that invalidate the current action immediately, regardless of how long it has left
 * to run. Adapted from the chapter's interrupt list.
 *
 * Declaration order IS the priority order: lower numeric value wins when several fire on the same frame.
 * None must stay at 0 so "nothing pending" and "lowest priority" are not the same value.
 *
 * An interrupt lasts exactly one update - it is consumed by the selection pass that reacts to it.
 */
UENUM(BlueprintType)
enum class EShooterAIInterrupt : uint8
{
	None UMETA(DisplayName = "None"),

	/** Shot. The single most urgent thing that can happen to a duelist. */
	TookDamage UMETA(DisplayName = "Took Damage"),

	/** The target was live and is now stale. Whatever the bot was doing assumed it could see them. */
	TargetLost UMETA(DisplayName = "Target Lost"),

	/** First acquisition after the reaction delay. Stops a hunt dead and starts a fight. */
	TargetAcquired UMETA(DisplayName = "Target Acquired"),

	/** Mag ran dry with reserve left. */
	WeaponNeedsReload UMETA(DisplayName = "Weapon Needs Reload"),

	/** Health crossed the retreat threshold this frame. */
	HealthCritical UMETA(DisplayName = "Health Critical")
};

/**
 * The latched validity conditions an action is chosen under.
 *
 * This is the chapter's `parallel` node made explicit: when an action is selected, the set of conditions
 * that had to be true for it to be the right choice is recorded, and *only that set* is re-checked each
 * tick. Nothing else about the decision is re-evaluated. When one fails, the action is invalidated and
 * selection runs again from the top.
 *
 * Not a UENUM: it is a C++-internal bitmask, never authored in the editor, and UHT's bitflag support
 * buys nothing here.
 */
enum class EShooterAILatch : uint16
{
	None          = 0,

	/** A target actor still exists and is alive. */
	TargetKnown   = 1 << 0,

	/** The target is still live - visible, or seen recently enough to act on. */
	TargetLive    = 1 << 1,

	/** The target is NOT currently visible. Held by actions whose point is to be out of sight. */
	TargetHidden  = 1 << 2,

	/** Health is still above the retreat threshold. */
	HealthOk      = 1 << 3,

	/** There are rounds in the mag. */
	MagHasAmmo    = 1 << 4,

	/** A move goal exists and has not been reached. Fails on arrival, which ends "go there" actions. */
	GoalPending   = 1 << 5,

	/** Still far enough out to be worth closing. Fails when the bot arrives at fighting range. */
	TargetFar     = 1 << 6,

	/** Still close enough to fight from. Fails when the target opens the range. */
	TargetNear    = 1 << 7,

	/** The mag is still empty. Held by the dry-gun retreat, so it ends the moment the reload lands rather
	 *  than running out its full duration with a full magazine. */
	MagEmpty      = 1 << 8
};
ENUM_CLASS_FLAGS(EShooterAILatch);

/** What the look/aim layer is doing. Deliberately independent of EShooterBotAction - see UShooterAIAimComponent. */
UENUM(BlueprintType)
enum class EShooterAimState : uint8
{
	/** Nothing worth looking at. Faces along travel so the bot sees what it is walking into. */
	Search UMETA(DisplayName = "Search"),

	/** A live target. The bot's view is on them and nothing else may take it. */
	Track UMETA(DisplayName = "Track"),

	/** Locomotion has been granted an explicit, time-bounded claim on the yaw (a wall run). */
	Traverse UMETA(DisplayName = "Traverse")
};

/** Phases of the committed wall-run traversal action. See UShooterAIMovementTechComponent. */
UENUM(BlueprintType)
enum class EShooterTechAction : uint8
{
	None UMETA(DisplayName = "None"),

	/** Wall found, yaw claimed, turning onto the travel direction. No jump yet. */
	WallRunLineUp UMETA(DisplayName = "Wall Run - Line Up"),

	/** Lined up and airborne, holding the yaw while the movement component's attach test runs. */
	WallRunCommit UMETA(DisplayName = "Wall Run - Commit"),

	/** Attached. Holding the yaw along the wall until the wall jump. */
	WallRunActive UMETA(DisplayName = "Wall Run - Active")
};

/** Named difficulty presets. Custom means "use whatever is authored in the struct, don't overwrite it". */
UENUM(BlueprintType)
enum class EShooterBotSkill : uint8
{
	Recruit UMETA(DisplayName = "Recruit"),
	Regular UMETA(DisplayName = "Regular"),
	Veteran UMETA(DisplayName = "Veteran"),
	Custom UMETA(DisplayName = "Custom (hand-authored)")
};

/**
 * A tunable response curve from target stillness to a multiplier.
 *
 * Exposed as min/max plus an exponent rather than as a UCurveFloat on purpose: a curve asset is a binary
 * .uasset that has to be authored before the bot works at all, and the whole AI is deliberately playable
 * from a fresh compile. The exponent covers everything a two-point curve needs to say - raise it to make
 * the bot forgiving until the target is *almost* perfectly still, lower it for a gentler ramp.
 */
USTRUCT(BlueprintType)
struct FShooterAIStillnessResponse
{
	GENERATED_BODY()

	FShooterAIStillnessResponse() {}

	FShooterAIStillnessResponse(float InAtMoving, float InAtStill, float InExponent)
		: AtMoving(InAtMoving), AtStill(InAtStill), Exponent(InExponent) {}

	/** Multiplier when the target is fully mobile (stillness 0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stillness", meta = (ClampMin = "0.0"))
	float AtMoving = 1.f;

	/** Multiplier when the target is perfectly still (stillness 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stillness", meta = (ClampMin = "0.0"))
	float AtStill = 1.f;

	/** Shapes the blend. 1 = linear, >1 = only rewards near-perfect stillness, <1 = punishes any slowdown. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stillness", meta = (ClampMin = "0.05", ClampMax = "8.0"))
	float Exponent = 1.f;

	float Evaluate(float Stillness) const
	{
		const float Shaped = FMath::Pow(FMath::Clamp(Stillness, 0.f, 1.f), FMath::Max(Exponent, 0.05f));
		return FMath::Lerp(AtMoving, AtStill, Shaped);
	}
};

/**
 * Everything that separates an easy bot from a hard one.
 *
 * Every field here is a *human limitation*: how fast it reacts, how fast it can turn, how badly it aims
 * before it has settled on a target, how often it commits to movement tech. There is deliberately no health,
 * damage, speed or accuracy-of-the-weapon multiplier anywhere in this struct, and none should be added - the
 * design pillar is that the bot uses the same body and the same guns as the player and differs only in
 * decision quality. A bot that wins by having more health is not a harder opponent, it is a longer one.
 */
USTRUCT(BlueprintType)
struct FShooterBotDifficulty
{
	GENERATED_BODY()

	// --- Perception -------------------------------------------------------------------------------

	/** Delay between the target becoming visible and the bot being allowed to act on it. Scaled by
	 *  ReactionStillnessResponse, so this is the reaction time against a *typically moving* target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception", meta = (ClampMin = "0.0"))
	float ReactionTime = 0.35f;

	/** How long a target stays "live" after the bot loses sight of it. While live the bot keeps its view on
	 *  the remembered position; once stale it is allowed to look elsewhere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception", meta = (ClampMin = "0.0"))
	float TargetMemoryTime = 4.f;

	/**
	 * How long line of sight must be *continuously* broken before the bot accepts that it is broken.
	 *
	 * Without this, a target strafing behind a railing flickers the LOS trace every frame, and everything
	 * downstream flickers with it: sprint on/off, ADS pressed/released, trigger held/dropped. That flicker was
	 * a large part of what read as "janky". Keep it short - it is a debounce, not memory.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LineOfSightGraceTime = 0.2f;

	// --- Target stillness -------------------------------------------------------------------------
	//
	// The core mechanic. Standing still is punished; sliding, jumping and wall running are the player's
	// defence. Everything below feeds one smoothed 0-1 number that then drives aim error, turn rate and
	// reaction time. See UShooterAIBlackboard::UpdateStillness.

	/** Horizontal speed at or below which the target counts as fully still. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stillness", meta = (ClampMin = "0.0"))
	float StillnessStillSpeed = 40.f;

	/** Horizontal speed at or above which the target counts as fully mobile. Sits near sprint speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stillness", meta = (ClampMin = "1.0"))
	float StillnessFullSpeed = 800.f;

	/** Fraction of remaining stillness removed while the target is airborne. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stillness", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StillnessAirbornePenalty = 0.5f;

	/** Fraction removed while the target is sliding. Higher than airborne: a slide is a fast, low, short read. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stillness", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StillnessSlidePenalty = 0.65f;

	/** Fraction removed while the target is wall running. The hardest thing in the game to track, so the
	 *  strongest penalty - this is what makes the wall run worth learning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stillness", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StillnessWallRunPenalty = 0.8f;

	/**
	 * Time constant for stillness *rising* - i.e. how long a player has to stand still before the bot has
	 * fully locked on. This is the number that decides whether standing still feels punished: too long and
	 * the mechanic is invisible, too short and stopping for a doorway gets you deleted.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stillness", meta = (ClampMin = "0.01"))
	float StillnessRiseTime = 0.35f;

	/** Time constant for stillness *falling*. Much shorter than the rise: moving again must pay off
	 *  immediately, or the player cannot feel the cause and effect. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stillness", meta = (ClampMin = "0.01"))
	float StillnessFallTime = 0.08f;

	/** Stillness assumed while the target cannot be seen. Below the midpoint on purpose - an unseen player
	 *  is presumed to be repositioning, so the bot does not get a free settled aim on a re-peek. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stillness", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StillnessWhenUnseen = 0.3f;

	/** Stillness -> aim error multiplier. Still targets get a tight cone, moving ones a wide one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stillness")
	FShooterAIStillnessResponse AimErrorStillnessResponse = FShooterAIStillnessResponse(2.5f, 0.15f, 1.5f);

	/** Stillness -> turn rate multiplier. Still targets get snapped onto, moving ones get lagged behind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stillness")
	FShooterAIStillnessResponse TurnRateStillnessResponse = FShooterAIStillnessResponse(0.55f, 1.6f, 1.2f);

	/** Stillness -> reaction time multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stillness")
	FShooterAIStillnessResponse ReactionStillnessResponse = FShooterAIStillnessResponse(1.5f, 0.4f, 1.5f);

	// --- Aim --------------------------------------------------------------------------------------

	/** Hard cap on how fast the bot can turn, degrees per second, before the stillness multiplier. This is
	 *  its mouse hand. Uncapped aim is what makes a bot feel like an aimbot even when its accuracy is poor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim", meta = (ClampMin = "10.0"))
	float MaxTurnRateDegrees = 220.f;

	/** Aim offset the moment a target is acquired, in degrees, before the stillness multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim", meta = (ClampMin = "0.0"))
	float AimErrorInitialDegrees = 5.5f;

	/** Aim offset once the bot has tracked the same target for AimSettleTime. Never set this to 0 - a bot
	 *  with zero settled error is a laser, and the player has no counterplay to being perfectly tracked. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim", meta = (ClampMin = "0.0"))
	float AimErrorSettledDegrees = 1.1f;

	/** How long continuous tracking takes to shrink the error from initial to settled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim", meta = (ClampMin = "0.01"))
	float AimSettleTime = 0.9f;

	/** How often a new random error direction is chosen. Long intervals read as a steady mis-zero;
	 *  short intervals read as jitter. Around a third of a second reads as a human micro-correcting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim", meta = (ClampMin = "0.01"))
	float AimErrorRefreshInterval = 0.35f;

	/** Seconds of deliberate staleness in the target position the bot aims at. This is what produces
	 *  believable tracking lag on a strafing target without any explicit "miss" logic. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim", meta = (ClampMin = "0.0"))
	float AimTrackingLag = 0.12f;

	/** How much of the target's velocity the bot leads by. 1 = perfect prediction, 0 = always aims at where
	 *  the target was. Below 1 on purpose: over- and under-leading is most of what makes duels winnable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim", meta = (ClampMin = "0.0", ClampMax = "1.5"))
	float TargetLeadFraction = 0.55f;

	/** The bot will not pull the trigger until its aim is within this many degrees of the target. Stops it
	 *  hosing walls while its turn rate catches up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim", meta = (ClampMin = "0.1"))
	float FireAngleToleranceDegrees = 7.f;

	// --- Trigger discipline -----------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fire", meta = (ClampMin = "0.05"))
	float BurstDurationMin = 0.28f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fire", meta = (ClampMin = "0.05"))
	float BurstDurationMax = 0.75f;

	/** Trigger-off time between bursts. This is the window the player gets to peek or push into. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fire", meta = (ClampMin = "0.0"))
	float BurstRestMin = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fire", meta = (ClampMin = "0.0"))
	float BurstRestMax = 0.65f;

	/** Mag fraction at or below which the bot will reload as soon as it is out of sight. Above 0 so a good
	 *  bot tops up in cover instead of running dry mid-duel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fire", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ReloadAtAmmoFraction = 0.35f;

	// --- Movement tech ----------------------------------------------------------------------------

	/** Probability the bot takes an available slide / wall run / jump-dodge opportunity. This is the knob
	 *  that decides how much the bot looks like it is playing the same game as the player. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MovementTechChance = 0.65f;

	/** How often a slide opportunity is evaluated. Also the minimum spacing between slide attempts on top
	 *  of the movement component's own SlideCooldown. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.05"))
	float SlideCheckInterval = 0.55f;

	/** How often an evasive jump is considered while fighting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.05"))
	float JumpDodgeInterval = 1.4f;

	/**
	 * How long one strafe leg lasts before the bot commits to the other side.
	 *
	 * This is the direct fix for "the strafe direction flips every frame". A leg is a commitment: the side is
	 * chosen once, a destination is issued once, and neither is reconsidered until the leg expires. Below
	 * about 0.7s the bot reads as vibrating rather than as circling.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.2"))
	float StrafeLegDurationMin = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.2"))
	float StrafeLegDurationMax = 2.2f;

	// --- Tactics ----------------------------------------------------------------------------------

	/** Health fraction at or below which the bot breaks off and retreats instead of trading. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RetreatHealthFraction = 0.3f;

	/**
	 * Minimum time any chosen action runs before its latched conditions are even *looked at*.
	 *
	 * The commitment window. Nothing short of an interrupt can change the bot's mind inside it. This is the
	 * single most important number for how decisive the bot reads: at 0 the bot is the old one, and above
	 * about 1.5s it starts ignoring things a human would have reacted to.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float ActionMinCommitTime = 0.8f;

	/**
	 * Range the bot tries to hold while fighting, in cm. Scaled per weapon by
	 * AShooterAIController::GetPreferredRange, which multiplies it by 1.6 for a semi-automatic.
	 *
	 * Size this against the *map*, not against the weapon.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics", meta = (ClampMin = "100.0"))
	float PreferredEngagementRange = 600.f;

	/**
	 * Hysteresis band around PreferredEngagementRange, as a fraction.
	 *
	 * Approach stops once the bot is inside Preferred * (1 + this); Strafe gives up and re-approaches once
	 * the target opens past Preferred * (1 + 2*this). Two different thresholds on purpose - a single one
	 * makes the two actions swap every time the range wobbles across it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float EngagementRangeTolerance = 0.25f;

	/**
	 * Probability **per second** that a strafing bot breaks the current angle and crosses to a new one.
	 *
	 * Per second, not per selection: the selection pass now runs at irregular intervals (it only runs when an
	 * action is invalidated), so this is converted against the time actually elapsed since the last roll.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RepositionChance = 0.15f;

	/** Shortest a reposition runs. Below this it is not a reposition, it is a twitch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics", meta = (ClampMin = "0.1"))
	float RepositionMinDuration = 0.7f;

	/**
	 * Longest a single reposition may run before the bot goes back to fighting, even if it has not arrived.
	 * A reposition is meant to be a step off the current angle, not a lap of the arena.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics", meta = (ClampMin = "0.1"))
	float RepositionMaxDuration = 1.8f;

	/** Longest a single retreat runs before the bot turns back and fights. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics", meta = (ClampMin = "0.5"))
	float RetreatMaxDuration = 4.f;

	/**
	 * How long after a retreat the bot refuses to retreat again for health reasons.
	 *
	 * Health does not regenerate, so without this a bot that drops below RetreatHealthFraction retreats,
	 * times out, re-evaluates, and retreats again - kiting for the rest of the match and never trading. The
	 * cooldown forces it to come back and finish the fight it is losing, which is both better play and far
	 * better to fight against.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics", meta = (ClampMin = "0.0"))
	float RetreatCooldown = 7.f;

	/**
	 * Shortest interval between two TookDamage interrupts.
	 *
	 * An automatic weapon lands a round every FireTime. One interrupt per round would invalidate the current
	 * action several times a second, which is the commitment model switched off exactly when the bot most
	 * needs to hold a plan - so being shot at continuously would make it *more* indecisive, not less.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics", meta = (ClampMin = "0.0"))
	float DamageInterruptCooldown = 1.2f;

	/** Longest a hunt runs before it re-plans from scratch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics", meta = (ClampMin = "0.5"))
	float HuntMaxDuration = 6.f;
};

/**
 * The authored presets. Kept as code rather than as data assets so a fresh BP_ShooterAIController is
 * playable with nothing configured - pick a skill on the Blueprint and the numbers arrive with it.
 */
inline FShooterBotDifficulty GetShooterBotDifficultyPreset(EShooterBotSkill Skill)
{
	FShooterBotDifficulty D;

	switch (Skill)
	{
	case EShooterBotSkill::Recruit:
		// Slow to react, slow to turn, sprays wide, rarely uses tech, and barely rewards a still target.
		// Loses most duels to a player who aims at all, which is what a first-tier practice opponent should do.
		D.ReactionTime = 0.6f;
		D.MaxTurnRateDegrees = 140.f;
		D.AimErrorInitialDegrees = 8.f;
		D.AimErrorSettledDegrees = 2.6f;
		D.AimSettleTime = 1.3f;
		D.AimTrackingLag = 0.2f;
		D.TargetLeadFraction = 0.25f;
		D.BurstRestMin = 0.5f;
		D.BurstRestMax = 1.f;
		D.MovementTechChance = 0.25f;
		D.RetreatHealthFraction = 0.45f;
		D.ActionMinCommitTime = 1.1f;
		D.StillnessRiseTime = 0.7f;
		D.AimErrorStillnessResponse = FShooterAIStillnessResponse(2.f, 0.5f, 2.f);
		D.TurnRateStillnessResponse = FShooterAIStillnessResponse(0.7f, 1.3f, 1.5f);
		D.ReactionStillnessResponse = FShooterAIStillnessResponse(1.3f, 0.7f, 2.f);
		break;

	case EShooterBotSkill::Veteran:
		// Reacts fast, turns fast, settles tight, uses tech constantly, and punishes a stationary player
		// almost immediately. Still not perfect: the settled error and the lead fraction stay short of 0 and
		// 1 deliberately, so the player can always win by out-moving it rather than only by out-aiming it.
		D.ReactionTime = 0.16f;
		D.MaxTurnRateDegrees = 320.f;
		D.AimErrorInitialDegrees = 3.5f;
		D.AimErrorSettledDegrees = 0.6f;
		D.AimSettleTime = 0.55f;
		D.AimTrackingLag = 0.07f;
		D.TargetLeadFraction = 0.8f;
		D.BurstDurationMax = 0.9f;
		D.BurstRestMin = 0.18f;
		D.BurstRestMax = 0.38f;
		D.MovementTechChance = 0.9f;
		D.RepositionChance = 0.25f;
		D.RepositionMaxDuration = 2.f;
		D.RetreatHealthFraction = 0.22f;
		D.ActionMinCommitTime = 0.6f;
		D.StillnessRiseTime = 0.22f;
		D.AimErrorStillnessResponse = FShooterAIStillnessResponse(2.8f, 0.1f, 1.2f);
		D.TurnRateStillnessResponse = FShooterAIStillnessResponse(0.5f, 1.8f, 1.f);
		D.ReactionStillnessResponse = FShooterAIStillnessResponse(1.6f, 0.3f, 1.2f);
		break;

	case EShooterBotSkill::Regular:
	case EShooterBotSkill::Custom:
	default:
		// The struct defaults are the Regular preset.
		break;
	}

	return D;
}
