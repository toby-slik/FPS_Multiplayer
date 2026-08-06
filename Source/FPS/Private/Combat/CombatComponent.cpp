
#include "Combat/CombatComponent.h"

#include "TimerManager.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/WeaponData.h"
#include "Engine/Engine.h"
#include "FPS/FPS.h"
#include "GameFramework/Pawn.h"
#include "Interfaces/PlayerInterface.h"
#include "Kismet/KismetMathLibrary.h"
#include "Net/UnrealNetwork.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Weapon/Weapon.h"


UCombatComponent::UCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	TraceLength = 20'000;
	bAiming = false;
	bTriggerPressed = false;
	Local_WeaponIndex = 0;
	TargetingTraceInterval = 0.f;
	TargetingTraceAccumulator = 0.f;
	HeadshotValidationTolerance = 120.f;
	bValidateHeadshotBonePosition = true;
}



void UCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                     FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	APawn* OwningPawn = Cast<APawn>(GetOwner());
	if (!IsValid(OwningPawn) || !OwningPawn->IsLocallyControlled()) return;

	// Optional throttle on the targeting-highlight trace below, which is the only per-frame work this
	// component does. 0 means every frame, and 0 is the intended shipping value: the highlight has to track
	// the crosshair crossing the opponent, and any interval long enough to save time worth measuring is also
	// long enough to see the highlight lag the aim. Exposed purely so the cost can be dialled back if a
	// profile ever disagrees - measure before raising it.
	if (TargetingTraceInterval > 0.f)
	{
		TargetingTraceAccumulator += DeltaTime;
		if (TargetingTraceAccumulator < TargetingTraceInterval) return;
		TargetingTraceAccumulator = 0.f;
	}

	APlayerController* PC = Cast<APlayerController>(OwningPawn->GetController());
	if (!IsValid(PC)) return;
	
	FVector EyesWorldLocation;
	FRotator EyesWorldRotation;
	PC->GetActorEyesViewPoint(EyesWorldLocation, EyesWorldRotation);
	const FVector EyesWorldDirection = UKismetMathLibrary::GetForwardVector(EyesWorldRotation);
	
	const FVector Start = EyesWorldLocation;
	const FVector End = Start + EyesWorldDirection * TraceLength;
	
	FHitResult Hit;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(GetOwner());
	
	FCollisionResponseParams ResponseParams;
	ResponseParams.CollisionResponse.SetAllChannels(ECR_Ignore);
	ResponseParams.CollisionResponse.SetResponse(ECC_Pawn, ECR_Block);
	
	GetWorld()->LineTraceSingleByChannel(Hit, Start, End, FPSTraceChannels::ECC_Weapon, QueryParams, ResponseParams);
	
	bHitPlayer = IsValid(Hit.GetActor()) && Hit.GetActor()->Implements<UPlayerInterface>();
	
	if (bHitPlayer != bHitPlayerLastFrame)
	{
		OnTargetingPlayerStatusChanged.Broadcast(bHitPlayer);
	}
	
	bHitPlayerLastFrame = bHitPlayer;
}

void UCombatComponent::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	DOREPLIFETIME(UCombatComponent, Inventory);
	DOREPLIFETIME(UCombatComponent, CurrentWeapon);
	DOREPLIFETIME_CONDITION(UCombatComponent, bAiming, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UCombatComponent, CurrentReserveAmmo, COND_OwnerOnly);
}

void UCombatComponent::Initiate_CycleWeapon()
{
	if (!IsValid(CurrentWeapon)) return;
	if (CurrentWeapon->WeaponStatus == EWeaponStatus::Cycling) return;
	
	AdvanceWeaponIndex();
	Local_CycleWeapon(Local_WeaponIndex);
	// Local_CycleWeapon(WeaponIndex)
	//Server_CycleWeapon(WeaponIndex)
	
	
//Local_CycleWeapon
	//play the 
	
}

void UCombatComponent::Notify_CycleWeapon()
{
	if (!IsValid(CurrentWeapon)) return;
	
	AWeapon* NewWeapon = Inventory[Local_WeaponIndex];
	if (IsValid(NewWeapon))
	{
		EquipWeapon(NewWeapon);
	}
}

