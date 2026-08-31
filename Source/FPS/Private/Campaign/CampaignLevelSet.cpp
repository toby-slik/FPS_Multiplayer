// Copyright Druid Mechanics

#include "Campaign/CampaignLevelSet.h"

#include "Weapon/Weapon.h"

int32 UCampaignLevelSet::FindLevelIndexByPackageName(const FString& PackageName) const
{
	// Compared on the short asset name, because the name the running world reports and the name a soft
	// object path carries differ by PIE prefixes and by the /Game/Maps/X.X duplication.
	const FString ShortName = FPackageName::GetShortName(PackageName);

	for (int32 Index = 0; Index < Levels.Num(); ++Index)
	{
		const FString LevelName = FPackageName::GetShortName(Levels[Index].Level.GetLongPackageName());
		if (!LevelName.IsEmpty() && LevelName.Equals(ShortName, ESearchCase::IgnoreCase))
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

void UCampaignLevelSet::BuildPlayerLoadout(int32 LevelIndex, TArray<TSubclassOf<AWeapon>>& OutWeapons) const
{
	OutWeapons.Reset();

	const int32 Last = FMath::Min(LevelIndex, Levels.Num() - 1);
	for (int32 Index = 0; Index <= Last; ++Index)
	{
		for (const TSubclassOf<AWeapon>& WeaponClass : Levels[Index].WeaponsUnlocked)
		{
			if (IsValid(WeaponClass.Get()))
			{
				OutWeapons.AddUnique(WeaponClass);
			}
		}
	}
}
