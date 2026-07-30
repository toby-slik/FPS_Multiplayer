// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/ReserveAmmo.h"

#include "Character/ShooterCharacter.h"
#include "Components/Image.h"

void UReserveAmmo::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	
	Image_WeaponIcon->SetRenderOpacity(0.f);
	Text_Ammo->SetRenderOpacity(0.f);
	
	GetOwningPlayer()->OnPossessedPawnChanged.AddDynamic(this, &ThisClass::OnPossessedPawnChanged);
	
	AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(GetOwningPlayer()->GetPawn());
	if (!IsValid(ShooterCharacter)) return;
	OnPossessedPawnChanged(nullptr, ShooterCharacter);
	
	if ( ShooterCharacter->HasWeaponFirstReplicated())
	{
		AWeapon* Weapon = IPlayerInterface::Execute_GetCurrentWeapon(ShooterCharacter);
		if (IsValid(Weapon))
		{
			OnCurrentReserveAmmoChanged(IPlayerInterface::Execute_GetReserveAmmo(ShooterCharacter), Weapon->Ammo);
		}
	}
	else
	{
		ShooterCharacter->OnWeaponFirstReplicated.AddDynamic(this, &ThisClass::OnWeaponFirstReplicated);
	}
	
}

void UReserveAmmo::OnPossessedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
}

void UReserveAmmo::OnCurrentReserveAmmoChanged(int32 RoundsInReserve, int32 RoundsInWeapon)
{
}

void UReserveAmmo::OnRoundFired(int32 RoundsCurrent, int32 RoundsMax)
{
}

void UReserveAmmo::OnWeaponFirstReplicated(AWeapon* Weapon)
{
	AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(GetOwningPlayer()->GetPawn());
	if (!IsValid(ShooterCharacter)) return;
	OnCurrentReserveAmmoChanged(IPlayerInterface::Execute_GetReserveAmmo(ShooterCharacter), Weapon->Ammo);
}
