

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstance.h"
#include "ShooterTypes/AttachmentTypes.h"
#include "ShooterTypes/ShooterTypes.h"
#include "Weapon.generated.h"

class UAttachmentData;
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
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	USkeletalMeshComponent* GetMesh1P() const;
	USkeletalMeshComponent* GetMesh3P() const;
	UMaterialInstanceDynamic* GetReticleDynamicMaterialInstance();
	UMaterialInstanceDynamic* GetAmmoCounterDynamicMaterialInstance();
	
	void AttachToOwningPawn(APawn* Pawn) const;
	void DetachFromOwningPawn();

	/**
	 * SpreadDegrees is the cone half-angle to scatter this shot within, and SpreadSeed picks where in that
	 * cone it lands. The seed is passed rather than drawn locally because the server runs this trace again
	 * for damage while the client's own run drives the tracer and impact FX - given the same seed and the
	 * same angle both machines land on the same direction, so what the shooter sees is what the server
	 * scores. Pass 0 spread for a perfectly centred shot.
	 */
	void WeaponTrace(FHitResult& OutHit, float TraceLength, float SpreadDegrees, uint16 SpreadSeed);
	
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

	/**
	 * When true, RecoilParams is replaced at BeginPlay by the preset for this weapon's WeaponType, so each
	 * gun kicks differently with nothing authored per Blueprint. Untick to hand-tune a weapon: the values
	 * already in RecoilParams are then used exactly as they stand.
	 *
	 * The presets live in C++ rather than in DA_WeaponData deliberately. Recoil is the cost side of a
	 * weapon's trade-off, so it belongs next to Damage, FireTime and MagCapacity - which are all on the
	 * weapon - rather than split into a second asset that could disagree with them.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category= "FPS|Recoil")
	bool bUseRecoilTypePreset;

	/**
	 * Greyed out in the Details panel while bUseRecoilTypePreset is ticked, because in that state BeginPlay
	 * replaces every value here and editing them does nothing. The EditCondition is the whole point: without
	 * it this silently discards hand-authored values at runtime, which reads as "recoil tuning is broken"
	 * rather than as "this weapon is on its preset".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "FPS|Recoil",
		meta = (EditCondition = "!bUseRecoilTypePreset"))
	FRecoilParams RecoilParams;

	/** The authored preset for a weapon type. Falls back to a mid-weight automatic for unknown tags. */
	static FRecoilParams GetRecoilPresetForWeaponType(const FGameplayTag& Type);

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

	// ---------------------------------------------------------------------------------------------
	// Recoil. Heat lives on the weapon rather than on UCombatComponent so each gun cools on its own
	// clock - swapping to a sidearm mid-burst must not hand it the rifle's accumulated spread, and the
	// rifle must still be hot if you swap straight back.
	// ---------------------------------------------------------------------------------------------

	/**
	 * Advances heat decay up to CurrentTime and returns the resulting 0-1 heat. Idempotent within a frame
	 * and safe to call at any rate, because it integrates from the last time it was called rather than
	 * assuming a fixed step - which is what lets the client call it every tick for its crosshair while the
	 * server calls it only on the frames it actually resolves a shot.
	 */
	float AdvanceAndGetHeat(float CurrentTime);

	/** Reads heat without advancing decay. For display and for anything that must not mutate state. */
	float GetHeat() const { return SpreadHeat; }

	/** Registers one round's worth of heat. Called on both the firing client and the authority. */
	void AddRecoilHeat(float CurrentTime);

	/** Clears heat and snaps the visible kick back to rest. Used on reload, equip and death. */
	void ResetRecoilState();

	/**
	 * Final cone half-angle in degrees for a shot taken right now. Pure function of current heat and the
	 * passed stance, so the firing client and the authority agree given the same stance.
	 */
	float GetSpreadDegrees(bool bIsAiming, bool bIsMoving, bool bIsAirborne) const;

	/** Upward view punch in degrees for a shot taken at the current heat. Horizontal is drawn by the caller. */
	float GetViewPunchPitch(bool bIsAiming) const;

	/**
	 * Half-width of the horizontal view punch in degrees for a shot taken right now, already scaled by aim,
	 * by heat and by attachments. The caller still draws the sign - a fixed direction would be a pattern to
	 * memorise. Lives here rather than in UCombatComponent so both punch channels read the same effective
	 * stats; splitting them let a foregrip tame the pitch while leaving the yaw untouched.
	 */
	float GetViewPunchYawRange(bool bIsAiming) const;

	/**
	 * Kicks the visible weapon meshes. Cosmetic only - it writes the meshes' relative transforms and never
	 * touches the aim point, the control rotation or the trace, so it is safe to run unpredicted on
	 * simulated proxies and needs no server agreement.
	 */
	void ApplyWeaponKick(bool bIsAiming);

	/** Interps the visible kick back toward rest. Driven from UCombatComponent's tick on every machine. */
	void UpdateWeaponKick(float DeltaTime);

	UPROPERTY(EditAnywhere, Category="FPS|Ammo")
	int32 MagCapacity;
	
	UPROPERTY(EditAnywhere, Category="FPS|Ammo")
	int32 Ammo;
	
	UPROPERTY(EditAnywhere, Category="FPS|Ammo")
	int32 StartingCarriedAmmo;

	EWeaponStatus WeaponStatus;

	// ---------------------------------------------------------------------------------------------
	// Attachments. Only *what is equipped* is replicated; the resulting stats are derived independently
	// on every machine, the same way FRecoilParams is derived from WeaponType at BeginPlay. That is what
	// keeps the firing client's predicted spread, mag size and reload timing identical to the authority's
	// without sending any of them.
	// ---------------------------------------------------------------------------------------------

	/**
	 * Hardpoints this weapon physically has. An attachment whose slot is not listed here can never be
	 * equipped, which is how "this sidearm has no underbarrel" is expressed - in data, per weapon, with no
	 * class logic. Order is irrelevant; duplicates are harmless.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Attachments")
	TArray<EAttachmentSlot> SupportedSlots;

	/**
	 * Equipped on the authority at BeginPlay, at each definition's own BaseRarity. Two purposes: it is how a
	 * tier's baseline loadout is authored before there is a loadout system, and the hard references keep those
	 * definition assets loaded on every machine, which is what makes the replicated pointer in
	 * FEquippedAttachment resolve immediately rather than arriving null - see OnRep_Attachments.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "FPS|Attachments")
	TArray<TObjectPtr<UAttachmentData>> DefaultAttachments;

	/** Server-authoritative. Never write this directly outside the Auth_ functions below. */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Attachments, Category = "FPS|Attachments")
	TArray<FEquippedAttachment> Attachments;

	UFUNCTION(BlueprintPure, Category = "FPS|Attachments")
	bool SupportsSlot(EAttachmentSlot Slot) const;

	/** Slot is on this weapon, the weapon type is accepted, and the definition names a real slot. */
	UFUNCTION(BlueprintPure, Category = "FPS|Attachments")
	bool CanEquipAttachment(const UAttachmentData* Definition) const;

	/** Returns a default-constructed entry when the slot is empty - check IsValidAttachment() on the result. */
	UFUNCTION(BlueprintPure, Category = "FPS|Attachments")
	FEquippedAttachment GetAttachmentInSlot(EAttachmentSlot Slot) const;

	/**
	 * Authority only. Replaces whatever occupies the definition's slot - one attachment per slot is the rule,
	 * so this is a set rather than an add. InstanceRarity is the *instance's* rarity and is clamped up to the
	 * definition's BaseRarity, which is the hook the GDD's per-round reroll and post-match steal will use.
	 * Returns false and changes nothing if the weapon cannot take this attachment.
	 */
	bool Auth_SetAttachment(UAttachmentData* Definition, EAttachmentRarity InstanceRarity);

	/** Authority only. No-op when the slot is already empty. Returns true if something was removed. */
	bool Auth_ClearAttachmentSlot(EAttachmentSlot Slot);

	const FWeaponEffectiveStats& GetEffectiveStats() const { return EffectiveStats; }

	/**
	 * Magazine size with attachments applied. Every ammo clamp, the reload refill and the HUD's rounds-max
	 * must go through this rather than reading MagCapacity, or an Extra Mags weapon reports and clamps to the
	 * wrong number - and on the owning client that shows up as a mag that refuses to fill.
	 */
	UFUNCTION(BlueprintPure, Category = "FPS|Ammo")
	int32 GetEffectiveMagCapacity() const;

	/** ADS field of view with optics applied. Read this from Blueprint instead of AimFieldOfView. */
	UFUNCTION(BlueprintPure, Category = "FPS|Aiming")
	float GetEffectiveAimFieldOfView() const;

	/** Aimed look sensitivity multiplier with attachments applied. */
	UFUNCTION(BlueprintPure, Category = "FPS|Aiming")
	float GetEffectiveAimLookSensitivityScale() const;

	/**
	 * Play rate for the reload montages. Reload completion is driven by an anim notify on the owning client,
	 * so the play rate IS the reload duration - there is no separate timer to keep in step with it.
	 */
	UFUNCTION(BlueprintPure, Category = "FPS|Attachments")
	float GetReloadPlayRate() const;

	/** Play rate for the equip/swap montages. Same story as the reload: the notify moves with the rate. */
	UFUNCTION(BlueprintPure, Category = "FPS|Attachments")
	float GetEquipPlayRate() const;

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

	/**
	 * ART PASS HOOK - deliberately a no-op in C++.
	 *
	 * Called on every machine whenever the Attachments array changes, and once at BeginPlay. Rebuild rather
	 * than diff: destroy whatever was spawned last time, then walk Attachments and for each entry spawn
	 * Definition->VisualMesh onto Mesh1P at Definition->AttachSocket1P and onto Mesh3P at AttachSocket3P,
	 * applying Definition->VisualRelativeTransform. Entries with no VisualMesh are the ones with no visual
	 * (Fast Mags, Extra Mags) and should be skipped.
	 *
	 * Two traps for when this gets filled in. Anything parented to Mesh1P inherits the recoil kick for free
	 * because ApplyKickToMeshes writes the mesh transforms, so attached meshes must NOT be kicked again
	 * themselves. And visibility is owned by SetMeshVisibilities on the parent meshes, so a spawned component
	 * should be left visible and allowed to inherit it rather than being hidden by hand.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "FPS|Attachments")
	void RefreshAttachmentVisuals();

	// Weapon Mesh: 1st person view
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FPS|Weapon")
	TObjectPtr<USkeletalMeshComponent> Mesh1P;
	
	// Weapon Mesh: 3rd person view
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FPS|Weapon")
	TObjectPtr<USkeletalMeshComponent> Mesh3P;
	
private:

	void SetMeshVisibilities(APawn* OwningPawn) const;

	/** Caches the authored rest transforms of both meshes. Must run before any kick is applied. */
	void CacheKickRestTransforms();

	/** Writes the current kick offset onto both meshes, relative to their cached rest transforms. */
	void ApplyKickToMeshes() const;

	int32 Sequence;

	UFUNCTION()
	void OnRep_Attachments();

	/**
	 * Rebuilds EffectiveStats from scratch out of the base stats plus every equipped attachment. Always a
	 * full rebuild rather than an incremental adjust, so removing an attachment can never leave part of its
	 * modifier behind.
	 */
	void RecalculateEffectiveStats();

	/**
	 * Trims a mag that is now over capacity because an Extra Mags attachment came off. Run on every machine
	 * from the same replicated data, so it needs no correction RPC. Not called from BeginPlay: an authored
	 * Ammo above MagCapacity is an existing content quirk and silently rewriting it here would hide it.
	 */
	void ClampAmmoToEffectiveCapacity();

	/** Not replicated and not a UPROPERTY - derived on every machine from the replicated Attachments array. */
	FWeaponEffectiveStats EffectiveStats;

	// --- Recoil runtime state. Never replicated: the authority keeps its own copy for spread, and the
	// visible kick is regenerated locally from each Local_Fire, so there is nothing worth sending. ---

	/** Shared 0-1 "fired recently" scalar. Drives spread, view punch and kick magnitude together. */
	float SpreadHeat;

	/** World time of the most recent round, for the decay delay. */
	float LastHeatShotTime;

	/** World time decay was last integrated up to. Kept separate from LastHeatShotTime so an arbitrary
	 *  call rate integrates exactly once over each interval instead of double-counting or skipping. */
	float LastHeatDecayTime;

	/** Current cosmetic offset from rest, in the meshes' parent space. */
	FVector KickLocationOffset;
	FRotator KickRotationOffset;

	FTransform Mesh1PRestTransform;
	FTransform Mesh3PRestTransform;
	bool bKickRestTransformsCached;
	
	UPROPERTY(EditDefaultsOnly, Category= "FPS|Weapon")
	TObjectPtr<UMaterialInterface> ReticleMaterial;
	
	UPROPERTY(EditDefaultsOnly, Category= "FPS|Weapon")
	TObjectPtr<UMaterialInterface> AmmoCounterMaterial;
	
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> DynMatInst_Reticle;
	
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> DynMatInst_AmmoCounter;
};