void UCombatComponent::Notify_ReloadWeapon()
{
	APawn* OwningPawn = Cast<APawn>(GetOwner());
	if (!IsValid(CurrentWeapon) || !IsValid(OwningPawn)) return;

	CurrentWeapon->WeaponStatus = EWeaponStatus::Idle;

	// Only the machine that owns the pawn drives reload completion. The server's own copy of the montage
	// plays on the 3P mesh, where a hit react or a fire montage can stomp it before the notify lands -
	// deciding the refill from there means the reload is silently dropped and the mag stays empty.
	if (!OwningPawn->IsLocallyControlled()) return;

	if (OwningPawn->HasAuthority())
	{
		Auth_ReloadWeapon();
	}
	else
	{
		Server_CompleteReload();
	}
}

void UCombatComponent::Auth_ReloadWeapon()
{
	if (!IsValid(CurrentWeapon)) return;

	const int32 EmptySpace = CurrentWeapon->MagCapacity - CurrentWeapon->Ammo;
	const int32 AmountToRefill = FMath::Min(EmptySpace, CurrentReserveAmmo);

	CurrentWeapon->Ammo += AmountToRefill;
	CurrentWeapon->WeaponStatus = EWeaponStatus::Idle;

	ReserveAmmo[CurrentWeapon->WeaponType] -= AmountToRefill;
	CurrentReserveAmmo = ReserveAmmo[CurrentWeapon->WeaponType];

	Client_ReloadWeapon(CurrentWeapon->Ammo, CurrentReserveAmmo);
}

void UCombatComponent::Server_CompleteReload_Implementation()
{
	Auth_ReloadWeapon();
}

void UCombatComponent::Client_ReloadWeapon_Implementation(int32 NewWeaponAmmo, int32 NewCarriedAmmo)
{
	APawn* OwningPawn = Cast<APawn>(GetOwner());
	if (!IsValid(CurrentWeapon) || !IsValid(OwningPawn)) return;

	if (OwningPawn->IsLocallyControlled())
	{
		// Ammo is being set outright rather than reconciled per shot, so any predicted shots still on
		// the books have to go with it - leaving them would subtract from every future mag.
		CurrentWeapon->ResetPredictionSequence();
		CurrentWeapon->Ammo = NewWeaponAmmo;
		CurrentReserveAmmo = NewCarriedAmmo;

		OnAmmoCounterChanged.Broadcast(CurrentWeapon->GetAmmoCounterDynamicMaterialInstance(), CurrentWeapon->Ammo, CurrentWeapon->MagCapacity);
		OnCurrentReserveAmmoChanged.Broadcast(CurrentReserveAmmo, CurrentWeapon->Ammo, CurrentWeapon->WeaponIcon);

		// The rounds only exist locally now, so a held trigger is picked back up here rather than at the notify.
		if (bTriggerPressed && CurrentWeapon->Ammo > 0)
		{
			Local_FireWeapon();
		}
	}
}

void UCombatComponent::Client_ConfirmHit_Implementation(bool bLethal, bool bHeadshot, float DamageDealt)
{
	OnHitConfirmed.Broadcast(bLethal, bHeadshot, DamageDealt);
}

bool UCombatComponent::Auth_IsHeadshot(const FHitResult& Hit) const
{
	if (!IsValid(CurrentWeapon)) return false;
	if (Hit.BoneName.IsNone()) return false;

	// A weapon can opt out of headshots entirely with a multiplier of 1, and there is no point paying
	// for the validation trace-back in that case.
	if (CurrentWeapon->HeadshotDamageMultiplier <= 1.f) return false;

	AActor* Target = Hit.GetActor();
	if (!IsValid(Target)) return false;

	const TArray<FName> HeadshotBones = IPlayerInterface::Execute_GetHeadshotBones(Target);
	if (!HeadshotBones.Contains(Hit.BoneName)) return false;

	return Auth_ValidateHeadshot(Hit, Hit.BoneName);
}

