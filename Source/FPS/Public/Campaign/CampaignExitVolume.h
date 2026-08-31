// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CampaignExitVolume.generated.h"

class UBoxComponent;

/**
 * The end of the traversal section. Walking into it takes the player to the next campaign level.
 *
 * Kept separate from the door so the arena beat and the level change are not the same trigger: the door
 * opens the moment the bot dies, and the level only changes once the player has actually run the parkour
 * between the two arenas.
 */
UCLASS()
class FPS_API ACampaignExitVolume : public AActor
{
	GENERATED_BODY()

public:

	ACampaignExitVolume();

protected:

	virtual void BeginPlay() override;

	UFUNCTION()
	void HandleOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Campaign")
	TObjectPtr<UBoxComponent> Box;

	/** Refuse the transition until the arena's bot is dead. Guards against a route that skips the fight. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Campaign")
	bool bRequireArenaCleared = true;
};
