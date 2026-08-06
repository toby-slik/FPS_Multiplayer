
#include "Combat/CombatComponent.h"

#include "TimerManager.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/AttachmentData.h"
#include "Data/WeaponData.h"
#include "Engine/Engine.h"
#include "FPS/FPS.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Interfaces/PlayerInterface.h"
#include "Kismet/KismetMathLibrary.h"
#include "Net/UnrealNetwork.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Player/ShooterPlayerController.h"
#include "Weapon/Weapon.h"


UCombatComponent::UCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	TraceLength = 20'000;
	bAiming = false;
	bTriggerPressed = false;
	bInventorySpawned = false;
	Local_WeaponIndex = 0;
	LastServerFireTime = -1.0e9;
	TargetingTraceInterval = 0.f;
	TargetingTraceAccumulator = 0.f;
	HeadshotValidationTolerance = 120.f;
	bValidateHeadshotBonePosition = true;
}

void UCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FireTimer);
	}
	bTriggerPressed = false;

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		DestroyInventory();
	}

	Super::EndPlay(EndPlayReason);
}



void UCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                     FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	APawn* OwningPawn = Cast<APawn>(GetOwner());
	if (IsValid(CurrentWeapon) && IsValid(GetWorld()))
	{
		CurrentWeapon->AdvanceAndGetHeat(GetWorld()->GetTimeSeconds());
		if (GetNetMode() != NM_DedicatedServer)
		{
			CurrentWeapon->UpdateWeaponKick(DeltaTime);
		}
	}
	// Everything below this line exists to drive the local player's reticle highlight, so it is gated on
	// being the first-person viewer rather than merely being locally controlled - that keeps a bot from
	// paying for a per-frame trace whose only consumer is a HUD it does not have.
	if (!IsValid(OwningPawn) || !IsOwnerFirstPerson()) return;

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
	if (Inventory.Num() < 2) return;
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
	if (!Inventory.IsValidIndex(Local_WeaponIndex)) return;
	
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
	int32* ReserveForWeapon = ReserveAmmo.Find(CurrentWeapon->WeaponType);
	if (!ReserveForWeapon)
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot reload %s: no reserve-ammo entry for tag %s"),
			*GetNameSafe(CurrentWeapon), *CurrentWeapon->WeaponType.ToString());
		return;
	}

	const int32 EmptySpace = CurrentWeapon->GetEffectiveMagCapacity() - CurrentWeapon->Ammo;
	const int32 AmountToRefill = FMath::Min(EmptySpace, *ReserveForWeapon);

	CurrentWeapon->Ammo += AmountToRefill;
	CurrentWeapon->WeaponStatus = EWeaponStatus::Idle;

	*ReserveForWeapon -= AmountToRefill;
	CurrentReserveAmmo = *ReserveForWeapon;

	Client_ReloadWeapon(CurrentWeapon->Ammo, CurrentReserveAmmo);
}

void UCombatComponent::Server_CompleteReload_Implementation()
{
	if (!IsValid(CurrentWeapon) || CurrentWeapon->WeaponStatus != EWeaponStatus::Reloading) return;
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

		OnAmmoCounterChanged.Broadcast(CurrentWeapon->GetAmmoCounterDynamicMaterialInstance(), CurrentWeapon->Ammo, CurrentWeapon->GetEffectiveMagCapacity());
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
	if (!IsValid(CurrentWeapon) || !IsValid(GetOwner()) || !GetOwner()->Implements<UPlayerInterface>()) return;

	// Must resolve the same mesh Local_CycleWeapon bound the delegate to, or the unbind silently misses and
	// this fires again on every later montage blend-out. A bot animates the cycle on its 3P mesh, so hard-coding
	// Mesh1P here would leak the binding for exactly the pawn that cycles most.
	USkeletalMeshComponent* Mesh = IsOwnerFirstPerson()
		? IPlayerInterface::Execute_GetMesh1P(GetOwner())
		: IPlayerInterface::Execute_GetMesh3P(GetOwner());
	UAnimInstance* AnimInstance = IsValid(Mesh) ? Mesh->GetAnimInstance() : nullptr;
	if (IsValid(AnimInstance) && AnimInstance->OnMontageBlendingOut.IsAlreadyBound(this, &ThisClass::BlendOut_CycleWeapon))
	{
		AnimInstance->OnMontageBlendingOut.RemoveDynamic(this, &ThisClass::BlendOut_CycleWeapon);
	}
	
	CurrentWeapon->WeaponStatus = EWeaponStatus::Idle;
	
	OnReticleChanged.Broadcast(CurrentWeapon->GetReticleDynamicMaterialInstance(),CurrentWeapon->ReticleParams, bHitPlayer);
	OnAmmoCounterChanged.Broadcast(CurrentWeapon->GetAmmoCounterDynamicMaterialInstance(), CurrentWeapon->Ammo, CurrentWeapon->GetEffectiveMagCapacity());
	OnCurrentReserveAmmoChanged.Broadcast(CurrentReserveAmmo, CurrentWeapon->Ammo, CurrentWeapon->WeaponIcon);
	
	if (bTriggerPressed && CurrentWeapon->FireType == EFireType::Auto && CurrentWeapon->Ammo > 0)
	{
		Local_FireWeapon();
	}
}

