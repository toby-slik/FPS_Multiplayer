// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "ShooterGameModeBase.h"
#include "CampaignGameMode.generated.h"

struct FCampaignLevel;
class UCampaignLevelSet;
class UCampaignSaveGame;
class UHealthComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCampaignArenaCleared);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCampaignLevelStarted, int32, LevelIndex);

/**
 * The single-player campaign.
 *
 * One level is one arena holding one bot, followed by a combat-free traversal section. Kill the bot, the
 * door opens, run the traversal, hit the exit volume, next level - with a harder bot and, at the levels
 * that grant one, a better gun. See GDD section 5.
 *
 * A separate mode from the 1v1 ladder in every sense: separate game mode, separate save slot, no tier,
 * no stealing, no matchmaking. It shares only the character, the weapons and the bot.
 */
UCLASS()
class FPS_API ACampaignGameMode : public AShooterGameModeBase
{
	GENERATED_BODY()

public:

	ACampaignGameMode();

	/** Bots never respawn here - the arena is cleared when its bot dies. The player's death is handled by
	 *  PlayerDeathBehaviour instead of by the base class's random-PlayerStart respawn. */
	virtual void RequestRespawn(ACharacter* Character, AController* Controller) override;

	/** The player's kit is the cumulative unlock list up to the current level; bots take the level's
	 *  BotWeapons when it specifies any. */
	virtual void GetStartingLoadout(const AController* Controller, TArray<TSubclassOf<AWeapon>>& OutWeapons) const override;

	/** Index of the level being played. Resolved from the level set by map name, falling back to the save. */
	UFUNCTION(BlueprintPure, Category = "Campaign")
	int32 GetCurrentLevelIndex() const { return CurrentLevelIndex; }

	UFUNCTION(BlueprintPure, Category = "Campaign")
	bool IsArenaCleared() const { return bArenaCleared; }

	/**
	 * Adds a bot to the set this arena is waiting on, applying the level's difficulty preset.
	 *
	 * Called automatically for every AI pawn already in the level. Call it by hand only for a bot spawned
	 * after the level has started, and call it *before* that bot can die or the clear will never fire.
	 */
	UFUNCTION(BlueprintCallable, Category = "Campaign")
	void RegisterEnemy(APawn* EnemyPawn);

	/** Saves progress and opens the next level's map. Called by ACampaignExitVolume. */
	UFUNCTION(BlueprintCallable, Category = "Campaign")
	void AdvanceToNextLevel();

	/** Reloads the current level's map, losing nothing but the run through this arena. */
	UFUNCTION(BlueprintCallable, Category = "Campaign")
	void RestartCurrentLevel();

	/** Fired on the server the moment the last bot in the arena dies. Doors listen for this. */
	UPROPERTY(BlueprintAssignable, Category = "Campaign")
	FCampaignArenaCleared OnArenaCleared;

	UPROPERTY(BlueprintAssignable, Category = "Campaign")
	FCampaignLevelStarted OnCampaignLevelStarted;

protected:

	virtual void BeginPlay() override;

	/** The ordered campaign. Required - without it the mode cannot know which level it is or what to grant. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Campaign")
	TObjectPtr<UCampaignLevelSet> LevelSet;

	/**
	 * Forces a level index instead of resolving one, for testing a mid-campaign level from the editor
	 * without playing up to it. -1 = resolve normally. Leave at -1 in anything shipped.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Campaign", meta = (ClampMin = "-1"))
	int32 LevelIndexOverride = -1;

	/** Seconds between the player dying and the level reloading. Long enough to see the death. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Campaign", meta = (ClampMin = "0.0"))
	float PlayerDeathRestartDelay = 2.f;

	/** Opened when the last level is cleared. Leave unset to simply stay in the final arena. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Campaign", meta = (AllowedClasses = "/Script/Engine.World"))
	TSoftObjectPtr<UWorld> CampaignCompleteLevel;

	/** Seconds between hitting the exit volume and the next map opening, so a transition can play. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Campaign", meta = (ClampMin = "0.0"))
	float LevelTransitionDelay = 0.5f;

	UFUNCTION()
	void HandleEnemyDeath();

private:

	/** Collects every AI-controlled pawn already placed in the level. Deferred to the next tick because a
	 *  placed pawn's AutoPossessAI controller does not exist yet while the game mode is beginning play. */
	void GatherPlacedEnemies();

	void ApplyLevelSettingsToEnemy(APawn* EnemyPawn);

	const FCampaignLevel* GetCurrentLevel() const;

	int32 ResolveLevelIndex() const;

	UPROPERTY()
	TObjectPtr<UCampaignSaveGame> SaveData;

	/** Health components of the bots this arena is waiting on. Held rather than a pawn list because the
	 *  pawn is destroyed shortly after death, and the count has to survive that. */
	UPROPERTY()
	TArray<TObjectPtr<UHealthComponent>> TrackedEnemies;

	int32 CurrentLevelIndex = 0;

	int32 LivingEnemyCount = 0;

	bool bArenaCleared = false;

	/** Latched once the exit has been taken, so a player standing in the volume cannot fire the transition
	 *  twice while the delay timer runs. */
	bool bTransitionStarted = false;

	FTimerHandle TransitionTimer;
	FTimerHandle RestartTimer;
};