bool UCombatComponent::Auth_ValidateHeadshot(const FHitResult& Hit, FName BoneName) const
{
	if (!bValidateHeadshotBonePosition) return true;

	AActor* Target = Hit.GetActor();
	if (!IsValid(Target)) return false;

	USkeletalMeshComponent* TargetMesh = IPlayerInterface::Execute_GetMesh3P(Target);
	if (!IsValid(TargetMesh)) return false;

	// Rejecting an unknown bone outright is the one hard check here: a name the skeleton does not have
	// can only come from a forged FHitResult, never from latency.
	if (TargetMesh->GetBoneIndex(BoneName) == INDEX_NONE) return false;

	const FVector BoneLocation = TargetMesh->GetBoneLocation(BoneName, EBoneSpaces::WorldSpace);

	return FVector::DistSquared(Hit.ImpactPoint, BoneLocation) <= FMath::Square(HeadshotValidationTolerance);
}

void UCombatComponent::BlendOut_CycleWeapon(UAnimMontage* Montage, bool bInterrupted)
{
	UAnimInstance* AnimInstance = IPlayerInterface::Execute_GetMesh1P(GetOwner())->GetAnimInstance();
	if (IsValid(AnimInstance) && AnimInstance->OnMontageBlendingOut.IsAlreadyBound(this, &ThisClass::BlendOut_CycleWeapon))
	{
		AnimInstance->OnMontageBlendingOut.RemoveDynamic(this, &ThisClass::BlendOut_CycleWeapon);
	}
	
	CurrentWeapon->WeaponStatus = EWeaponStatus::Idle;
	
	OnReticleChanged.Broadcast(CurrentWeapon->GetReticleDynamicMaterialInstance(),CurrentWeapon->ReticleParams, bHitPlayer);
	OnAmmoCounterChanged.Broadcast(CurrentWeapon->GetAmmoCounterDynamicMaterialInstance(), CurrentWeapon->Ammo, CurrentWeapon->MagCapacity);
	OnCurrentReserveAmmoChanged.Broadcast(CurrentReserveAmmo, CurrentWeapon->Ammo, CurrentWeapon->WeaponIcon);
	
	if (bTriggerPressed && CurrentWeapon->FireType == EFireType::Auto && CurrentWeapon->Ammo > 0)
	{
		Local_FireWeapon();
	}
}

void UCombatComponent::Local_CycleWeapon(int32 WeaponIndex)
{
	AWeapon* NextWeapon = Inventory[WeaponIndex];
	if (!IsValid(NextWeapon) || !IsValid(WeaponData)) return;
	CurrentWeapon->WeaponStatus = EWeaponStatus::Cycling;
	NextWeapon->WeaponStatus = EWeaponStatus::Cycling;
	
	APawn* OwningPawn = Cast<APawn>(GetOwner());
	const bool bIsLocal = IsValid(OwningPawn) && OwningPawn->IsLocallyControlled();
	
	const FMontageData& MontageData = bIsLocal ? WeaponData->FirstPersonMontages.FindChecked(NextWeapon->WeaponType) : WeaponData->ThirdPersonMontages.FindChecked(NextWeapon->WeaponType);
	USkeletalMeshComponent* Mesh = bIsLocal ? IPlayerInterface::Execute_GetMesh1P(GetOwner()) : IPlayerInterface::Execute_GetMesh3P(GetOwner());
	if (IsValid(Mesh) && IsValid(MontageData.EquipMontage))
	{
		Mesh->GetAnimInstance()->Montage_Play(MontageData.EquipMontage);
	}
	if (bIsLocal)
	{
		ServerCycleWeapon(WeaponIndex);
		Mesh->GetAnimInstance()->OnMontageBlendingOut.AddDynamic(this, &ThisClass::BlendOut_CycleWeapon);
	}
}

void UCombatComponent::ServerCycleWeapon_Implementation(int32 WeaponIndex)
{
	Local_WeaponIndex = WeaponIndex;
	Multicast_CycleWeapon(WeaponIndex);
}