void UCombatComponent::Local_CycleWeapon(int32 WeaponIndex)
{
	if (!Inventory.IsValidIndex(WeaponIndex) || !IsValid(CurrentWeapon) || !IsValid(WeaponData)) return;
	AWeapon* NextWeapon = Inventory[WeaponIndex];
	if (!IsValid(NextWeapon)) return;
	CurrentWeapon->WeaponStatus = EWeaponStatus::Cycling;
	NextWeapon->WeaponStatus = EWeaponStatus::Cycling;
	
	APawn* OwningPawn = Cast<APawn>(GetOwner());
	const bool bIsLocal = IsValid(OwningPawn) && OwningPawn->IsLocallyControlled();

	// Two different questions, and they only give the same answer for a human player. bFirstPerson picks the
	// assets; bIsLocal below decides who owns the swap - a bot is locally controlled on the authority but has
	// no first-person view, so it must animate and be watched on the 3P mesh.
	const bool bFirstPerson = IsOwnerFirstPerson();

	const TMap<FGameplayTag, FMontageData>& MontageMap = bFirstPerson ? WeaponData->FirstPersonMontages : WeaponData->ThirdPersonMontages;
	const FMontageData* MontageData = MontageMap.Find(NextWeapon->WeaponType);
	if (!MontageData)
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot cycle to %s: no %s montage data for tag %s"),
			*GetNameSafe(NextWeapon), bFirstPerson ? TEXT("first-person") : TEXT("third-person"),
			*NextWeapon->WeaponType.ToString());
		CurrentWeapon->WeaponStatus = EWeaponStatus::Idle;
		NextWeapon->WeaponStatus = EWeaponStatus::Unequipped;
		return;
	}
	USkeletalMeshComponent* Mesh = bFirstPerson ? IPlayerInterface::Execute_GetMesh1P(GetOwner()) : IPlayerInterface::Execute_GetMesh3P(GetOwner());
	UAnimInstance* AnimInstance = IsValid(Mesh) ? Mesh->GetAnimInstance() : nullptr;
	if (IsValid(AnimInstance) && IsValid(MontageData->EquipMontage))
	{
		// The swap is timed by the montage - Notify_CycleWeapon inside it is what actually hands the new weapon
		// over, and BlendOut_CycleWeapon is what returns control - so a play rate is the whole of "faster weapon
		// swapping" with no timer to keep in step. Taken from the weapon being drawn rather than the one being
		// stowed, because the quick-draw grip belongs to the gun coming up.
		AnimInstance->Montage_Play(MontageData->EquipMontage, NextWeapon->GetEquipPlayRate());
	}
	if (bIsLocal && IsValid(AnimInstance))
	{
		ServerCycleWeapon(WeaponIndex);
		AnimInstance->OnMontageBlendingOut.AddUniqueDynamic(this, &ThisClass::BlendOut_CycleWeapon);
	}
}

