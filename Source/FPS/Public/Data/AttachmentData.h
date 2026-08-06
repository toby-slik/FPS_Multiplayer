// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/DataAsset.h"
#include "ShooterTypes/AttachmentTypes.h"
#include "AttachmentData.generated.h"

class UStaticMesh;
class UMaterialInterface;

/**
 * One attachment, authored entirely as data. A UDataAsset rather than a data-table row to match how
 * DA_WeaponData already works, and because each attachment is referenced individually by the weapons and
 * (later) the loadout that can hold it - a per-asset reference is what keeps those references cookable and
 * keeps the asset resident on every machine, which the replication of FEquippedAttachment relies on.
 *
 * Adding a new attachment is: create one of these, pick a Slot, add Modifiers. No C++.
 */
UCLASS()
class FPS_API UAttachmentData : public UDataAsset
{
	GENERATED_BODY()

public:

	/** The hardpoint this occupies. A weapon that does not list this slot can never hold this attachment. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Attachment")
	EAttachmentSlot Slot = EAttachmentSlot::None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Attachment")
	FText DisplayName;

	/**
	 * Stable identity, independent of the asset's path. The persistence, stealing and duplicate-resolution
	 * flows in the GDD all need to name an attachment in a save game or in a network message that will
	 * outlive any asset rename, and an asset path is not that. Nothing reads it yet - it is here so the
	 * later systems do not force a data migration.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Attachment")
	FGameplayTag AttachmentTag;

	/**
	 * The rarity this attachment is authored at. Modifiers hold their authored Value at exactly this rarity,
	 * and an instance sitting above it scales them by ValuePerRarityStep - so this is the zero point, not a
	 * gate. Never compare it against a tier's protected baseline directly; that is the instance's job.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Attachment")
	EAttachmentRarity BaseRarity = EAttachmentRarity::Common;

	/**
	 * What this attachment actually does. Author trade-offs here, not upgrades: a modifier that only helps
	 * is a power increase, and the equipment pillar says equipment expresses playstyle instead. A scope that
	 * tightens AimSpread should be paying for it with AimLookSensitivity, and so on.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Attachment")
	TArray<FWeaponStatModifier> Modifiers;

	/**
	 * Weapon-type tags this fits. Empty means it fits any weapon that has the slot, which is the intended
	 * default - the slot is normally restriction enough, and this exists for the cases where it is not
	 * (a rifle scope on a sidearm).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Attachment")
	FGameplayTagContainer SupportedWeaponTypes;

	/** Loadout/HUD icon, matching how AWeapon::WeaponIcon is used. Nothing consumes it yet. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Attachment")
	TObjectPtr<UMaterialInterface> AttachmentIcon;

	// ---------------------------------------------------------------------------------------------
	// Art pass. Nothing below is read by any gameplay code - these are the hooks for the visual pass,
	// consumed only by AWeapon::RefreshAttachmentVisuals, which is a no-op stub today.
	// ---------------------------------------------------------------------------------------------

	/** Mesh to hang off the weapon. Leave unset for an attachment with no visual (Fast Mags, Extra Mags). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Attachment|Visual")
	TObjectPtr<UStaticMesh> VisualMesh;

	/**
	 * Socket on the weapon's Mesh1P this mounts to. Named per attachment rather than per slot so two optics
	 * with different eye reliefs can sit at different points on the same rail.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Attachment|Visual")
	FName AttachSocket1P;

	/** Socket on the weapon's Mesh3P. Usually the same name as the 1P socket, but not required to be. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Attachment|Visual")
	FName AttachSocket3P;

	/** Offset applied after socket attachment, for trimming a mesh into place without re-exporting it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Attachment|Visual")
	FTransform VisualRelativeTransform = FTransform::Identity;

	/** True when this attachment is allowed on a weapon carrying the given WeaponType tag. */
	UFUNCTION(BlueprintPure, Category = "FPS|Attachment")
	bool IsCompatibleWithWeaponType(const FGameplayTag& WeaponType) const;
};