void UCombatComponent::Multicast_CycleWeapon_Implementation(int32 WeaponIndex)
{
	APawn* OwningPawn = Cast<APawn>(GetOwner());
	if (!IsValid(OwningPawn)) return;
	
	if (!OwningPawn->IsLocallyControlled())
	{
		Local_WeaponIndex = WeaponIndex;
		Local_CycleWeapon(WeaponIndex);
	}
}



bool UCombatComponent::IsOwnerSprinting() const
{
	AActor* OwningActor = GetOwner();
	if (!IsValid(OwningActor) || !OwningActor->Implements<UPlayerInterface>()) return false;

	return IPlayerInterface::Execute_IsSprinting(OwningActor);
}

void UCombatComponent::CancelOwnerSprint()
{
	AActor* OwningActor = GetOwner();
	if (!IsValid(OwningActor) || !OwningActor->Implements<UPlayerInterface>()) return;

	IPlayerInterface::Execute_CancelSprint(OwningActor);
}

void UCombatComponent::Initiate_FireWeapon_Pressed()
{
	if (!IsValid(CurrentWeapon)) return;

	// Sprinting blocks firing - but the press that breaks the sprint must still shoot.
	// Cancel first, then fall through to the normal fire path so the first shot is never dropped.
	// This only ends the sprint state: a slide or a jump in progress is untouched, and firing
	// while sliding or airborne is allowed.
	CancelOwnerSprint();

	bTriggerPressed = true;

	if (CurrentWeapon->WeaponStatus != EWeaponStatus::Idle) return;

	// A dry click starts the reload rather than doing nothing. Initiate_ReloadWeapon still decides
	// whether there is anything to reload with, so an empty reserve is a no-op as before.
	if (CurrentWeapon->Ammo <= 0)
	{
		Initiate_ReloadWeapon();
		return;
	}

	Local_FireWeapon();
}

void UCombatComponent::Local_FireWeapon()
{
	if (!IsValid(CurrentWeapon)) return;

	// Backstop for the "cannot fire while sprinting" rule - covers the auto-fire loop
	// re-entering through FireTimerFinished. By the time a cancelling press reaches here,
	// CancelOwnerSprint has already run locally, so that shot still goes off.
	if (IsOwnerSprinting()) return;

	ensure(IsValid(WeaponData));
	
	CurrentWeapon->WeaponStatus = EWeaponStatus::Firing;
	
	UAnimMontage* Montage1P = WeaponData->FirstPersonMontages.FindChecked(CurrentWeapon->WeaponType).FireMontage;
	USkeletalMeshComponent* Mesh1P = IPlayerInterface::Execute_GetMesh1P(GetOwner());
	if (IsValid(Montage1P) && IsValid(Mesh1P))
	{
		Mesh1P->GetAnimInstance()->Montage_Play(Montage1P);
	}
	
	FHitResult Hit;
	CurrentWeapon->WeaponTrace(Hit, TraceLength);
	
	EPhysicalSurface ImpactSurfaceType = Hit.PhysMaterial.IsValid(false) ? Hit.PhysMaterial->SurfaceType.GetValue() : SurfaceType1;
	CurrentWeapon->Local_Fire(Hit.ImpactPoint, Hit.ImpactNormal, ImpactSurfaceType, true);
	
	OnRoundFired.Broadcast(CurrentWeapon->Ammo, CurrentWeapon->MagCapacity, CurrentReserveAmmo);
	
	GetWorld()->GetTimerManager().SetTimer(FireTimer, this, &ThisClass::FireTimerFinished, CurrentWeapon->FireTime);
	Server_FireWeapon(Hit);
}

int32 UCombatComponent::AdvanceWeaponIndex()
{
	if (Inventory.Num() >= 2)
	{
		Local_WeaponIndex = (Local_WeaponIndex + 1) % Inventory.Num();
	}
	return Local_WeaponIndex;
}

void UCombatComponent::OnRep_CurrentReserveAmmo()
{
	if (IsValid(CurrentWeapon))
	{
		OnCurrentReserveAmmoChanged.Broadcast(CurrentReserveAmmo, CurrentWeapon->Ammo, CurrentWeapon->WeaponIcon);
	}
}



