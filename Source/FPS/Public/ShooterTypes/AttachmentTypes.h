#pragma once

#include "CoreMinimal.h"
#include "AttachmentTypes.generated.h"

class UAttachmentData;

/**
 * Hardpoints on a weapon. One attachment per slot, so two attachments sharing a slot are a real choice
 * rather than a shopping list - Extra Mags and Fast Mags both sit in Magazine on purpose, which is what
 * turns "larger magazine vs faster reload" from a table in the GDD into something the player actually picks.
 *
 * A weapon declares the slots it has hardware for in AWeapon::SupportedSlots, so a compact sidearm can
 * simply have no Underbarrel and needs no special-case code anywhere to say so.
 */
UENUM(BlueprintType)
enum class EAttachmentSlot : uint8
{
	None UMETA(DisplayName = "None"),

	/** Sights and scopes. */
	Optic UMETA(DisplayName = "Optic"),

	/**
	 * Muzzle-rail hardware, which is where the laser sight lives. Deliberately not the same slot as the
	 * foregrip: those two are the hip-fire and the recoil answer respectively, and forcing a choice between
	 * them would collapse two different playstyles into one decision.
	 */
	Muzzle UMETA(DisplayName = "Muzzle / Laser"),

	/** Foregrips and bipods - the recoil slot. */
	Underbarrel UMETA(DisplayName = "Underbarrel"),

	/** Rear grips and stocks - the handling slot. */
	Grip UMETA(DisplayName = "Grip"),

	/** Magazine bodies and speed plates. */
	Magazine UMETA(DisplayName = "Magazine")
};

/**
 * Attachment rarity. Names are provisional - the GDD leaves the tier/rarity curve TBD - but the *ordering*
 * is load-bearing and is what the code uses: the enum value is cast to an integer and the distance above the
 * definition's own BaseRarity becomes the number of "rarity steps" a modifier is scaled by. Insert new
 * entries in ascending order only, never in the middle.
 */
UENUM(BlueprintType)
enum class EAttachmentRarity : uint8
{
	Common UMETA(DisplayName = "Common"),
	Uncommon UMETA(DisplayName = "Uncommon"),
	Rare UMETA(DisplayName = "Rare"),
	Epic UMETA(DisplayName = "Epic"),
	Legendary UMETA(DisplayName = "Legendary")
};

/**
 * Every weapon stat an attachment is allowed to touch. This list is the contract: if a stat is not here,
 * no attachment can reach it, which is how the movement pillar is enforced structurally rather than by
 * review. There is deliberately no locomotion stat and deliberately no fire rate or damage stat - those
 * are raw power with no cost side, and equipment here expresses playstyle instead.
 */
UENUM(BlueprintType)
enum class EWeaponStat : uint8
{
	None UMETA(DisplayName = "None"),

	/** Spread multiplier applied only while NOT aiming. Below 1 tightens the hip. Laser sight. */
	HipFireSpread UMETA(DisplayName = "Hip-Fire Spread"),

	/** Spread multiplier applied only while aiming, on top of FRecoilParams::AimSpreadMultiplier. Optics. */
	AimSpread UMETA(DisplayName = "Aim Spread"),

	/** Multiplier on the weapon's ADS field of view. Below 1 zooms further in. Optics. */
	AimFieldOfView UMETA(DisplayName = "Aim Field Of View"),

	/** Multiplier on aimed look sensitivity. The cost side of a high-zoom optic. */
	AimLookSensitivity UMETA(DisplayName = "Aim Look Sensitivity"),

	/** Multiplier on view punch - the channel that actually costs accuracy. Foregrip. */
	ViewPunch UMETA(DisplayName = "View Punch"),

	/** Multiplier on the cosmetic weapon-mesh kick. Usually moved alongside ViewPunch so they agree. */
	WeaponKick UMETA(DisplayName = "Weapon Kick"),

	/** Multiplier on heat gained per round, so it scales spread, punch and kick together over a burst. */
	RecoilHeat UMETA(DisplayName = "Recoil Heat"),

	/** Multiplier on reload animation play rate. Above 1 is faster. Fast Mags. */
	ReloadSpeed UMETA(DisplayName = "Reload Speed"),

	/** Multiplier on equip/swap animation play rate. Above 1 is faster. Quick-Draw Grip. */
	EquipSpeed UMETA(DisplayName = "Equip Speed"),

	/** Rounds added to the magazine. Always additive - see FWeaponEffectiveStats::ApplyModifier. Extra Mags. */
	MagCapacity UMETA(DisplayName = "Magazine Capacity")
};