void UCombatComponent::ServerCycleWeapon_Implementation(int32 WeaponIndex)
{
	if (!Inventory.IsValidIndex(WeaponIndex) || !IsValid(Inventory[WeaponIndex])) return;
	if (Inventory[WeaponIndex]->GetOwner() != GetOwner()) return;

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

bool UCombatComponent::IsOwnerFirstPerson() const
{
	AActor* OwningActor = GetOwner();
	if (!IsValid(OwningActor) || !OwningActor->Implements<UPlayerInterface>()) return false;

	return IPlayerInterface::Execute_IsFirstPersonViewer(OwningActor);
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
		// Asked after the fact rather than re-testing Initiate_ReloadWeapon's conditions here, so the two
		// can never disagree about whether a reload is running. Local_ReloadWeapon is what sets Reloading,
		// and the status guard above guarantees it was Idle a line ago.
		Initiate_ReloadWeapon();
		const bool bReloadStarted = CurrentWeapon->WeaponStatus == EWeaponStatus::Reloading;

		CurrentWeapon->Local_DryFire(bReloadStarted);
		return;
	}

	Local_FireWeapon();
}

void UCombatComponent::Local_FireWeapon()
{
	AWeapon* FiredWeapon = CurrentWeapon;
	if (!IsValid(FiredWeapon) || !IsValid(WeaponData) || !IsValid(GetOwner()) ||
		!GetOwner()->Implements<UPlayerInterface>()) return;

	// Backstop for the "cannot fire while sprinting" rule - covers the auto-fire loop
	// re-entering through FireTimerFinished. By the time a cancelling press reaches here,
	// CancelOwnerSprint has already run locally, so that shot still goes off.
	if (IsOwnerSprinting()) return;

	FiredWeapon->WeaponStatus = EWeaponStatus::Firing;

	// A bot reaches this function too - it is locally controlled on the authority - but it has no arms to
	// animate, so it plays its shot on the 3P mesh right here. That is also what makes Multicast_FireWeapon
	// below correct without change: the multicast's cosmetic branch deliberately skips the shooter's own
	// machine, and for a bot the shooter's own machine IS the listen-server host the human is watching from.
	// Without this the host would see the bot fire with no animation at all.
	const bool bFirstPerson = IsOwnerFirstPerson();

	UAnimMontage* FireMontage = nullptr;
	const TMap<FGameplayTag, FMontageData>& MontageMap = bFirstPerson ? WeaponData->FirstPersonMontages : WeaponData->ThirdPersonMontages;
	if (const FMontageData* MontageData = MontageMap.Find(FiredWeapon->WeaponType))
	{
		FireMontage = MontageData->FireMontage;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Missing %s montage data for weapon tag %s"),
			bFirstPerson ? TEXT("first-person") : TEXT("third-person"),
			*FiredWeapon->WeaponType.ToString());
	}
	USkeletalMeshComponent* FiringMesh = bFirstPerson
		? IPlayerInterface::Execute_GetMesh1P(GetOwner())
		: IPlayerInterface::Execute_GetMesh3P(GetOwner());
	UAnimInstance* FiringAnimInstance = IsValid(FiringMesh) ? FiringMesh->GetAnimInstance() : nullptr;
	if (IsValid(FireMontage) && IsValid(FiringAnimInstance))
	{
		FiringAnimInstance->Montage_Play(FireMontage);
	}

	FHitResult Hit;
	const uint16 SpreadSeed = static_cast<uint16>(FMath::RandHelper(MAX_uint16 + 1));
	const float SpreadDegrees = PrepareWeaponSpread(FiredWeapon, Cast<APawn>(GetOwner()));
	FiredWeapon->WeaponTrace(Hit, TraceLength, SpreadDegrees, SpreadSeed);
	
	EPhysicalSurface ImpactSurfaceType = Hit.PhysMaterial.IsValid(false) ? Hit.PhysMaterial->SurfaceType.GetValue() : SurfaceType1;
	FiredWeapon->Local_Fire(Hit.ImpactPoint, Hit.ImpactNormal, ImpactSurfaceType, bFirstPerson);

	// Before the heat this round adds, so the punch is priced on the heat the shot arrived with - exactly
	// as the cone above was. Moving it after would make the first round of every burst kick as though it
	// were the second.
	ApplyLocalFireRecoil(FiredWeapon);

	// On a listen-server host Server_FireWeapon below is the authority path and adds the heat itself, so
	// adding it here as well would count every host shot twice and double the host's spread.
	if (!FiredWeapon->HasAuthority())
	{
		FiredWeapon->AddRecoilHeat(GetWorld()->GetTimeSeconds());
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		FiredWeapon->ApplyWeaponKick(bAiming);
	}

	// On a listen-server host this executes immediately and spends the authoritative round before the UI
	// broadcast below. Remote owning clients have already predicted their local ammo in Local_Fire.
	Server_FireWeapon(FiredWeapon, SpreadSeed);

	OnRoundFired.Broadcast(FiredWeapon->Ammo, FiredWeapon->GetEffectiveMagCapacity(), CurrentReserveAmmo);
	
	GetWorld()->GetTimerManager().SetTimer(FireTimer, this, &ThisClass::FireTimerFinished, FiredWeapon->FireTime);
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

