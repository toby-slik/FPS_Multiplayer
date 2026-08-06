// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "ShooterAITypes.generated.h"

/**
 * The bot's tactical state. Deliberately exclusive - a 1v1 bot is only ever doing one of these things, and
 * modelling that as a plain enum rather than as a behaviour tree keeps the whole decision layer in C++ where
 * it is diffable in git. See the note at the top of AShooterAIController for why there is no BT asset.
 */
UENUM(BlueprintType)
enum class EShooterBotState : uint8
{
	/** No target known. Holds position. */
	Idle UMETA(DisplayName = "Idle"),

	/** Target exists but is not visible. Moves to its last known position, then sweeps. */
	Hunt UMETA(DisplayName = "Hunt"),

	/** Target visible. Fights: strafes, holds preferred range, shoots. */
	Engage UMETA(DisplayName = "Engage"),

	/** Deliberately breaking the current angle - crossing to a new position rather than trading in place. */
	Reposition UMETA(DisplayName = "Reposition"),

	/** Low health or empty mag. Breaks line of sight, then reloads. */
	Retreat UMETA(DisplayName = "Retreat")
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

	/** Delay between the target becoming visible and the bot being allowed to act on it. The single most
	 *  important difficulty knob: it is what gives the player the first shot on an easy bot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception", meta = (ClampMin = "0.0"))
	float ReactionTime = 0.35f;

	/** How long the bot keeps hunting a target's last known position after losing sight of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception", meta = (ClampMin = "0.0"))
	float TargetMemoryTime = 4.f;

	// --- Aim --------------------------------------------------------------------------------------

	/** Hard cap on how fast the bot can turn, degrees per second. This is its mouse hand. Uncapped aim is
	 *  what makes a bot feel like an aimbot even when its accuracy is poor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim", meta = (ClampMin = "10.0"))
	float MaxTurnRateDegrees = 220.f;

	/** Aim offset the moment a target is acquired, in degrees. */
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

	/** How often an evasive jump is considered while engaging. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.05"))
	float JumpDodgeInterval = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.1"))
	float StrafeChangeIntervalMin = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.1"))
	float StrafeChangeIntervalMax = 1.9f;

	// --- Tactics ----------------------------------------------------------------------------------

	/** Health fraction at or below which the bot breaks off and retreats instead of trading. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RetreatHealthFraction = 0.3f;

	/** How often the state machine re-decides. Not per frame on purpose: a bot that reconsiders every frame
	 *  oscillates between states at the boundaries and reads as indecisive rather than as smart. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics", meta = (ClampMin = "0.02"))
	float DecisionInterval = 0.25f;

	/** Range the bot tries to hold while engaging, in cm. It closes when further than this and backs off
	 *  when much closer. Scaled per weapon by AShooterAIController::GetPreferredRange. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics", meta = (ClampMin = "100.0"))
	float PreferredEngagementRange = 1200.f;

	/** Probability that, on any given decision tick while engaging, the bot breaks the current angle and
	 *  crosses to a new one instead of continuing to trade from where it stands. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RepositionChance = 0.15f;
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
		// Slow to react, slow to turn, sprays wide, rarely uses tech. Loses most duels to a player who
		// aims at all, which is what a first-tier practice opponent should do.
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
		break;

	case EShooterBotSkill::Veteran:
		// Reacts fast, turns fast, settles tight, uses tech constantly. Still not perfect: the settled
		// error and the lead fraction stay short of 0 and 1 deliberately, so the player can always win a
		// duel by out-moving it rather than only by out-aiming it.
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
		D.RetreatHealthFraction = 0.22f;
		break;

	case EShooterBotSkill::Regular:
	case EShooterBotSkill::Custom:
	default:
		// The struct defaults are the Regular preset.
		break;
	}

	return D;
}