/** How a modifier folds into the running total for its stat. */
UENUM(BlueprintType)
enum class EWeaponStatModifierOp : uint8
{
	/** Accumulator *= Value. The default for every multiplier stat, so two attachments compound. */
	Multiply UMETA(DisplayName = "Multiply"),

	/** Accumulator += Value. The only sensible op for MagCapacity, and available on the rest. */
	Add UMETA(DisplayName = "Add")
};

/**
 * One authored stat change. An attachment is nothing but a slot plus a list of these, which is the whole
 * point of the system: adding a new attachment must never require new C++.
 */
USTRUCT(BlueprintType)
struct FWeaponStatModifier
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	EWeaponStat Stat = EWeaponStat::None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	EWeaponStatModifierOp Op = EWeaponStatModifierOp::Multiply;

	/** The value at the definition's own BaseRarity. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	float Value = 1.f;

	/**
	 * Added to Value once per rarity step the *instance* sits above the definition's BaseRarity, which is
	 * what the GDD's per-round reroll and the post-match steal will drive later.
	 *
	 * Note the sign: this is added to the value as authored, not applied as an improvement, because the code
	 * has no idea which direction is "better" for a given stat. A stat where lower is better - hip-fire
	 * spread, view punch - therefore wants a NEGATIVE step here. Getting this backwards produces an
	 * attachment that gets worse as it rolls up, which is the one mistake worth checking for by hand.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	float ValuePerRarityStep = 0.f;

	float GetValueForRaritySteps(int32 RaritySteps) const
	{
		return Value + ValuePerRarityStep * static_cast<float>(FMath::Max(RaritySteps, 0));
	}
};

/**
 * The aggregated result of a weapon's base stats plus every equipped attachment. Recomputed from scratch
 * whenever the attachment list changes rather than being adjusted incrementally, so equipping and removing
 * in any order can never leave a residue behind.
 *
 * Deliberately NOT replicated. Each machine derives this from the replicated Attachments array using the
 * same code and the same data assets, exactly as FRecoilParams is derived from WeaponType at BeginPlay - so
 * the firing client and the authority agree on spread and mag size without a byte crossing the network.
 */
USTRUCT(BlueprintType)
struct FWeaponEffectiveStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	float HipFireSpreadMultiplier = 1.f;

	UPROPERTY(BlueprintReadOnly)
	float AimSpreadMultiplier = 1.f;

	UPROPERTY(BlueprintReadOnly)
	float AimFieldOfViewMultiplier = 1.f;

	UPROPERTY(BlueprintReadOnly)
	float AimLookSensitivityMultiplier = 1.f;

	UPROPERTY(BlueprintReadOnly)
	float ViewPunchMultiplier = 1.f;

	UPROPERTY(BlueprintReadOnly)
	float WeaponKickMultiplier = 1.f;

	UPROPERTY(BlueprintReadOnly)
	float RecoilHeatMultiplier = 1.f;

	UPROPERTY(BlueprintReadOnly)
	float ReloadSpeedMultiplier = 1.f;

	UPROPERTY(BlueprintReadOnly)
	float EquipSpeedMultiplier = 1.f;

	/** Rounds, not a multiplier. Signed so an attachment can trade capacity away for something else. */
	UPROPERTY(BlueprintReadOnly)
	int32 MagCapacityBonus = 0;

	/** Back to "no attachments equipped". Called at the top of every recalculation. */
	void Reset() { *this = FWeaponEffectiveStats(); }

	/** Folds one modifier in at the given number of rarity steps above its definition's BaseRarity. */
	void ApplyModifier(const FWeaponStatModifier& Modifier, int32 RaritySteps);
};

/**
 * One attachment as it exists on a weapon right now. This is the replicated unit and the thing that will
 * later be saved, stolen and rerolled, so it carries an instance Rarity that is free to differ from
 * Definition->BaseRarity - a Common laser rolled up to Rare for a round is the same definition with a
 * higher instance rarity, not a second asset.
 */
USTRUCT(BlueprintType)
struct FEquippedAttachment
{
	GENERATED_BODY()

	/**
	 * Cached from Definition->Slot at equip time rather than read through the pointer. The slot is what
	 * every lookup keys on, and it has to stay answerable even on a client whose reference to the
	 * definition asset has not resolved yet - see AWeapon::OnRep_Attachments.
	 */
	UPROPERTY(BlueprintReadOnly)
	EAttachmentSlot Slot = EAttachmentSlot::None;

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UAttachmentData> Definition = nullptr;

	/** The instance's rarity. At or above Definition->BaseRarity; the difference scales every modifier. */
	UPROPERTY(BlueprintReadOnly)
	EAttachmentRarity Rarity = EAttachmentRarity::Common;

	bool IsValidAttachment() const { return Slot != EAttachmentSlot::None && Definition != nullptr; }
};