float UCombatComponent::PrepareWeaponSpread(AWeapon* Weapon, APawn* OwningPawn) const
{
	if (!IsValid(Weapon) || !IsValid(OwningPawn) || !IsValid(GetWorld())) return 0.f;

	if (!OwningPawn->Implements<UPlayerInterface>()) return 0.f;

	// Settled before the cone is measured, so the shot is priced on the heat it arrives with rather than on
	// heat that has already been shed. The round's own contribution is added afterwards by the caller,
	// which is what keeps the first shot from cold perfectly accurate.
	Weapon->AdvanceAndGetHeat(GetWorld()->GetTimeSeconds());

	const float MovementThreshold = FMath::Max(Weapon->RecoilParams.SpreadMovementSpeedThreshold, 0.f);

	// Asked through the interface rather than read off the movement component directly, matching how the
	// rest of this component reaches the pawn. It also gets wall-running right: a wall run is a custom
	// movement mode, so IsFalling() is false throughout one and reading it here would have handed the
	// player ground-grade accuracy for the whole run.
	const bool bIsMoving = IPlayerInterface::Execute_IsMovingFasterThan(OwningPawn, MovementThreshold);
	const bool bIsAirborne = IPlayerInterface::Execute_IsAirborne(OwningPawn);

	return Weapon->GetSpreadDegrees(bAiming, bIsMoving, bIsAirborne);
}

void UCombatComponent::ApplyLocalFireRecoil(AWeapon* FiredWeapon)
{
	APawn* OwningPawn = Cast<APawn>(GetOwner());
	if (!IsValid(FiredWeapon) || !IsValid(OwningPawn)) return;

	// Both channels below are local cosmetics or local view state. On a listen-server host this function
	// still runs - the host is locally controlled - but on a dedicated server there is no view to punch and
	// no camera to shake, and Local_FireWeapon is only reached by the owning client in any case.
	if (!OwningPawn->IsLocallyControlled()) return;

	const FRecoilParams& Params = FiredWeapon->RecoilParams;

	// --- View punch ---
	// Queued on the controller rather than applied here, because the controller is the only place that can
	// add to the view without fighting the player's own mouse for the frame - see UpdateRotation.
	if (AShooterPlayerController* PC = Cast<AShooterPlayerController>(OwningPawn->GetController()); IsValid(PC))
	{
		const float PunchPitch = FiredWeapon->GetViewPunchPitch(bAiming);

		// Both channels are asked of the weapon now rather than assembled from RecoilParams here, so a recoil
		// attachment cannot reach one of them and miss the other.
		const float YawRange = FiredWeapon->GetViewPunchYawRange(bAiming);

		const float PunchYaw = YawRange > 0.f ? FMath::FRandRange(-YawRange, YawRange) : 0.f;

		PC->AddViewRecoil(PunchPitch, PunchYaw);
	}

	// --- Screen shake ---
	const float ShakeScale = bAiming ? FMath::Max(Params.AimCameraShakeMultiplier, 0.f) : 1.f;
	if (OwningPawn->Implements<UPlayerInterface>())
	{
		IPlayerInterface::Execute_AddCameraShake(
			OwningPawn,
			Params.CameraShakeAmplitude * ShakeScale,
			Params.CameraShakeFrequency,
			Params.CameraShakeDuration,
			Params.CameraShakeClass);
	}
}

