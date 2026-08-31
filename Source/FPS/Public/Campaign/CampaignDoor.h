// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CampaignDoor.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

/**
 * The arena door. Shut until the level's bot is dead, then open onto the traversal section.
 *
 * The kill-to-open link is the campaign's core beat, so it lives in C++ rather than in per-map Blueprint
 * wiring: drop one of these in the wall and it is connected. OnDoorOpened is there for the animation and
 * the sound, which are per-map concerns.
 */
UCLASS()
class FPS_API ACampaignDoor : public AActor
{
	GENERATED_BODY()

public:

	ACampaignDoor();

	UFUNCTION(BlueprintPure, Category = "Campaign")
	bool IsOpen() const { return bOpen; }

	/** Opens the door regardless of the arena state. For a scripted opening; the normal path is the
	 *  campaign game mode's OnArenaCleared. */
	UFUNCTION(BlueprintCallable, Category = "Campaign")
	void OpenDoor();

	/** Play the animation, the sound, whatever this map wants. The collision and visibility handling in
	 *  bHideOnOpen has already run by the time this fires. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Campaign")
	void OnDoorOpened();

protected:

	virtual void BeginPlay() override;

	UFUNCTION()
	void HandleArenaCleared();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Campaign")
	TObjectPtr<UStaticMeshComponent> DoorMesh;

	/**
	 * Hide the mesh and drop its collision when the door opens.
	 *
	 * True is the blockout behaviour - the door vanishes and the player runs through. Set it false once
	 * the door has a real open animation, and move the collision off in OnDoorOpened when the animation
	 * has actually finished, or the player runs through a door that is still visibly shut.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Campaign")
	bool bHideOnOpen = true;

private:

	bool bOpen = false;
};
