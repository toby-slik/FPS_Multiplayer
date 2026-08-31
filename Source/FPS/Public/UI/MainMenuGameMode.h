// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MainMenuGameMode.generated.h"

class UCampaignLevelSet;
class UMainMenuWidget;

/**
 * The front-end. Set this as the GameMode override on the startup map.
 *
 * Deliberately not derived from AShooterGameModeBase: the menu spawns no pawn, has no respawn, and no
 * loadout - inheriting that machinery would only mean carrying rules that must never fire here.
 */
UCLASS()
class FPS_API AMainMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:

	AMainMenuGameMode();

protected:

	virtual void BeginPlay() override;

	/** The menu widget class. Defaults to the code-authored UMainMenuWidget, so the menu works with no
	 *  asset to create; point it at a WBP once there is one to style. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Menu")
	TSubclassOf<UMainMenuWidget> MenuWidgetClass;

	/**
	 * The campaign the menu starts. Soft, and defaulted to the conventional asset path, so a project that
	 * names its level set DA_Campaign needs no configuration at all - the menu resolves it on load and
	 * logs plainly if it is missing.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Menu")
	TSoftObjectPtr<UCampaignLevelSet> LevelSet;

private:

	UPROPERTY()
	TObjectPtr<UMainMenuWidget> MenuWidget;
};
