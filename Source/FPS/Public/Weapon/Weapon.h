

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstance.h"
#include "ShooterTypes/ShooterTypes.h"
#include "Weapon.generated.h"

class UMaterialInstanceDynamic;
enum EPhysicalSurface : int;

UENUM(BlueprintType)
enum class EFireType : uint8
{
	Auto UMETA(DisplayName = "Automatic"),
	SemiAuto UMETA(DisplayName = "SemiAutomatic")
};

UENUM(BlueprintType)
enum class EWeaponStatus : uint8
{
	Idle, // weapon doing nothing can fire/ roload / cycle
	Firing, 
	Reloading,
	Cycling,
	Unequipped,
};

UCLASS()
class FPS_API AWeapon : public AActor
{
	GENERATED_BODY()

public:
	AWeapon();
	
	USkeletalMeshComponent* GetMesh1P() const;
	USkeletalMeshComponent* GetMesh3P() const;
	UMaterialInstanceDynamic* GetReticleDynamicMaterialInstance();
	UMaterialInstanceDynamic* GetAmmoCounterDynamicMaterialInstance();
	
	void AttachToOwningPawn(APawn* Pawn) const;
	void DetachFromOwningPawn();
	void WeaponTrace(FHitResult& OutHit, float TraceLength);
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FPS|WeaponType")
	FGameplayTag WeaponType;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Aiming")
	float AimFieldOfView;

	/**
	 * Look sensitivity multiplier applied while aiming this weapon. 1.0 keeps whatever the FOV Scaling
	 * modifier on IA_Look already gives (aiming narrows the FOV, so turning is roughly
	 * AimFieldOfView / DefaultFOV as slow); below 1.0 slows aimed turning further.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FPS|Aiming")
	float AimLookSensitivityScale;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Trace")
	float TraceRadius;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FPS|FireType")
	EFireType FireType;
	 
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FPS|FireType")
	float FireTime;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FPS|Damage")
	float Damage;

	/**
	 * Damage is multiplied by this when the round lands on one of the target's headshot bones.
	 * Per weapon on purpose: a high multiplier is meant to be paid for elsewhere (smaller mag, worse
	 * hip-fire, slower swap) rather than being a free global rule. 1.0 disables headshots for a weapon.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FPS|Damage")
	float HeadshotDamageMultiplier;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "FPS|Reticle")
	FReticleParams ReticleParams;

	/**
	 * Read live by UShooterReticle every frame, so a Blueprint writing to this at runtime changes the
	 * marker immediately. Not snapshotted anywhere on the widget - see UShooterReticle::GetHitMarkerParams.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FPS|HitMarker")
	FHitMarkerParams HitMarkerParams;

	UPROPERTY(EditDefaultsOnly, Category= "FPS|Icon")
	TObjectPtr<UMaterialInterface> WeaponIcon;
	
	void Local_Fire(const FVector& ImpactPoint, const FVector& ImpactNormal, TEnumAsByte<EPhysicalSurface> ImpactSurfaceType, bool bIsFirstPerson);

	/**
	 * Trigger pulled on an empty magazine. Purely cosmetic and deliberately never replicated, so it is
	 * guarded to the locally controlled instigator - a simulated proxy or a dedicated server must stay silent.
	 */
	void Local_DryFire(bool bReloadStarted);

	void Auth_Fire();
	void Rep_Fire(int32 AuthAmmo);

	/**
	 * Drops every outstanding client-side fire prediction. Must be called whenever Ammo is set from an
	 * authoritative value instead of being reconciled shot by shot (reload, equip), otherwise Sequence
	 * keeps offsetting Rep_Fire and the local mag reads short of the server's for the rest of the match.
	 */
	void ResetPredictionSequence() { Sequence = 0; }
	
	UPROPERTY(EditAnywhere, Category="FPS|Ammo")
	int32 MagCapacity;
	
	UPROPERTY(EditAnywhere, Category="FPS|Ammo")
	int32 Ammo;
	
	UPROPERTY(EditAnywhere, Category="FPS|Ammo")
	int32 StartingCarriedAmmo;
	
	EWeaponStatus WeaponStatus;
	
protected:
	virtual void BeginPlay() override;

	UFUNCTION(BlueprintImplementableEvent)
	void FireEffects(const FVector& ImpactPoint, const FVector& ImpactNormal, EPhysicalSurface ImpactSurfaceType, bool bIsFirstPerson);

	/**
	 * Authored in the weapon Blueprint alongside FireEffects, using the existing ATT_* attenuation and
	 * SCON_Guns_* concurrency assets. bReloadStarted is true when the same press also began a reload
	 * (the mag was empty but the reserve was not), so the click can be softened or skipped rather than
	 * fighting the reload foley. False means the reserve is empty too and this event is the only
	 * feedback the player gets for that press.
	 */
	UFUNCTION(BlueprintImplementableEvent)
	void DryFireEffects(bool bReloadStarted);

	// Weapon Mesh: 1st person view
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FPS|Weapon")
	TObjectPtr<USkeletalMeshComponent> Mesh1P;
	
	// Weapon Mesh: 3rd person view
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FPS|Weapon")
	TObjectPtr<USkeletalMeshComponent> Mesh3P;
	
private:
	
	void SetMeshVisibilities(APawn* OwningPawn) const;
	
	int32 Sequence;
	
	UPROPERTY(EditDefaultsOnly, Category= "FPS|Weapon")
	TObjectPtr<UMaterialInterface> ReticleMaterial;
	
	UPROPERTY(EditDefaultsOnly, Category= "FPS|Weapon")
	TObjectPtr<UMaterialInterface> AmmoCounterMaterial;
	
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> DynMatInst_Reticle;
	
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> DynMatInst_AmmoCounter;
};
