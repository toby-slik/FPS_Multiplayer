// Copyright Druid Mechanics

#include "Campaign/CampaignSaveGame.h"

#include "Kismet/GameplayStatics.h"

UCampaignSaveGame* UCampaignSaveGame::LoadOrCreate()
{
	if (UGameplayStatics::DoesSaveGameExist(GetSlotName(), 0))
	{
		if (UCampaignSaveGame* Loaded = Cast<UCampaignSaveGame>(UGameplayStatics::LoadGameFromSlot(GetSlotName(), 0)))
		{
			return Loaded;
		}

		UE_LOG(LogTemp, Warning, TEXT("Campaign save slot exists but could not be loaded; starting a fresh campaign"));
	}

	return Cast<UCampaignSaveGame>(UGameplayStatics::CreateSaveGameObject(StaticClass()));
}

void UCampaignSaveGame::Save() const
{
	// Const because saving is not a state change to the campaign - the cast is only to satisfy the
	// GameplayStatics signature, which does not take a const save object.
	UGameplayStatics::SaveGameToSlot(const_cast<UCampaignSaveGame*>(this), GetSlotName(), 0);
}

void UCampaignSaveGame::ResetProgress()
{
	CurrentLevelIndex = 0;
	HighestLevelReached = 0;
	LevelsCleared = 0;
}