void UCombatComponent::Server_FireWeapon_Implementation(AWeapon* FiredWeapon, uint16 SpreadSeed)
{
	APawn* OwningPawn = Cast<APawn>(GetOwner());
	if (!IsValid(OwningPawn) || !IsValid(FiredWeapon)) return;
	if (FiredWeapon != CurrentWeapon || !Inventory.Contains(FiredWeapon) || FiredWeapon->GetOwner() != GetOwner())
	{
		Client_CorrectFire(FiredWeapon, IsValid(FiredWeapon) ? FiredWeapon->Ammo : 0);
		return;
	}
	if (FiredWeapon->Ammo <= 0)
	{
		Client_CorrectFire(FiredWeapon, FiredWeapon->Ammo);
		return;
	}

	const double ServerTime = GetWorld()->GetTimeSeconds();
	const double MinimumFireInterval = FMath::Max(static_cast<double>(FiredWeapon->FireTime) * 0.75, 0.01);
	if (ServerTime - LastServerFireTime < MinimumFireInterval)
	{
		Client_CorrectFire(FiredWeapon, FiredWeapon->Ammo);
		return;
	}
	LastServerFireTime = ServerTime;

	FHitResult Hit;
	const float SpreadDegrees = PrepareWeaponSpread(FiredWeapon, OwningPawn);
	FiredWeapon->WeaponTrace(Hit, TraceLength, SpreadDegrees, SpreadSeed);

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
			const float DamageToApply = bHeadshot ? FiredWeapon->Damage * FiredWeapon->HeadshotDamageMultiplier : FiredWeapon->Damage;

			const bool bLethal = IPlayerInterface::Execute_DoDamage(Hit.GetActor(), DamageToApply, GetOwner());

			// Confirmation comes from the authoritative trace result, not the client's own trace in
			// Local_FireWeapon - the two can disagree, and a false-positive marker reads far worse
			// than one that arrives a round trip late.
			// On a listen-server host this is a Client_ RPC on a locally controlled authority pawn,
			// so it simply executes locally. No separate host path is needed.
			Client_ConfirmHit(bLethal, bHeadshot, DamageToApply);
		}
	}

	FiredWeapon->Auth_Fire();
	FiredWeapon->AddRecoilHeat(GetWorld()->GetTimeSeconds());

	const EPhysicalSurface ImpactSurfaceType = Hit.PhysMaterial.IsValid(false)
		? Hit.PhysMaterial->SurfaceType.GetValue()
		: SurfaceType1;
	Multicast_FireWeapon(FiredWeapon, Hit.ImpactPoint, Hit.ImpactNormal,
		static_cast<uint8>(ImpactSurfaceType), FiredWeapon->Ammo);
}

void UCombatComponent::Multicast_FireWeapon_Implementation(AWeapon* FiredWeapon, FVector_NetQuantize ImpactPoint,
	FVector_NetQuantizeNormal ImpactNormal, uint8 ImpactSurfaceType, int32 AuthAmmo)
{
	APawn* OwningPawn = Cast<APawn>(GetOwner());
	if (!IsValid(OwningPawn) || !IsValid(FiredWeapon)) return;

	if (OwningPawn->IsLocallyControlled())
	{
		FiredWeapon->Rep_Fire(AuthAmmo);
	}
	else
	{
		FiredWeapon->Local_Fire(ImpactPoint, ImpactNormal,
			static_cast<EPhysicalSurface>(ImpactSurfaceType), false);
		if (!FiredWeapon->HasAuthority() && IsValid(GetWorld()))
		{
			FiredWeapon->AddRecoilHeat(GetWorld()->GetTimeSeconds());
		}
		if (GetNetMode() != NM_DedicatedServer)
		{
			FiredWeapon->ApplyWeaponKick(bAiming);
		}

		UAnimMontage* Montage3P = nullptr;
		if (IsValid(WeaponData))
		{
			if (const FMontageData* MontageData = WeaponData->ThirdPersonMontages.Find(FiredWeapon->WeaponType))
			{
				Montage3P = MontageData->FireMontage;
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("Missing third-person montage data for weapon tag %s"),
					*FiredWeapon->WeaponType.ToString());
			}
		}
		USkeletalMeshComponent* Mesh3P = IPlayerInterface::Execute_GetMesh3P(GetOwner());
		UAnimInstance* AnimInstance3P = IsValid(Mesh3P) ? Mesh3P->GetAnimInstance() : nullptr;
		if (IsValid(Montage3P) && IsValid(AnimInstance3P))
		{
			AnimInstance3P->Montage_Play(Montage3P);
		}
	}
}

