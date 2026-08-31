// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Campaign/CampaignTypes.h"
#include "CampaignLevelSet.generated.h"

/**
 * The ordered list of campaign levels. One asset per campaign.
 *
 * A data asset rather than a table on the game mode: every campaign map needs the same list to answer
 * "which level am I, and what has the player unlocked by now", and duplicating that per map is how the
 * progression drifts out of sync.
 */
UCLASS(BlueprintType)
class FPS_API UCampaignLevelSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Campaign", meta = (TitleProperty = "DisplayName"))
	TArray<FCampaignLevel> Levels;

	UFUNCTION(BlueprintPure, Category = "Campaign")
	int32 GetLevelCount() const { return Levels.Num(); }

	UFUNCTION(BlueprintPure, Category = "Campaign")
	bool IsValidLevelIndex(int32 Index) const { return Levels.IsValidIndex(Index); }

	/** Index of the level whose map matches PackageName, or INDEX_NONE. Used to work out which level the
	 *  game mode is standing in when the map was opened directly from the editor. */
	int32 FindLevelIndexByPackageName(const FString& PackageName) const;

	/**
	 * The player's full kit at LevelIndex: every WeaponsUnlocked entry from level 0 up to and including it,
	 * de-duplicated, in unlock order. Weapon order matters - Equip takes index 0 - so the pistol stays the
	 * default draw until a later level pushes something ahead of it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Campaign")
	void BuildPlayerLoadout(int32 LevelIndex, TArray<TSubclassOf<AWeapon>>& OutWeapons) const;
};
