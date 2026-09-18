// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CampaignTriggerDoor.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

/**
 * A door that slides up once, the moment a player-controlled pawn enters its trigger volume.
 *
 * Unlike ACampaignDoor, this has no relationship to the arena/bot state at all - it is a plain
 * level-dressing beat (the "you have to walk up to it" threshold into an arena, or any other
 * one-way gate a level wants), so it never touches ACampaignGameMode. Place the trigger a short
 * distance before the door so the slide has time to finish before the player arrives.
 */
UCLASS()
class FPS_API ACampaignTriggerDoor : public AActor
{
	GENERATED_BODY()

public:

	ACampaignTriggerDoor();

	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintPure, Category = "Campaign")
	bool IsTriggered() const { return bTriggered; }

protected:

	virtual void BeginPlay() override;

	UFUNCTION()
	void HandleTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Fires once, the moment the trigger is entered - the animation/sound cue for this map. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Campaign")
	void OnDoorTriggered();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Campaign")
	TObjectPtr<UStaticMeshComponent> DoorMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Campaign")
	TObjectPtr<UBoxComponent> Trigger;

	/** Local-space distance DoorMesh rises once triggered. Aim it so the door ends up hidden inside
	 *  solid geometry above the opening (a lintel, a ceiling void), not just floating in the air. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Campaign", meta = (ClampMin = "0.0"))
	float SlideDistance = 400.f;

	/** Seconds for the slide to complete. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Campaign", meta = (ClampMin = "0.01"))
	float SlideDuration = 1.2f;

	/**
	 * Drop the door's collision the instant it is triggered rather than waiting for the slide to finish.
	 *
	 * True is the safe default: a player who reaches the doorway before the animation completes must
	 * never be blocked by a door that is visibly already moving. Set false only if the door's real-world
	 * silhouette should still block early for some deliberate reason.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Campaign")
	bool bDropCollisionOnTriggered = true;

private:

	FVector ClosedRelativeLocation = FVector::ZeroVector;
	float ElapsedTime = 0.f;
	bool bTriggered = false;
};
