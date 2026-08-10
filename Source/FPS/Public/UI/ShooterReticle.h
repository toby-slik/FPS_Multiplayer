// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Weapon/Weapon.h"
#include "ShooterReticle.generated.h"

class UImage;
class UMaterialInstanceDynamic;
class UCombatComponent;


UCLASS()
class FPS_API UShooterReticle : public UUserWidget
{
	GENERATED_BODY()
	
public:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Reticle;	
	
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_AmmoCounter;

	/**
	 * Optional so the widget BP still compiles before the image exists in WBP_ShooterReticle.
	 * Every use of it is null-checked.
	 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Image_HitMarker;

	/**
	 * Hit marker art. Lives on the widget rather than on AWeapon because the marker is a HUD-global
	 * element - it must survive a weapon cycle mid-burst, and there is no reason for a shotgun to
	 * own a different marker. The per-weapon *feel* is still authorable via FHitMarkerParams.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "FPS|Reticle")
	TObjectPtr<UMaterialInterface> HitMarkerMaterial;


private:

	TWeakObjectPtr<UMaterialInstanceDynamic> CurrentReticle_DynMatInst;
	TWeakObjectPtr<UMaterialInstanceDynamic> CurrentAmmoCounter_DynMatInst;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HitMarker_DynMatInst;

	FReticleParams CurrentReticleParams;

	/**
	 * Resolved once per possession in OnPossesedPawnChanged, never in NativeTick. Exists so the marker can
	 * read HitMarkerParams off the live weapon instead of a snapshot: UCombatComponent::CurrentWeapon is
	 * already the replicated source of truth for which weapon is current, so a weapon cycle needs no extra
	 * plumbing here and the widget never holds a second, drifting copy of that answer.
	 */
	TWeakObjectPtr<UCombatComponent> CurrentCombat;

	/**
	 * The current weapon's hit marker tuning, read live so a Blueprint write lands on the very next frame.
	 * Returns a default-constructed fallback when there is no pawn or no weapon yet - between death and
	 * respawn, or before the weapon first replicates. Safe to call every frame; it is two weak/IsValid
	 * checks and a pointer hop, no component lookup and no cast.
	 */
	const FHitMarkerParams& GetHitMarkerParams() const;

	/** Current weapon's recoil heat, 0-1. Returns 0 with no pawn or no weapon yet. Never mutates state. */
	float GetCurrentSpreadAlpha() const;

	float BaseCornerScaleFactor;
	float BaseShapeCutFactor;
	float _BaseCornerScaleFactor_RoundFired;
	float _BaseShapeCutFactor_RoundFired;
	float _BaseCornerScaleFactor_Aiming;
	float _BaseShapeCutFactor_Aiming;
	float _BaseCornerScaleFactor_TargetingPlayer;

	/** Sustained opening that tracks the weapon's real bullet cone. See the note at its use in NativeTick. */
	float _BaseCornerScaleFactor_Spread;

	/**
	 * Normalised 0-1 ADS blend, driven at AimingInterpSpeed so it moves in lockstep with the authored
	 * _Base*_Aiming offsets. Deliberately NOT those offsets reused: they interp toward authored values in
	 * reticle units, which are neither normalised nor guaranteed to be 0 at the hip, so they cannot drive a
	 * lerp. Everything that has a hip/ADS pair crossfades on this one alpha, so the whole reticle arrives at
	 * the ADS look on the same curve and no single term pops ahead of the others.
	 */
	float _AimAlpha;
	float _HitMarkerIntensity;
	float _HitMarkerLethal;
	float _HitMarkerHeadshot;

	/**
	 * Latched by a confirmed kill and only released once the marker has fully faded out. The lethal
	 * look has to hold for the marker's whole visible life - see the note in NativeTick.
	 */
	bool _bHitMarkerLethalLatched;

	/**
	 * Latched by a confirmed headshot, released on exactly the same condition as the lethal latch and for
	 * exactly the same reason: a headshot that only reads gold for the first few frames of a marker that
	 * stays up for most of a second reads as "the headshot marker is teal".
	 */
	bool _bHitMarkerHeadshotLatched;
	bool bAiming;
	bool bTargetingPlayer;

	void SetupHitMarker();

	/**
	 * True when the marker has to be drawn in C++ instead of by a material.
	 * The painted marker exists so the hit-marker feature is complete with zero editor asset work -
	 * no material, no image in the widget BP. The moment both Image_HitMarker and HitMarkerMaterial
	 * are authored, the material path owns the marker entirely and this returns false, because
	 * drawing both would stack two markers on top of each other.
	 */
	bool ShouldPaintHitMarker() const;

	/** Draws the four diagonal ticks into OutDrawElements at LayerId. Returns the layer it drew on. */
	int32 PaintHitMarker(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;

	
	UFUNCTION()
	void OnPossesedPawnChanged(APawn* OldPawn, APawn* NewPawn);
	
	UFUNCTION()
	void OnWeaponFirstReplicated(AWeapon* Weapon);
	
	UFUNCTION()
	void OnReticleChanged(UMaterialInstanceDynamic* ReticleDynMatInst, const FReticleParams& ReticleParams, bool bCurrentlyTargetingPlayer);
	
	UFUNCTION()
	void OnAmmoCounterChanged(UMaterialInstanceDynamic* AmmoCounterDynMatInst, int32 RoundsCurrent, int32 RoundsMax);
	
	UFUNCTION()
	void OnRoundFired(int32 RoundsCurrent, int32 RoundsMax, int32 RoundsInReserve);
	
	UFUNCTION()
	void OnAimingStatusChanged(bool bIsAiming);
	
	UFUNCTION()
	void OnTargetingPlayerStatusChanged(bool bTargeting);

	UFUNCTION()
	void OnHitConfirmed(bool bLethal, bool bHeadshot, float DamageDealt);
};