void UCombatComponent::FireTimerFinished()
{
	if (!IsValid(CurrentWeapon)) return;
	
	if (CurrentWeapon->WeaponStatus == EWeaponStatus::Firing)
	{
		CurrentWeapon->WeaponStatus = EWeaponStatus::Idle;
	}

	// Status has to gate the loop as well as the initial press. Without this, a held trigger fires
	// straight through a reload or a weapon cycle, and the fire montage stomps the reload montage
	// before its notify can land - so the reload never completes and the mag is never refilled.
	if (CurrentWeapon->WeaponStatus != EWeaponStatus::Idle) return;

	if (bTriggerPressed && CurrentWeapon->FireType == EFireType::Auto && CurrentWeapon->Ammo > 0)
	{
		Local_FireWeapon();
	}
}

void UCombatComponent::Server_FireWeapon_Implementation(const FHitResult& Hit)
{
	if (!IsValid(CurrentWeapon)) return;

	APawn* OwningPawn = Cast<APawn>(GetOwner());
	if (!IsValid(OwningPawn)) return;
	if (CurrentWeapon->Ammo <= 0) return;

	if (IsValid(Hit.GetActor()) && Hit.GetActor()->Implements<UPlayerInterface>())
	{
		// A dead pawn keeps its capsule until the respawn, so without this gate rounds fired into a
		// corpse still ran damage and would light up the hit marker. Asked through the interface
		// rather than by finding the target's UHealthComponent, matching how the rest of this
		// component reaches the pawn.
		if (IPlayerInterface::Execute_IsAlive(Hit.GetActor()))
		{
			// The multiplier is resolved here rather than inside DoDamage so the target never has to know
			// anything about weapon stats - it is handed a final number. It is also authority-only by
			// construction: Local_FireWeapon does not scale damage at all, so a client cannot pre-apply it.
			const bool bHeadshot = Auth_IsHeadshot(Hit);
			const float DamageToApply = bHeadshot ? CurrentWeapon->Damage * CurrentWeapon->HeadshotDamageMultiplier : CurrentWeapon->Damage;

			const bool bLethal = IPlayerInterface::Execute_DoDamage(Hit.GetActor(), DamageToApply, GetOwner());

			// Confirmation comes from the authoritative trace result, not the client's own trace in
			// Local_FireWeapon - the two can disagree, and a false-positive marker reads far worse
			// than one that arrives a round trip late.
			// On a listen-server host this is a Client_ RPC on a locally controlled authority pawn,
			// so it simply executes locally. No separate host path is needed.
			Client_ConfirmHit(bLethal, bHeadshot, DamageToApply);
		}
	}

	// A locally controlled authority pawn already spent its round in Local_Fire.
	// Remote clients need the server to spend the authoritative round here.
	if (!OwningPawn->IsLocallyControlled())
	{
		CurrentWeapon->Auth_Fire();
	}
	
	Multicast_FireWeapon(Hit, CurrentWeapon->Ammo);
}

void UCombatComponent::Multicast_FireWeapon_Implementation(const FHitResult& Hit, int32 AuthAmmo)
{
	APawn* OwningPawn = Cast<APawn>(GetOwner());
	if (OwningPawn->IsLocallyControlled())
	{
		CurrentWeapon->Rep_Fire(AuthAmmo);
	}
	else
	{
		ensure(IsValid(WeaponData));
		
		EPhysicalSurface ImpactSurfaceType = Hit.PhysMaterial.IsValid(false) ? Hit.PhysMaterial->SurfaceType.GetValue() : SurfaceType1;
		CurrentWeapon->Local_Fire(Hit.ImpactPoint, Hit.ImpactNormal, ImpactSurfaceType, false);
	
		UAnimMontage* Montage3P = WeaponData->ThirdPersonMontages.FindChecked(CurrentWeapon->WeaponType).FireMontage;
		USkeletalMeshComponent* Mesh3P = IPlayerInterface::Execute_GetMesh3P(GetOwner());
		if (IsValid(Montage3P) && IsValid(Mesh3P))
		{
			Mesh3P->GetAnimInstance()->Montage_Play(Montage3P);
		}
	}
}

