// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Weapon/Weapon.h"
#include "ShooterReticle.generated.h"

class UImage; 
class UMaterialInstanceDynamic;


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
	 * own a different marker. The per-weapon *feel* is still authorable via FReticleParams.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "FPS|Reticle")
	TObjectPtr<UMaterialInterface> HitMarkerMaterial;


private:

	TWeakObjectPtr<UMaterialInstanceDynamic> CurrentReticle_DynMatInst;
	TWeakObjectPtr<UMaterialInstanceDynamic> CurrentAmmoCounter_DynMatInst;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HitMarker_DynMatInst;

	FReticleParams CurrentReticleParams;
	float BaseCornerScaleFactor;
	float BaseShapeCutFactor;
	float _BaseCornerScaleFactor_RoundFired;
	float _BaseShapeCutFactor_RoundFired;
	float _BaseCornerScaleFactor_Aiming;
	float _BaseShapeCutFactor_Aiming;
	float _BaseCornerScaleFactor_TargetingPlayer;
	float _HitMarkerIntensity;
	float _HitMarkerLethal;

	/**
	 * Latched by a confirmed kill and only released once the marker has fully faded out. The lethal
	 * look has to hold for the marker's whole visible life - see the note in NativeTick.
	 */
	bool _bHitMarkerLethalLatched;
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
