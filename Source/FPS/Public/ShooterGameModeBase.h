// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ShooterGameModeBase.generated.h"

/**
 * 
 */
UCLASS()
class FPS_API AShooterGameModeBase : public AGameModeBase
{
	GENERATED_BODY()
	
public:

	void RequestRespawn(ACharacter* Character, AController* Controller);

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
