// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "AI/ShooterAITypes.h"
#include "Engine/World.h"
#include "CampaignTypes.generated.h"

class AWeapon;

/**
 * One campaign level: an arena holding a single bot, plus the traversal section that leads out of it.
 *
 * The whole level sequence lives in one UCampaignLevelSet rather than being configured per-map, because
 * the two things that have to escalate - bot skill and the player's kit - only make sense read in order.
 * Per-map configuration would let level 3 hand out a worse gun than level 2 with nothing to catch it.
 */
USTRUCT(BlueprintType)
struct FCampaignLevel
{
	GENERATED_BODY()

	/** Shown on the level-start card. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	FText DisplayName;

	/** The map to open for this level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign", meta = (AllowedClasses = "/Script/Engine.World"))
	TSoftObjectPtr<UWorld> Level;

	/** Difficulty preset pushed onto every bot in the arena when the level starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	EShooterBotSkill BotSkill = EShooterBotSkill::Recruit;

	/**
	 * Weapons *added* at this level, not the full loadout.
	 *
	 * The player's kit is the union of this array across every level up to and including the current one,
	 * so the first level listing only a pistol is what makes the campaign start on a pistol. Leave later
	 * levels empty to keep the previous kit unchanged.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	TArray<TSubclassOf<AWeapon>> WeaponsUnlocked;

	/** What the bot carries here. Empty means the bot Blueprint's own DefaultWeaponClass is left alone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	TArray<TSubclassOf<AWeapon>> BotWeapons;
};
