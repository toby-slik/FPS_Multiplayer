

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "CombatComponent.generated.h"


class UMaterialInstanceDynamic;
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
	
	UPROPERTY(ReplicatedUsing = OnRep_CurrentReserveAmmo)
	int32 CurrentReserveAmmo;
	
	
protected:
	
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
	FTimerHandle FireTimer;
	void FireTimerFinished();
	
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
	
	UFUNCTION(Server, Reliable)
	void Server_FireWeapon(const FHitResult& Hit);
	
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_FireWeapon(const FHitResult& Hit, int32 AuthAmmo);
	
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

	/** Sprint state of the owning pawn, asked through IPlayerInterface. */
	bool IsOwnerSprinting() const;

	/** Ends the owning pawn's sprint. Never affects sliding, jumping or any other movement state. */
	void CancelOwnerSprint();
	
	
};
