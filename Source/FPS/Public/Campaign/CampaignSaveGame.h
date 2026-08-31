// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "CampaignSaveGame.generated.h"

/**
 * Campaign progress. Deliberately its own save object and its own slot: the campaign is a separate mode
 * from the ranked ladder, so nothing here may reach the persistent multiplayer inventory, and nothing
 * there may reach this.
 *
 * Only the reached index is stored, never a weapon list - the kit is derived from the level set, so
 * re-authoring the unlock order fixes old saves instead of stranding them.
 */
UCLASS()
class FPS_API UCampaignSaveGame : public USaveGame
{
	GENERATED_BODY()

public:

	/** The level the player is currently on. */
	UPROPERTY(BlueprintReadWrite, Category = "Campaign")
	int32 CurrentLevelIndex = 0;

	/** Furthest level ever reached, for a level-select screen. Never decreases. */
	UPROPERTY(BlueprintReadWrite, Category = "Campaign")
	int32 HighestLevelReached = 0;

	/** Levels cleared this run, for a completion readout. */
	UPROPERTY(BlueprintReadWrite, Category = "Campaign")
	int32 LevelsCleared = 0;

	static const TCHAR* GetSlotName() { return TEXT("Campaign"); }

	/** Loads the campaign slot, creating an empty save object if the slot does not exist yet. */
	UFUNCTION(BlueprintCallable, Category = "Campaign")
	static UCampaignSaveGame* LoadOrCreate();

	UFUNCTION(BlueprintCallable, Category = "Campaign")
	void Save() const;

	/** Back to level 0. HighestLevelReached is cleared too - a new campaign is a new run, and leaving a
	 *  level-select unlocked past where the player has actually played would undercut the escalation. */
	UFUNCTION(BlueprintCallable, Category = "Campaign")
	void ResetProgress();
};
