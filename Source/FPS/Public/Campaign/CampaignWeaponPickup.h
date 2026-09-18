// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CampaignWeaponPickup.generated.h"

class AWeapon;
class UBoxComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class USoundBase;
class UStaticMeshComponent;

/**
 * A weapon lying in the level, collected by walking into it.
 *
 * This is how a campaign level's newly unlocked weapon actually reaches the player. ACampaignGameMode's
 * starting loadout is deliberately the cumulative kit from *before* the current level, so "unlocked here"
 * reads as "found here" rather than appearing in the player's hands at spawn - the first arena starts the
 * player with literally nothing and one of these holding the pistol.
 *
 * Place the actor on the floor: the trigger box and the display mesh both sit above the actor's origin, so
 * the origin is the point that should touch the ground.
 *
 * Collection is authority-only. bCollected replicates so the pickup visibly disappears on every machine even
 * though only the server decides it was taken.
 */
UCLASS()
class FPS_API ACampaignWeaponPickup : public AActor
{
	GENERATED_BODY()

public:

	ACampaignWeaponPickup();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void OnConstruction(const FTransform& Transform) override;

	UFUNCTION(BlueprintPure, Category = "Campaign")
	bool IsCollected() const { return bCollected; }

	/**
	 * Play the grab sound, the flash, whatever this map wants. The pickup has already been hidden and its
	 * collision dropped by the time this fires.
	 *
	 * Collector is only known on the authority - on a remote client this fires from OnRep_Collected with
	 * null, because who walked into it is not replicated. Anything that needs the collector must handle that.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Campaign")
	void OnPickedUp(APawn* Collector);

protected:

	virtual void BeginPlay() override;

	UFUNCTION()
	void HandleOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Authority-only. Latches bCollected, hides the pickup here and, through the rep notify, everywhere else. */
	void Auth_Collect(APawn* Collector);

	UFUNCTION()
	void OnRep_Collected();

	/** The visible half of being collected. Runs on every machine - directly on the authority, from the rep
	 *  notify on everyone else - so nothing cosmetic here may touch gameplay state. */
	void Local_ApplyCollected(APawn* Collector);

	/** Pulls the display mesh off the weapon class so the pickup looks like the gun it grants with no asset
	 *  authoring. Also runs in the editor, so the level designer sees the right weapon while placing it. */
	void ApplyDisplayMesh();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Campaign")
	TObjectPtr<UBoxComponent> Box;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Campaign")
	TObjectPtr<UStaticMeshComponent> PedestalMesh;

	/** The floating weapon. Purely decorative - it is not the granted AWeapon, which is spawned into the
	 *  player's inventory by UCombatComponent::Auth_GrantWeapon. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Campaign")
	TObjectPtr<USkeletalMeshComponent> WeaponMesh;

	/** The weapon this pickup grants. Required - with nothing set, walking into it does nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Campaign")
	TSubclassOf<AWeapon> WeaponClass;

	/**
	 * Equip the granted weapon when the player is carrying nothing at all.
	 *
	 * True is what makes the first arena work: the player spawns unarmed, and without this they would walk
	 * away from the pistol still holding air until they pressed the cycle key. It never takes a weapon out of
	 * the player's hands - a pickup found mid-campaign always just joins the inventory.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Campaign")
	bool bEquipIfUnarmed = true;

	/**
	 * Disappear even when the player already owns this weapon type.
	 *
	 * True avoids the worse outcome of the two: a pickup the player can walk through repeatedly with no
	 * feedback. Set it false while testing if you want the pickup to stay put until it actually grants
	 * something.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Campaign")
	bool bCollectIfAlreadyOwned = true;

	/** Played at the pickup's location on every machine that can hear it. Optional. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Campaign")
	TObjectPtr<USoundBase> PickupSound;

	/** Overrides the mesh taken from the weapon class. Leave unset to show the weapon's own third-person mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Campaign")
	TObjectPtr<USkeletalMesh> DisplayMeshOverride;

	/** Degrees per second the floating weapon spins. 0 stops it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Campaign|Motion")
	float SpinRate = 60.f;

	/** Centimetres the floating weapon rises and falls. 0 stops the bob. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Campaign|Motion", meta = (ClampMin = "0.0"))
	float BobHeight = 6.f;

	/** Bobs per second. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Campaign|Motion", meta = (ClampMin = "0.0"))
	float BobRate = 0.6f;

	/**
	 * Seconds between being collected and the actor being destroyed on the server, leaving OnPickedUp room to
	 * play something out first. 0 keeps the (hidden, non-colliding) actor alive for the rest of the level.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Campaign", meta = (ClampMin = "0.0"))
	float DestroyDelay = 2.f;

private:

	UPROPERTY(ReplicatedUsing = OnRep_Collected)
	bool bCollected = false;

	/** Relative location the bob oscillates around, captured before anything moves the display mesh. */
	FVector WeaponMeshRestLocation = FVector::ZeroVector;

	float BobAccumulator = 0.f;
};
