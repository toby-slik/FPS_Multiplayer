// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ShooterGameModeBase.generated.h"

class AWeapon;

/**
 * 
 */
UCLASS()
class FPS_API AShooterGameModeBase : public AGameModeBase
{
	GENERATED_BODY()
	
public:

	/** Virtual because the campaign does not respawn at all: its bots die once and its player restarts the
	 *  level. See ACampaignGameMode. */
	virtual void RequestRespawn(ACharacter* Character, AController* Controller);

	/**
	 * Weapons this controller starts with, overriding the pawn's own DefaultWeaponClass.
	 *
	 * Left empty by the base mode, which is what "use the pawn's list" means. Queried by
	 * UCombatComponent::SpawnInventory rather than pushed by the mode, because inventory is spawned from
	 * PossessedBy and SetPlayerDefaults does not run until after that - anything pushed would arrive to a
	 * pawn that had already drawn its gun.
	 */
	virtual void GetStartingLoadout(const AController* Controller, TArray<TSubclassOf<AWeapon>>& OutWeapons) const {}

	/**
	 * Sends AI controllers to BotPawnClass and everyone else to DefaultPawnClass.
	 *
	 * RequestRespawn routes through RestartPlayerAtPlayerStart, which asks this function what to spawn. Without
	 * the override a respawning bot is handed the *player's* pawn class, so any bot-specific configuration on
	 * its own Blueprint is silently lost on its first death - the bot works, then quietly stops being the bot.
	 */
	virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;

protected:

	/** Pawn class spawned for AI controllers. Leave unset to fall back to DefaultPawnClass. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|AI")
	TSubclassOf<APawn> BotPawnClass;
};