void UCombatComponent::Initiate_FireWeapon_Released()
{
	bTriggerPressed = false;
}

void UCombatComponent::Initiate_ReloadWeapon()
{
	if (!IsValid(CurrentWeapon)) return;
	if (CurrentWeapon->WeaponStatus == EWeaponStatus::Cycling || CurrentWeapon->WeaponStatus == EWeaponStatus::Reloading) return;
	if (CurrentWeapon->Ammo == CurrentWeapon->MagCapacity) return;
	if (CurrentReserveAmmo == 0) return;
	
	Local_ReloadWeapon();
	Server_ReloadWeapon();
	
}

void UCombatComponent::Local_ReloadWeapon()
{
	
	APawn* OwningPawn = Cast<APawn>(GetOwner());
	if (!IsValid(CurrentWeapon) || !IsValid(OwningPawn)) return;
	ensure(WeaponData);
	
	const bool bIsLocal = OwningPawn->IsLocallyControlled();
	UAnimMontage* ReloadMontage = bIsLocal ? WeaponData->FirstPersonMontages.FindChecked(CurrentWeapon->WeaponType).ReloadMontage : WeaponData->ThirdPersonMontages.FindChecked(CurrentWeapon->WeaponType).ReloadMontage;
	USkeletalMeshComponent* Mesh = bIsLocal ? IPlayerInterface::Execute_GetMesh1P(OwningPawn) : IPlayerInterface::Execute_GetMesh3P(OwningPawn);
	if (IsValid(ReloadMontage) && IsValid(Mesh))
	{
		Mesh->GetAnimInstance()->Montage_Play(ReloadMontage);
	}

	UAnimMontage* WeaponReloadMontage = WeaponData->WeaponMontages.FindChecked(CurrentWeapon->WeaponType).ReloadMontage;
	USkeletalMeshComponent* WeaponMesh = bIsLocal ? CurrentWeapon->GetMesh1P() : CurrentWeapon->GetMesh3P();
	if (IsValid(WeaponReloadMontage) && IsValid(WeaponMesh))
	{
		if (UAnimInstance* AnimInstance = WeaponMesh->GetAnimInstance())
		{
			WeaponMesh->GetAnimInstance()->Montage_Play(WeaponReloadMontage);
		}
	}
	CurrentWeapon->WeaponStatus = EWeaponStatus::Reloading;

	
	
}

void UCombatComponent::Server_ReloadWeapon_Implementation()
{
	Multicast_ReloadWeapon();
}

void UCombatComponent::Multicast_ReloadWeapon_Implementation()
{
	APawn* OwningPawn = Cast<APawn>(GetOwner());
	if (!IsValid(OwningPawn)) return;

	// The instigator already played its own reload in Initiate_ReloadWeapon. Playing it again here
	// restarts the montage a round trip in, which shifts or duplicates the notify it is waiting on.
	if (OwningPawn->IsLocallyControlled()) return;

	Local_ReloadWeapon();
}

void UCombatComponent::Initiate_Aim_Pressed()
{
	// Aiming cancels sprint, then always proceeds. The gate is on the sprint state only,
	// so aiming stays available while airborne, sliding and later wall-running.
	CancelOwnerSprint();

	Local_Aim(true);
	Server_Aim(true);
}

void UCombatComponent::Initiate_Aim_Released()
{
	Local_Aim(false);
	Server_Aim(false);
}

void UCombatComponent::Server_Aim_Implementation(bool bPressed)
{
	Local_Aim(bPressed);
}

void UCombatComponent::Local_Aim(bool bPressed)
{
	bAiming = bPressed;
	OnAimingStatusChanged.Broadcast(bAiming);
}


void UCombatComponent::Equip(AWeapon* Weapon)
{
	
	CurrentWeapon = Weapon;
	CurrentWeapon->AttachToOwningPawn(Cast<APawn>(GetOwner()));
	
	CurrentWeapon->WeaponStatus = EWeaponStatus::Idle;
	
	CurrentReserveAmmo = ReserveAmmo.FindChecked(CurrentWeapon->WeaponType);
	OnCurrentReserveAmmoChanged.Broadcast(CurrentReserveAmmo, Weapon->Ammo, CurrentWeapon->WeaponIcon);
}

