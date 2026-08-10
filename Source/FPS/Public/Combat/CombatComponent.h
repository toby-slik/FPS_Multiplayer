

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "ShooterTypes/AttachmentTypes.h"
#include "CombatComponent.generated.h"


class UAttachmentData;
class UMaterialInstanceDynamic;
class USoundBase;
class USoundConcurrency;
class UWeaponData;
class AWeapon;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FReticleChanged, UMaterialInstanceDynamic*, ReticleDynMatInst, const FReticleParams&, ReticleParams, bool, bCurrentlyTargetingPlayer);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FAmmoCounterChanged, UMaterialInstanceDynamic*, AmmoCounterDynMatInst, int32, RoundsCurrent, int32, RoundsMax);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FRoundFired, int32, RoundsCurrent, int32, RoundsMax, int32, RoundsInReserve);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAimingStatusChanged, bool, bIsAiming);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTargetingPlayerStatusChanged, bool, bIsAiming);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FCurrentReserveAmmoChanged, int32, RoundsInReserve, int32, RoundsInWeapon, UMaterialInterface*, WeaponIconMaterial);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FHitConfirmed, bool, bLethal, bool, bHeadshot, float, DamageDealt);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class FPS_API UCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombatComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;
	
	UFUNCTION(BlueprintPure, Category = "FPS|Combat")
	static UCombatComponent* FindCombatComponent(const AActor* Actor) { return ( IsValid(Actor) ? Actor->FindComponentByClass<UCombatComponent>() : nullptr ); }
	
	// Cycle to the next weapon in the inventory
	void Initiate_CycleWeapon();
	void Initiate_FireWeapon_Pressed();
	void Initiate_FireWeapon_Released();
	void Initiate_ReloadWeapon();
	void Initiate_Aim_Pressed();
	void Initiate_Aim_Released();
	
	void Notify_CycleWeapon();
	void Notify_ReloadWeapon();
	
	UPROPERTY(BlueprintAssignable)
	FReticleChanged OnReticleChanged;
	
	UPROPERTY(BlueprintAssignable)
	FAmmoCounterChanged OnAmmoCounterChanged;
	
	UPROPERTY(BlueprintAssignable)
	FRoundFired OnRoundFired;
	
	UPROPERTY(BlueprintAssignable)
	FAimingStatusChanged OnAimingStatusChanged;
	
	UPROPERTY(BlueprintAssignable)
	FTargetingPlayerStatusChanged OnTargetingPlayerStatusChanged;
	
	UPROPERTY(BlueprintAssignable)
	FCurrentReserveAmmoChanged OnCurrentReserveAmmoChanged;

	/** Fired on the shooting client only, once the server has confirmed the round landed on a player. */
	UPROPERTY(BlueprintAssignable)
	FHitConfirmed OnHitConfirmed;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Weapon")
	TObjectPtr<UWeaponData> WeaponData;
	
	void Equip(AWeapon* Weapon);
	void EquipWeapon(AWeapon* Weapon);
	
	UFUNCTION(Server, Reliable)
	void Server_EquipWeapon(AWeapon* Weapon);
	
	void SpawnInventory();
	void DestroyInventory();
	
	UPROPERTY(BlueprintReadOnly, Replicated)
	bool bAiming;
	
	UPROPERTY(Transient, BlueprintReadOnly, ReplicatedUsing = OnRep_CurrentWeapon)
	TObjectPtr<AWeapon> CurrentWeapon;
	
	void InitializeWeaponWidgets() const;

	// --- Attachments ---------------------------------------------------------------------------------

	/**
	 * Authority-only entry point for putting an attachment on one of this pawn's weapons. There is deliberately
	 * no Initiate_/Server_ pair and no client path: unlike firing, nothing about equipping needs to feel
	 * instant, and the loadout screen, the per-round rarity reroll and the post-match steal are all decisions
	 * the server makes anyway. Prediction here would only create a state a client could disagree about.
	 *
	 * Weapon must be in this component's Inventory. Returns false and changes nothing otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "FPS|Attachments")
	bool Auth_EquipAttachment(AWeapon* Weapon, UAttachmentData* Definition, EAttachmentRarity InstanceRarity);

	UFUNCTION(BlueprintCallable, Category = "FPS|Attachments")
	bool Auth_RemoveAttachment(AWeapon* Weapon, EAttachmentSlot Slot);

	/**
	 * Called by AWeapon on every machine once its attachment list has changed and its effective stats have been
	 * rebuilt. Only redraws the ammo HUD - the mag size it is displaying may have just moved. Broadcasting on a
	 * dedicated server is harmless; there are no listeners there.
	 */
	void NotifyAttachmentsChanged(const AWeapon* Weapon) const;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentReserveAmmo)
	int32 CurrentReserveAmmo;
	
	
protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
	UPROPERTY(EditDefaultsOnly, Category = "FPS|Weapon")
	float TraceLength;

	/**
	 * Seconds between targeting-highlight traces in TickComponent. 0 = every frame, which is the intended
	 * value - see the note at the throttle itself. Raising this trades highlight responsiveness for a saving
	 * that is below the noise floor at 1v1, so only raise it with a profile in hand.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "FPS|Reticle")
	float TargetingTraceInterval;

	UFUNCTION()
	void BlendOut_CycleWeapon(UAnimMontage* Montage, bool bInterrupted);

private:
	
	TMap<FGameplayTag, int32> ReserveAmmo;
	float TargetingTraceAccumulator;
	bool bHitPlayer;
	bool bHitPlayerLastFrame;
	bool bTriggerPressed;
	bool bInventorySpawned;
	FTimerHandle FireTimer;
	double LastServerFireTime;
	void FireTimerFinished();
	/**
	 * Settles Weapon's heat decay and returns the cone half-angle a shot taken right now should use.
	 *
	 * Called on both the firing client and the authority, and deliberately reads the *same* inputs on each,
	 * so the two agree on the cone without anything being sent. Both are reading replicated stance, so at
	 * high ping they can briefly disagree about a stance that changed mid-flight; the authority's answer is
	 * the one that scores, and the divergence is bounded by one multiplier. That is why the cone is
	 * recomputed here rather than trusted from the client - see Server_FireWeapon.
	 */
	float PrepareWeaponSpread(AWeapon* Weapon, APawn* OwningPawn) const;
	
	UFUNCTION()
	void OnRep_CurrentWeapon(AWeapon* LastWeapon);
	
	void SetCurrentWeapon(AWeapon* NewWeapon, AWeapon* LastWeapon);
	
	UPROPERTY(Transient, Replicated)
	TArray<AWeapon*> Inventory;
	
	UPROPERTY(EditDefaultsOnly, Category = "FPS|Weapon")
	TArray<TSubclassOf<AWeapon>> DefaultWeaponClass;
	
	AWeapon* SpawnWeapon(TSubclassOf<AWeapon> WeaponClass) const;
	
	UFUNCTION(Server, Reliable)
	void Server_Aim(bool bPressed);
	
	/**
	 * The server derives the hit from its own view; no client-authored FHitResult crosses the network.
	 *
	 * SpreadSeed only chooses *where in the cone* the round lands - the cone's size is recomputed here from
	 * the authority's own heat, never taken from the client. That split is what makes it safe to let the
	 * client influence spread at all: a forged seed can rotate a round around the cone, which buys nothing,
	 * but it cannot shrink the cone, which would be an aimbot.
	 */
	UFUNCTION(Server, Reliable)
	void Server_FireWeapon(AWeapon* FiredWeapon, uint16 SpreadSeed);
	
	/** Transient cosmetics must never block reliable gameplay traffic. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_FireWeapon(AWeapon* FiredWeapon, FVector_NetQuantize ImpactPoint,
		FVector_NetQuantizeNormal ImpactNormal, uint8 ImpactSurfaceType, int32 AuthAmmo);

	/** Rare correction used when the server rejects an over-rate or stale fire request. */
	UFUNCTION(Client, Reliable)
	void Client_CorrectFire(AWeapon* FiredWeapon, int32 AuthAmmo);
	
	void Local_Aim(bool bPressed);
	void Local_FireWeapon();
	
	int32 Local_WeaponIndex;
	int32 AdvanceWeaponIndex();
	
	void Local_CycleWeapon(int32 WeaponIndex);
	
	UFUNCTION(Server, Reliable)
	void ServerCycleWeapon(int32 WeaponIndex);
	
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_CycleWeapon(int32 WeaponIndex);
	
	UFUNCTION()
	void OnRep_CurrentReserveAmmo();
	
	void Local_ReloadWeapon();

	UFUNCTION(Server, Reliable)
	void Server_ReloadWeapon();

	UFUNCTION(Client, Reliable)
	void Client_ReloadWeapon(int32 NewWeaponAmmo, int32 NewCarriedAmmo);

	/**
	 * Tells the shooter its round landed. Deliberately Unreliable: the hit marker is purely cosmetic,
	 * and at auto-fire rates a reliable per-shot RPC saturates the reliable buffer and can drop clients.
	 * A missed marker costs one frame of feedback; a full buffer costs the connection.
	 */
	UFUNCTION(Client, Unreliable)
	void Client_ConfirmHit(bool bLethal, bool bHeadshot, float DamageDealt);

	// --- Hit confirmation audio ----------------------------------------------------------------------

	/**
	 * Plays the confirm tone 2D for the instigating player only. Called from Client_ConfirmHit, so it
	 * inherits that path's guarantee that the hit was scored by the authority - the shooter never hears a
	 * click for a round their own optimistic trace thought landed.
	 */
	void Local_PlayHitConfirmSound(bool bLethal, bool bHeadshot) const;

	/** Resolves the per-weapon override first, then the component default, then the plain hit sound. */
	USoundBase* SelectHitConfirmSound(bool bLethal, bool bHeadshot) const;

	/** The global confirm tone. Required - the other two only exist to specialise it. */
	UPROPERTY(EditDefaultsOnly, Category = "FPS|HitMarker")
	TObjectPtr<USoundBase> HitConfirmSound;

	/** Optional. Unset means a headshot sounds like any other hit; the marker still turns gold. */
	UPROPERTY(EditDefaultsOnly, Category = "FPS|HitMarker")
	TObjectPtr<USoundBase> HitConfirmSound_Headshot;

	/** Optional. Unset means a kill sounds like any other hit; the marker still turns red. */
	UPROPERTY(EditDefaultsOnly, Category = "FPS|HitMarker")
	TObjectPtr<USoundBase> HitConfirmSound_Lethal;

	UPROPERTY(EditDefaultsOnly, Category = "FPS|HitMarker", meta = (ClampMin = "0.0"))
	float HitConfirmVolume;

	UPROPERTY(EditDefaultsOnly, Category = "FPS|HitMarker", meta = (ClampMin = "0.0"))
	float HitConfirmPitch;

	/**
	 * Optional voice cap for the confirm tone. Worth assigning one: an automatic landing every round starts
	 * a new 2D voice per hit, and a dozen overlapping copies of the same short transient sums into a
	 * clipped buzz rather than reading as twelve hits. A concurrency of 2-3 with Stop Oldest keeps it crisp.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "FPS|HitMarker")
	TObjectPtr<USoundConcurrency> HitConfirmConcurrency;

	/** Latch so the "no confirm sound assigned" warning is logged once, not once per landed round. */
	mutable bool bHasWarnedMissingHitConfirmSound;

	/**
	 * Authority-only. True when Hit names one of the target's headshot bones AND the claim survives
	 * Auth_ValidateHeadshot. Never call this on a client - the result is what scales damage.
	 */
	bool Auth_IsHeadshot(const FHitResult& Hit) const;

	/**
	 * Cheap sanity gate on a client-supplied Hit.BoneName. Server_FireWeapon trusts the client's
	 * FHitResult wholesale (there is no server-side rewind yet), so a damage multiplier keyed on
	 * BoneName is directly forgeable. This confirms the named bone actually exists on the target's mesh
	 * and that Hit.ImpactPoint is within HeadshotValidationTolerance of that bone's current
	 * server-side position. On failure only the multiplier is dropped, never the shot - honest clients
	 * fail this from ordinary latency and a swallowed hit feels far worse than a swallowed headshot.
	 */
	bool Auth_ValidateHeadshot(const FHitResult& Hit, FName BoneName) const;

	/**
	 * Distance Hit.ImpactPoint may sit from the claimed bone's server-side world position and still count.
	 * Defaults deliberately loose: the server has no rewind, so at high ping the target has genuinely
	 * moved between the client's trace and this check. Tighten only if forged headshots become a problem.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "FPS|Damage")
	float HeadshotValidationTolerance;

	/**
	 * Turn off if headshots stop registering on a dedicated server: the 3P mesh may not tick its pose
	 * there, leaving bone transforms stale, which makes every position check fail. Turning it off means
	 * the server takes the client's claimed bone on trust.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "FPS|Damage")
	bool bValidateHeadshotBonePosition;

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_ReloadWeapon();

	/** Moves rounds from reserve into the mag on the authority and confirms the result to the owner. */
	void Auth_ReloadWeapon();

	/** Sent by the owning client when its reload animation reaches the point the mag goes in. */
	UFUNCTION(Server, Reliable)
	void Server_CompleteReload();

	// --- Recoil ------------------------------------------------------------------------------------

	/**
	 * View punch and screen shake for one round - the two recoil channels that only exist on the shooter's
	 * own machine. Called from Local_FireWeapon only. The visible weapon kick is deliberately NOT here: it
	 * has to run for observers too, so it is applied next to each Local_Fire instead.
	 */
	void ApplyLocalFireRecoil(AWeapon* FiredWeapon);

	/** Sprint state of the owning pawn, asked through IPlayerInterface. */
	bool IsOwnerSprinting() const;

	/**
	 * True when this pawn should use first-person assets - its own arms, Mesh1P montages, the 1P weapon mesh.
	 *
	 * Deliberately NOT IsLocallyControlled(). An AI-controlled pawn is locally controlled on the authority,
	 * so every 1P/3P selection in this component would pick the arms nobody can see. Sites that ask "does
	 * this machine drive the logic" (reload completion, prediction reconcile, the multicast de-dupe) still
	 * ask IsLocallyControlled() and must keep doing so - the two questions only coincide for human players.
	 */
	bool IsOwnerFirstPerson() const;

	/** Ends the owning pawn's sprint. Never affects sliding, jumping or any other movement state. */
	void CancelOwnerSprint();
	
	
};
