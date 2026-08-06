#include "Tags/ShooterGamePlayTags.h" 

namespace  ShooterTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_WeaponType_None, "Weapon.Type.None", "No Weapon Type");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_WeaponType_Rifle, "Weapon.Type.Rifle", "Rifle Weapon Type");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_WeaponType_Pistol, "Weapon.Type.Pistol", "Pistol Weapon Type");

	// Declared in the header since the lever rifle and assault rifle were added, but never defined - so any
	// C++ use of either was a link error waiting to happen. Both are also listed in
	// Config/DefaultGameplayTags.ini, where they were originally added through the editor; the tag manager
	// merges a native tag with an identical ini entry, so the existing Blueprint references still resolve to
	// the same tag and nothing needs re-picking.
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_WeaponType_LeverRifle, "Weapon.Type.LeverRifle", "Lever Rifle Weapon Type");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_WeaponType_AssultRifle, "Weapon.Type.AssultRifle", "Assault Rifle Weapon Type");
}
