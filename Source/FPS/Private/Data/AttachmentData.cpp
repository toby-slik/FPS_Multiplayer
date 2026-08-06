// Fill out your copyright notice in the Description page of Project Settings.


#include "Data/AttachmentData.h"

bool UAttachmentData::IsCompatibleWithWeaponType(const FGameplayTag& WeaponType) const
{
	if (SupportedWeaponTypes.IsEmpty()) return true;

	// MatchesAny rather than the container's own HasTag, and the direction matters: this walks the *weapon's*
	// tag up to its parents, so an entry of WeaponType.Rifle also accepts a future WeaponType.Rifle.Marksman.
	// SupportedWeaponTypes.HasTag(WeaponType) matches the other way round and would reject it.
	return WeaponType.MatchesAny(SupportedWeaponTypes);
}