void UCombatComponent::Client_CorrectFire_Implementation(AWeapon* FiredWeapon, int32 AuthAmmo)
{
	if (!IsValid(FiredWeapon) || FiredWeapon->GetInstigator() != GetOwner()) return;

	FiredWeapon->ResetPredictionSequence();
	FiredWeapon->Ammo = FMath::Clamp(AuthAmmo, 0, FiredWeapon->GetEffectiveMagCapacity());
	if (FiredWeapon == CurrentWeapon)
	{
		OnRoundFired.Broadcast(FiredWeapon->Ammo, FiredWeapon->GetEffectiveMagCapacity(), CurrentReserveAmmo);
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
	if (CurrentWeapon->Ammo == CurrentWeapon->GetEffectiveMagCapacity()) return;
	if (CurrentReserveAmmo == 0) return;
	
	Local_ReloadWeapon();
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		Multicast_ReloadWeapon();
	}
	else
	{
		Server_ReloadWeapon();
	}
	
}

void UCombatComponent::Local_ReloadWeapon()
{
	
	APawn* OwningPawn = Cast<APawn>(GetOwner());
	if (!IsValid(CurrentWeapon) || !IsValid(OwningPawn) || !IsValid(WeaponData) ||
		!OwningPawn->Implements<UPlayerInterface>()) return;
	
	// Asset selection, not logic ownership - a bot reloads on its 3P mesh where the human can see it, and
	// Notify_ReloadWeapon still keys the refill off IsLocallyControlled().
	const bool bIsLocal = IsOwnerFirstPerson();
	const TMap<FGameplayTag, FMontageData>& MontageMap = bIsLocal ? WeaponData->FirstPersonMontages : WeaponData->ThirdPersonMontages;
	const FMontageData* MontageData = MontageMap.Find(CurrentWeapon->WeaponType);
	if (!MontageData)
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot reload %s: no %s montage data for tag %s"),
			*GetNameSafe(CurrentWeapon), bIsLocal ? TEXT("first-person") : TEXT("third-person"),
			*CurrentWeapon->WeaponType.ToString());
		return;
	}
	// Read once and used for both the character and the weapon montage below, so the mag going into the gun
	// stays lined up with the hand putting it there. It comes from replicated attachment data, so an observer
	// running this same function sees the reload at the same speed the shooter does.
	//
	// Worth knowing: because the refill is driven by an anim notify on the owning client, this play rate IS the
	// reload duration and the server does not time it independently - so a Fast Mags reload is trusted the same
	// way the base reload already is. Nothing new is exposed here, but server-side reload timing is the place
	// to look if that trust ever has to be tightened.
	const float ReloadPlayRate = CurrentWeapon->GetReloadPlayRate();

	UAnimMontage* ReloadMontage = MontageData->ReloadMontage;
	USkeletalMeshComponent* Mesh = bIsLocal ? IPlayerInterface::Execute_GetMesh1P(OwningPawn) : IPlayerInterface::Execute_GetMesh3P(OwningPawn);
	UAnimInstance* PlayerAnimInstance = IsValid(Mesh) ? Mesh->GetAnimInstance() : nullptr;
	if (IsValid(ReloadMontage) && IsValid(PlayerAnimInstance))
	{
		PlayerAnimInstance->Montage_Play(ReloadMontage, ReloadPlayRate);
	}

	UAnimMontage* WeaponReloadMontage = nullptr;
	if (const FMontageData* WeaponMontageData = WeaponData->WeaponMontages.Find(CurrentWeapon->WeaponType))
	{
		WeaponReloadMontage = WeaponMontageData->ReloadMontage;
	}
	USkeletalMeshComponent* WeaponMesh = bIsLocal ? CurrentWeapon->GetMesh1P() : CurrentWeapon->GetMesh3P();
	if (IsValid(WeaponReloadMontage) && IsValid(WeaponMesh))
	{
		if (UAnimInstance* AnimInstance = WeaponMesh->GetAnimInstance())
		{
			AnimInstance->Montage_Play(WeaponReloadMontage, ReloadPlayRate);
		}
	}
	CurrentWeapon->ResetRecoilState();
	CurrentWeapon->WeaponStatus = EWeaponStatus::Reloading;

	
	
}

