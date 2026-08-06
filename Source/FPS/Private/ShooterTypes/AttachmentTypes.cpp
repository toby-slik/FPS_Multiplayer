#include "ShooterTypes/AttachmentTypes.h"

namespace
{
	void CombineStat(float& Accumulator, EWeaponStatModifierOp Op, float Value)
	{
		if (Op == EWeaponStatModifierOp::Multiply)
		{
			Accumulator *= Value;
			return;
		}
		Accumulator += Value;
	}
}

void FWeaponEffectiveStats::ApplyModifier(const FWeaponStatModifier& Modifier, int32 RaritySteps)
{
	const float Value = Modifier.GetValueForRaritySteps(RaritySteps);

	switch (Modifier.Stat)
	{
	case EWeaponStat::HipFireSpread:
		CombineStat(HipFireSpreadMultiplier, Modifier.Op, Value);
		break;
	case EWeaponStat::AimSpread:
		CombineStat(AimSpreadMultiplier, Modifier.Op, Value);
		break;
	case EWeaponStat::AimFieldOfView:
		CombineStat(AimFieldOfViewMultiplier, Modifier.Op, Value);
		break;
	case EWeaponStat::AimLookSensitivity:
		CombineStat(AimLookSensitivityMultiplier, Modifier.Op, Value);
		break;
	case EWeaponStat::ViewPunch:
		CombineStat(ViewPunchMultiplier, Modifier.Op, Value);
		break;
	case EWeaponStat::WeaponKick:
		CombineStat(WeaponKickMultiplier, Modifier.Op, Value);
		break;
	case EWeaponStat::RecoilHeat:
		CombineStat(RecoilHeatMultiplier, Modifier.Op, Value);
		break;
	case EWeaponStat::ReloadSpeed:
		CombineStat(ReloadSpeedMultiplier, Modifier.Op, Value);
		break;
	case EWeaponStat::EquipSpeed:
		CombineStat(EquipSpeedMultiplier, Modifier.Op, Value);
		break;
	case EWeaponStat::MagCapacity:
		// Op is ignored here on purpose. Rounds are counted, not scaled: a Multiply on a bonus that starts at
		// 0 would silently evaluate to 0 and read as "the attachment does nothing", which is far harder to
		// diagnose than simply always adding. RoundToInt rather than truncating so +2.5 does not become +2.
		MagCapacityBonus += FMath::RoundToInt(Value);
		break;
	default:
		break;
	}
}