void UCombatComponent::EquipWeapon(AWeapon* Weapon)
{
	if (!IsValid(Weapon) || !IsValid(GetOwner())) return;
	if (GetOwner()->GetLocalRole() == ROLE_Authority)
	{
		SetCurrentWeapon(Weapon, CurrentWeapon);
	}
	else
	{
		Server_EquipWeapon(Weapon);
	}
}

void UCombatComponent::SetCurrentWeapon(AWeapon* NewWeapon, AWeapon* LastWeapon)
{
	AWeapon* LocalLastWeapon = nullptr;
	
	if (IsValid(LastWeapon))
	{
		LocalLastWeapon = LastWeapon;
	}
	else if (NewWeapon != CurrentWeapon)
	{
		LocalLastWeapon = CurrentWeapon;
	}
	
	if (IsValid(LocalLastWeapon))
	{
		LocalLastWeapon->DetachFromOwningPawn();
		LocalLastWeapon->WeaponStatus = EWeaponStatus::Unequipped;
	}
	
	CurrentWeapon = NewWeapon;
	APawn* OwningPawn = Cast<APawn>(GetOwner());
	if (IsValid(OwningPawn) && OwningPawn->HasAuthority() && IsValid(CurrentWeapon))
	{
		CurrentReserveAmmo = ReserveAmmo.FindChecked(CurrentWeapon->WeaponType);
	}
	
	CurrentWeapon->AttachToOwningPawn(OwningPawn);
}

void UCombatComponent::Server_EquipWeapon_Implementation(AWeapon* Weapon)
{
	EquipWeapon(Weapon);
}

void UCombatComponent::SpawnInventory()
{
	if (GetOwner()->GetLocalRole() < ROLE_Authority) return;
	
	for (TSubclassOf<AWeapon>& WeaponClass : DefaultWeaponClass)
	{
		AWeapon* Weapon = SpawnWeapon(WeaponClass);
		Inventory.AddUnique(Weapon);
		ReserveAmmo.Add(Weapon->WeaponType, Weapon->StartingCarriedAmmo);
	}
	
	if (Inventory.Num() > 0)
	{
		Equip(Inventory[0]);
		InitializeWeaponWidgets();
	}
}

void UCombatComponent::DestroyInventory()
{
	for (AWeapon* Weapon : Inventory)
	{
		if (IsValid(Weapon))
		{
			Weapon->Destroy();
		}
	}
}

void UCombatComponent::InitializeWeaponWidgets() const
{
	if (IsValid(CurrentWeapon))
	{
		OnReticleChanged.Broadcast(CurrentWeapon->GetReticleDynamicMaterialInstance(), CurrentWeapon->ReticleParams, bHitPlayer);
		OnAmmoCounterChanged.Broadcast(CurrentWeapon->GetAmmoCounterDynamicMaterialInstance(), CurrentWeapon->Ammo, CurrentWeapon->MagCapacity);
	}
}



void UCombatComponent::OnRep_CurrentWeapon(AWeapon* LastWeapon)
{
	SetCurrentWeapon(CurrentWeapon, LastWeapon);
	
	IPlayerInterface::Execute_WeaponReplicated(GetOwner());
	InitializeWeaponWidgets();
}



AWeapon* UCombatComponent::SpawnWeapon(TSubclassOf<AWeapon> WeaponClass) const
{
	AActor* OwningActor = GetOwner();
	if (!IsValid(OwningActor)) return nullptr;
	if (OwningActor->GetLocalRole() < ROLE_Authority) return nullptr;
	
	FActorSpawnParameters SpawnInfo;
	SpawnInfo.Instigator = Cast<APawn>(OwningActor);
	SpawnInfo.Owner = OwningActor;
	SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	
	return GetWorld()->SpawnActor<AWeapon>(WeaponClass, SpawnInfo);
}