void UCombatComponent::Server_ReloadWeapon_Implementation()
{
	if (!IsValid(CurrentWeapon) || CurrentWeapon->WeaponStatus != EWeaponStatus::Idle) return;
	if (CurrentWeapon->Ammo >= CurrentWeapon->GetEffectiveMagCapacity()) return;
	const int32* ReserveForWeapon = ReserveAmmo.Find(CurrentWeapon->WeaponType);
	if (!ReserveForWeapon || *ReserveForWeapon <= 0) return;

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
	if (!IsValid(Weapon) || Weapon->GetOwner() != GetOwner()) return;

	SetCurrentWeapon(Weapon, CurrentWeapon);
	if (!IsValid(CurrentWeapon)) return;

	CurrentWeapon->WeaponStatus = EWeaponStatus::Idle;
	OnCurrentReserveAmmoChanged.Broadcast(CurrentReserveAmmo, CurrentWeapon->Ammo, CurrentWeapon->WeaponIcon);
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
	if (!IsValid(CurrentWeapon) || !IsValid(OwningPawn)) return;

	if (OwningPawn->HasAuthority())
	{
		if (const int32* ReserveForWeapon = ReserveAmmo.Find(CurrentWeapon->WeaponType))
		{
			CurrentReserveAmmo = *ReserveForWeapon;
		}
		else
		{
			CurrentReserveAmmo = 0;
			UE_LOG(LogTemp, Error, TEXT("Cannot equip %s: no reserve-ammo entry for tag %s"),
				*GetNameSafe(CurrentWeapon), *CurrentWeapon->WeaponType.ToString());
		}
	}
	
	CurrentWeapon->AttachToOwningPawn(OwningPawn);
	CurrentWeapon->WeaponStatus = EWeaponStatus::Idle;
}

void UCombatComponent::Server_EquipWeapon_Implementation(AWeapon* Weapon)
{
	if (!IsValid(Weapon) || !Inventory.Contains(Weapon) || Weapon->GetOwner() != GetOwner()) return;
	SetCurrentWeapon(Weapon, CurrentWeapon);
}

bool UCombatComponent::Auth_EquipAttachment(AWeapon* Weapon, UAttachmentData* Definition, EAttachmentRarity InstanceRarity)
{
	AActor* OwningActor = GetOwner();
	if (!IsValid(OwningActor) || !OwningActor->HasAuthority()) return false;
	if (!IsValid(Weapon) || !IsValid(Definition)) return false;

	// Same ownership gate the fire and equip RPCs use: the weapon has to be one of ours. Without it this
	// becomes a way to modify another player's gun through our own component.
	if (!Inventory.Contains(Weapon) || Weapon->GetOwner() != OwningActor) return false;

	if (!Weapon->Auth_SetAttachment(Definition, InstanceRarity)) return false;

	// The authority's own HUD is not driven by OnRep_Attachments - a listen-server host never receives its own
	// replication - so the redraw is kicked here as well. On a dedicated server this broadcasts to nothing.
	NotifyAttachmentsChanged(Weapon);

	return true;
}

bool UCombatComponent::Auth_RemoveAttachment(AWeapon* Weapon, EAttachmentSlot Slot)
{
	AActor* OwningActor = GetOwner();
	if (!IsValid(OwningActor) || !OwningActor->HasAuthority()) return false;
	if (!IsValid(Weapon)) return false;
	if (!Inventory.Contains(Weapon) || Weapon->GetOwner() != OwningActor) return false;

	if (!Weapon->Auth_ClearAttachmentSlot(Slot)) return false;

	NotifyAttachmentsChanged(Weapon);

	return true;
}

void UCombatComponent::NotifyAttachmentsChanged(const AWeapon* Weapon) const
{
	if (!IsValid(Weapon) || Weapon != CurrentWeapon.Get()) return;

	OnAmmoCounterChanged.Broadcast(CurrentWeapon->GetAmmoCounterDynamicMaterialInstance(), CurrentWeapon->Ammo, CurrentWeapon->GetEffectiveMagCapacity());
	OnCurrentReserveAmmoChanged.Broadcast(CurrentReserveAmmo, CurrentWeapon->Ammo, CurrentWeapon->WeaponIcon);
}

