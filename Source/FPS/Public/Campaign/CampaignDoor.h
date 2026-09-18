// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CampaignDoor.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

/**
 * A door actor with two independent jobs, sharing the same mesh/collision mechanics:
 *
 *  - The arena door: shut until the level's bot is dead, then open onto the traversal section. The
 *    kill-to-open link is the campaign's core beat, so it lives in C++ rather than in per-map Blueprint
 *    wiring - drop one of these in the wall and it is connected.
 *  - The airlock door: placed just past a ACampaignExitVolume. It opens the same way the arena door does
 *    (on OnArenaCleared, which has already fired by the time the player can physically reach it), but is
 *    additionally closed explicitly by CloseDoor() the moment the exit volume is triggered, sealing off
 *    retreat while the level transition (fade + load) plays out.
 *
 * OnDoorOpened/OnDoorClosed are there for the animation and the sound, which are per-map concerns.
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

	/** Closes an open door - the airlock use. Never called by the arena-door path; nothing here listens
	 *  for an event that would reopen it, so once closed this way it stays closed. */
	UFUNCTION(BlueprintCallable, Category = "Campaign")
	void CloseDoor();

	/** Play the animation, the sound, whatever this map wants. The collision and visibility handling in
	 *  bHideOnOpen has already run by the time this fires. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Campaign")
	void OnDoorOpened();

	/** Play the close animation/sound. The collision and visibility handling in bHideOnOpen has already
	 *  run by the time this fires (mesh made visible/solid again if bHideOnOpen hid it on open). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Campaign")
	void OnDoorClosed();

protected:

	virtual void BeginPlay() override;

	UFUNCTION()
	void HandleArenaCleared();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Campaign")
	TObjectPtr<UStaticMeshComponent> DoorMesh;

	/**
	 * Hide the mesh and drop its collision when the door opens (and, symmetrically, restore both when it
	 * closes).
	 *
	 * True is the blockout behaviour - the door vanishes and the player runs through. Set it false once
	 * the door has a real open animation, and move the collision in OnDoorOpened/OnDoorClosed when the
	 * animation has actually finished, or the player runs through a door that is still visibly shut (or
	 * past one that looks closed but isn't yet).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Campaign")
	bool bHideOnOpen = true;

private:

	bool bOpen = false;
};