void UCombatComponent::SpawnInventory()
{
	AActor* OwningActor = GetOwner();
	if (!IsValid(OwningActor) || !OwningActor->HasAuthority() || bInventorySpawned) return;
	bInventorySpawned = true;
	
	for (const TSubclassOf<AWeapon>& WeaponClass : DefaultWeaponClass)
	{
		if (!IsValid(WeaponClass.Get()))
		{
			UE_LOG(LogTemp, Error, TEXT("%s has an invalid DefaultWeaponClass entry"), *GetNameSafe(OwningActor));
			continue;
		}

		AWeapon* Weapon = SpawnWeapon(WeaponClass);
		if (!IsValid(Weapon))
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to spawn inventory weapon class %s for %s"),
				*GetNameSafe(WeaponClass.Get()), *GetNameSafe(OwningActor));
			continue;
		}
		if (ReserveAmmo.Contains(Weapon->WeaponType))
		{
			UE_LOG(LogTemp, Error, TEXT("Duplicate inventory weapon tag %s on %s; destroying duplicate %s"),
				*Weapon->WeaponType.ToString(), *GetNameSafe(OwningActor), *GetNameSafe(Weapon));
			Weapon->Destroy();
			continue;
		}

		Inventory.Add(Weapon);
		ReserveAmmo.Add(Weapon->WeaponType, Weapon->StartingCarriedAmmo);
	}
	
	if (Inventory.Num() > 0)
	{
		Equip(Inventory[0]);
		InitializeWeaponWidgets();
	}
	else
	{
		bInventorySpawned = false;
	}
}

void UCombatComponent::DestroyInventory()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FireTimer);
	}
	bTriggerPressed = false;
	Local_WeaponIndex = 0;
	LastServerFireTime = -1.0e9;
	bInventorySpawned = false;

	AWeapon* LastWeapon = CurrentWeapon;
	CurrentWeapon = nullptr;
	if (IsValid(LastWeapon))
	{
		LastWeapon->DetachFromOwningPawn();
	}
	CurrentReserveAmmo = 0;

	TArray<AWeapon*> WeaponsToDestroy = MoveTemp(Inventory);
	Inventory.Reset();
	ReserveAmmo.Reset();

	for (AWeapon* Weapon : WeaponsToDestroy)
	{
		if (IsValid(Weapon))
		{
			Weapon->Destroy();
		}
	}

	if (AActor* OwningActor = GetOwner(); IsValid(OwningActor) && OwningActor->HasAuthority())
	{
		OwningActor->ForceNetUpdate();
	}
}

void UCombatComponent::InitializeWeaponWidgets() const
{
	if (IsValid(CurrentWeapon))
	{
		OnReticleChanged.Broadcast(CurrentWeapon->GetReticleDynamicMaterialInstance(), CurrentWeapon->ReticleParams, bHitPlayer);
		OnAmmoCounterChanged.Broadcast(CurrentWeapon->GetAmmoCounterDynamicMaterialInstance(), CurrentWeapon->Ammo, CurrentWeapon->GetEffectiveMagCapacity());
	}
}



void UCombatComponent::OnRep_CurrentWeapon(AWeapon* LastWeapon)
{
	SetCurrentWeapon(CurrentWeapon, LastWeapon);

	if (!IsValid(CurrentWeapon)) return;
	if (IsValid(GetOwner()) && GetOwner()->Implements<UPlayerInterface>())
	{
		IPlayerInterface::Execute_WeaponReplicated(GetOwner());
	}
	InitializeWeaponWidgets();
}



AWeapon* UCombatComponent::SpawnWeapon(TSubclassOf<AWeapon> WeaponClass) const
{
	AActor* OwningActor = GetOwner();
	if (!IsValid(OwningActor)) return nullptr;
	if (OwningActor->GetLocalRole() < ROLE_Authority) return nullptr;
	if (!IsValid(WeaponClass.Get()) || !IsValid(GetWorld())) return nullptr;
	
	FActorSpawnParameters SpawnInfo;
	SpawnInfo.Instigator = Cast<APawn>(OwningActor);
	SpawnInfo.Owner = OwningActor;
	SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	
	return GetWorld()->SpawnActor<AWeapon>(WeaponClass, SpawnInfo);
}

