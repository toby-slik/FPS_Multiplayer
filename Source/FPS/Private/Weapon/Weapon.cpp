// Copyright Druid Mechanics


#include "Weapon/Weapon.h"

#include "KismetTraceUtils.h"
#include "Components/SkeletalMeshComponent.h"
#include "FPS/FPS.h"
#include "GameFramework/Pawn.h"
#include "Interfaces/PlayerInterface.h"
#include "Kismet/KismetMathLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Tags/ShooterGamePlayTags.h"


AWeapon::AWeapon()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bNetUseOwnerRelevancy = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(5.f);
	SetMinNetUpdateFrequency(2.f);
	
	Mesh1P = CreateDefaultSubobject<USkeletalMeshComponent>("Mesh1P");
	Mesh1P->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	Mesh1P->bReceivesDecals = false;
	Mesh1P->CastShadow = false;
	Mesh1P->SetHiddenInGame(true);
	SetRootComponent(Mesh1P);
	
	Mesh3P = CreateDefaultSubobject<USkeletalMeshComponent>("Mesh3P");
	Mesh3P->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	Mesh3P->bReceivesDecals = false;
	Mesh3P->CastShadow = true;
	Mesh3P->SetupAttachment(Mesh1P);
	Mesh3P->SetHiddenInGame(true);

	AimFieldOfView = 65.0f;
	AimLookSensitivityScale = 1.0f;
	TraceRadius = 5.f;
	FireTime = 0.1f;
	MagCapacity = 10;
	Ammo = 30;
	StartingCarriedAmmo = 10;
	Sequence = 0;
	WeaponStatus = EWeaponStatus::Unequipped;
	Damage = 15.f;
	HeadshotDamageMultiplier = 2.f;

	bUseRecoilTypePreset = true;
	SpreadHeat = 0.f;
	LastHeatShotTime = -1.0e6f;
	LastHeatDecayTime = -1.0e6f;
	KickLocationOffset = FVector::ZeroVector;
	KickRotationOffset = FRotator::ZeroRotator;
	bKickRestTransformsCached = false;
}

USkeletalMeshComponent* AWeapon::GetMesh1P() const
{
	return Mesh1P;
}

USkeletalMeshComponent* AWeapon::GetMesh3P() const
{
	return Mesh3P;
}

UMaterialInstanceDynamic* AWeapon::GetReticleDynamicMaterialInstance()
{
	if (!IsValid(DynMatInst_Reticle))
	{
		DynMatInst_Reticle = UMaterialInstanceDynamic::Create(ReticleMaterial, this);
	}
	return DynMatInst_Reticle;
}

UMaterialInstanceDynamic* AWeapon::GetAmmoCounterDynamicMaterialInstance()
{
	if (!IsValid(DynMatInst_AmmoCounter))
	{
		DynMatInst_AmmoCounter = UMaterialInstanceDynamic::Create(AmmoCounterMaterial, this);
	}
	return DynMatInst_AmmoCounter;
}

void AWeapon::AttachToOwningPawn(APawn* Pawn) const
{
	if (!IsValid(Pawn) || !Pawn->Implements<UPlayerInterface>() || !IsValid(Mesh1P) || !IsValid(Mesh3P)) return;
	
	SetMeshVisibilities(Pawn);
	
	const FName AttachPoint = IPlayerInterface::Execute_GetWeaponAttachPoint(Pawn, WeaponType);
	USkeletalMeshComponent* PawnMesh1P = IPlayerInterface::Execute_GetMesh1P(Pawn);
	USkeletalMeshComponent* PawnMesh3P = IPlayerInterface::Execute_GetMesh3P(Pawn);
	if (!IsValid(PawnMesh1P) || !IsValid(PawnMesh3P)) return;
	
	Mesh1P->AttachToComponent(PawnMesh1P, FAttachmentTransformRules::KeepRelativeTransform, AttachPoint);
	Mesh3P->AttachToComponent(PawnMesh3P, FAttachmentTransformRules::KeepRelativeTransform, AttachPoint);
}

void AWeapon::DetachFromOwningPawn()
{
	// Cleared before the detach, not after. Both the detach and the later re-attach keep relative
	// transforms, so stowing a weapon mid-kick would otherwise bake that kicked pose in as its rest pose.
	ResetRecoilState();

	Mesh1P->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
	Mesh1P->SetHiddenInGame(true);
	
	Mesh3P->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
	Mesh3P->SetHiddenInGame(true);
}

void AWeapon::WeaponTrace(FHitResult& OutHit, float TraceLength, float SpreadDegrees, uint16 SpreadSeed)
{
	FCollisionQueryParams QueryParams;
	QueryParams.bReturnPhysicalMaterial = true;
	QueryParams.AddIgnoredActor(GetOwner());
	
	FCollisionResponseParams ResponseParams;
	ResponseParams.CollisionResponse.SetAllChannels(ECR_Ignore);
	ResponseParams.CollisionResponse.SetResponse(ECC_Pawn, ECR_Block);
	ResponseParams.CollisionResponse.SetResponse(ECC_WorldStatic, ECR_Block);
	ResponseParams.CollisionResponse.SetResponse(ECC_WorldDynamic, ECR_Block);
	ResponseParams.CollisionResponse.SetResponse(ECC_PhysicsBody, ECR_Block);
	
	APawn* InstigatingPawn = GetInstigator();
	if (!IsValid(InstigatingPawn) || !IsValid(GetWorld())) return;

	if (APlayerController* PC = Cast<APlayerController>(InstigatingPawn->GetController()); IsValid(PC))
	{
		FVector EyesWorldLocation;
		FRotator EyesWorldRotation;
		PC->GetActorEyesViewPoint(EyesWorldLocation, EyesWorldRotation);
		FVector EyesWorldDirection = UKismetMathLibrary::GetForwardVector(EyesWorldRotation);

		if (SpreadDegrees > 0.f)
		{
			// Seeded rather than drawn from the global RNG so this function returns the same direction on
			// the firing client and on the authority - see the note on the declaration. The angle/radius
			// pair is sampled with a sqrt on the radius, which is what makes hits land uniformly across
			// the cone's area; sampling radius linearly would pile rounds up in the centre and make the
			// cone read much tighter than the number says it is.
			FRandomStream Stream(static_cast<int32>(SpreadSeed));
			const float Angle = Stream.FRandRange(0.f, 2.f * PI);
			const float Radius = FMath::Sqrt(Stream.FRand());

			const FRotationMatrix EyesBasis(EyesWorldRotation);
			const FVector Right = EyesBasis.GetUnitAxis(EAxis::Y);
			const FVector Up = EyesBasis.GetUnitAxis(EAxis::Z);

			// Applied as a tangent offset rather than by composing two rotators, so the cone stays circular
			// instead of being sheared by pitch/yaw order, and so it cannot gimbal near straight up or down.
			const float TangentScale = FMath::Tan(FMath::DegreesToRadians(SpreadDegrees));
			EyesWorldDirection = (EyesWorldDirection
				+ Right * (FMath::Cos(Angle) * Radius * TangentScale)
				+ Up * (FMath::Sin(Angle) * Radius * TangentScale)).GetSafeNormal();
		}

		const FVector Start = EyesWorldLocation;
		const FVector End = Start + EyesWorldDirection * TraceLength;

		const bool bHit = GetWorld()->SweepSingleByChannel(
			OutHit, 
			Start, 
			End, 
			FQuat::Identity, 
			FPSTraceChannels::ECC_Weapon, 
			FCollisionShape::MakeSphere(TraceRadius),
			QueryParams,
			ResponseParams);
		
		if (!bHit)
		{
			OutHit.ImpactPoint = End;
		}
	}
}

void AWeapon::Local_Fire(const FVector& ImpactPoint, const FVector& ImpactNormal,
	TEnumAsByte<EPhysicalSurface> ImpactSurfaceType, bool bIsFirstPerson)
{
	FireEffects(ImpactPoint, ImpactNormal, ImpactSurfaceType, bIsFirstPerson);

	APawn* InstigatingPawn = GetInstigator();
	if (IsValid(InstigatingPawn) && InstigatingPawn->IsLocallyControlled() && !HasAuthority())
	{
		Ammo = FMath::Clamp(Ammo -1, 0, MagCapacity);
		++Sequence;
	}
}

void AWeapon::Local_DryFire(bool bReloadStarted)
{
	APawn* InstigatingPawn = GetInstigator();
	if (!IsValid(InstigatingPawn)) return;
	if (!InstigatingPawn->IsLocallyControlled()) return;

	DryFireEffects(bReloadStarted);
}

void AWeapon::Auth_Fire()
{
	Ammo = FMath::Clamp(Ammo -1, 0, MagCapacity);
}

void AWeapon::Rep_Fire(int32 AuthAmmo)
{
	APawn* InstigatingPawn = GetInstigator();
	if (IsValid(InstigatingPawn) && InstigatingPawn->IsLocallyControlled())
	{
		// This shot is now acknowledged, so only the still-unacknowledged ones stay predicted away.
		// Clamped because a stale Sequence would otherwise hand back a negative mag (locks out firing)
		// or one above capacity.
		Sequence = FMath::Max(Sequence - 1, 0);
		Ammo = FMath::Clamp(AuthAmmo - Sequence, 0, MagCapacity);
	}
}

FRecoilParams AWeapon::GetRecoilPresetForWeaponType(const FGameplayTag& Type)
{
	FRecoilParams P;

	if (Type == ShooterTags::TAG_WeaponType_LeverRifle)
	{
		// The hand cannon. 100 damage on a 1.5s cycle, so its whole cost has to land in one shove: at that
		// cadence heat is fully shed between rounds by design, which means a climbing spray is not available
		// to price it with. Near-perfect from cold in exchange - it is a precision weapon - and the recovery
		// is slow and laboured, which is what sells mass far better than a larger, faster kick would.
		P.HeatPerShot = 0.5f;
		P.HeatDecayPerSecond = 1.6f;
		P.HeatDecayDelay = 0.25f;

		P.SpreadBaseDegrees = 0.06f;
		P.SpreadMaxDegrees = 3.f;
		P.AimSpreadMultiplier = 0.3f;

		P.ViewPunchPitchMin = 2.6f;
		P.ViewPunchPitchMax = 3.4f;
		P.ViewPunchYawRange = 0.5f;
		P.ViewPunchMaxAccumulatedPitch = 9.f;
		P.ViewPunchInterpSpeed = 18.f;
		P.ViewRecoveryFraction = 0.9f;
		P.ViewRecoveryDelay = 0.12f;
		P.ViewRecoverySpeed = 7.f;
		// ADS barely tames this one. A scope should not turn a hand cannon into a target rifle.
		P.AimViewPunchMultiplier = 0.8f;

		P.WeaponKickBackward = 7.5f;
		P.WeaponKickUpward = 2.f;
		P.WeaponKickPitch = 9.5f;
		P.WeaponKickYawRange = 1.6f;
		P.WeaponKickRollRange = 3.5f;
		// Barely stacks - there is never a second round in flight before this one has settled.
		P.WeaponKickMaxAccumulated = 1.6f;
		P.WeaponKickRecoverySpeed = 7.f;
		P.AimWeaponKickMultiplier = 0.7f;

		// Low frequency reads as heavy. The same amplitude at 30Hz reads as a rattle instead.
		P.CameraShakeAmplitude = 1.1f;
		P.CameraShakeFrequency = 18.f;
		P.CameraShakeDuration = 0.34f;
		P.AimCameraShakeMultiplier = 0.7f;

		return P;
	}

	if (Type == ShooterTags::TAG_WeaponType_AssultRifle)
	{
		// The spray weapon, and the only one where sustained fire is the primary way to use it - so it is
		// the one tuned around the heat curve rather than around a single shot. Individually the softest
		// kick of the four; held down, the most punishing.
		P.HeatPerShot = 0.16f;
		P.HeatDecayPerSecond = 1.f;
		// Must stay above FireTime or the gun cools between its own rounds and never climbs at all.
		P.HeatDecayDelay = 0.15f;

		P.SpreadBaseDegrees = 0.3f;
		P.SpreadMaxDegrees = 4.2f;
		P.AimSpreadMultiplier = 0.35f;

		P.ViewPunchPitchMin = 0.34f;
		P.ViewPunchPitchMax = 0.85f;
		P.ViewPunchYawRange = 0.4f;
		P.ViewPunchMaxAccumulatedPitch = 7.f;
		P.ViewPunchInterpSpeed = 26.f;
		P.ViewRecoveryFraction = 0.8f;
		P.ViewRecoveryDelay = 0.07f;
		P.ViewRecoverySpeed = 10.f;
		P.AimViewPunchMultiplier = 0.6f;

		P.WeaponKickBackward = 2.6f;
		P.WeaponKickUpward = 0.6f;
		P.WeaponKickPitch = 3.4f;
		P.WeaponKickYawRange = 0.9f;
		P.WeaponKickRollRange = 1.8f;
		P.WeaponKickMaxAccumulated = 2.5f;
		P.WeaponKickRecoverySpeed = 13.f;
		P.AimWeaponKickMultiplier = 0.5f;

		P.CameraShakeAmplitude = 0.4f;
		P.CameraShakeFrequency = 26.f;
		P.CameraShakeDuration = 0.16f;
		P.AimCameraShakeMultiplier = 0.45f;

		return P;
	}

	if (Type == ShooterTags::TAG_WeaponType_Pistol)
	{
		// Snappy and forgiving per shot, with the fastest recovery of the four so a controlled cadence stays
		// accurate. Paid for with the steepest heat gain: trigger-spamming a sidearm opens the cone faster
		// than anything else here, which is what keeps it from outclassing the rifles at range.
		P.HeatPerShot = 0.28f;
		P.HeatDecayPerSecond = 1.8f;
		P.HeatDecayDelay = 0.1f;

		P.SpreadBaseDegrees = 0.22f;
		P.SpreadMaxDegrees = 3.6f;
		P.AimSpreadMultiplier = 0.4f;

		P.ViewPunchPitchMin = 0.75f;
		P.ViewPunchPitchMax = 1.25f;
		P.ViewPunchYawRange = 0.5f;
		P.ViewPunchMaxAccumulatedPitch = 5.f;
		P.ViewPunchInterpSpeed = 30.f;
		P.ViewRecoveryFraction = 0.9f;
		P.ViewRecoveryDelay = 0.05f;
		P.ViewRecoverySpeed = 14.f;
		P.AimViewPunchMultiplier = 0.6f;

		// Light frame, so more muzzle flip than travel - a pistol snaps up rather than driving back.
		P.WeaponKickBackward = 2.2f;
		P.WeaponKickUpward = 0.9f;
		P.WeaponKickPitch = 5.5f;
		P.WeaponKickYawRange = 1.4f;
		P.WeaponKickRollRange = 2.6f;
		P.WeaponKickMaxAccumulated = 2.2f;
		P.WeaponKickRecoverySpeed = 16.f;
		P.AimWeaponKickMultiplier = 0.55f;

		P.CameraShakeAmplitude = 0.45f;
		P.CameraShakeFrequency = 30.f;
		P.CameraShakeDuration = 0.14f;
		P.AimCameraShakeMultiplier = 0.5f;

		return P;
	}

	if (Type == ShooterTags::TAG_WeaponType_Rifle)
	{
		// The middle of the set: a real single-shot kick, but recoverable enough to keep a rhythm.
		P.HeatPerShot = 0.22f;
		P.HeatDecayPerSecond = 1.2f;
		P.HeatDecayDelay = 0.14f;

		P.SpreadBaseDegrees = 0.18f;
		P.SpreadMaxDegrees = 3.2f;
		P.AimSpreadMultiplier = 0.3f;

		P.ViewPunchPitchMin = 0.85f;
		P.ViewPunchPitchMax = 1.5f;
		P.ViewPunchYawRange = 0.3f;
		P.ViewPunchMaxAccumulatedPitch = 7.f;
		P.ViewPunchInterpSpeed = 24.f;
		P.ViewRecoveryFraction = 0.85f;
		P.ViewRecoveryDelay = 0.08f;
		P.ViewRecoverySpeed = 9.5f;
		P.AimViewPunchMultiplier = 0.6f;

		P.WeaponKickBackward = 4.f;
		P.WeaponKickUpward = 1.f;
		P.WeaponKickPitch = 5.2f;
		P.WeaponKickYawRange = 1.f;
		P.WeaponKickRollRange = 2.2f;
		P.WeaponKickMaxAccumulated = 2.f;
		P.WeaponKickRecoverySpeed = 10.f;
		P.AimWeaponKickMultiplier = 0.55f;

		P.CameraShakeAmplitude = 0.65f;
		P.CameraShakeFrequency = 22.f;
		P.CameraShakeDuration = 0.22f;
		P.AimCameraShakeMultiplier = 0.5f;

		return P;
	}

	// Unknown or None: the struct's own defaults, which are a mid-weight automatic. Left deliberately
	// playable rather than zeroed, so a new weapon tag has usable recoil before anyone tunes it.
	return P;
}

float AWeapon::AdvanceAndGetHeat(float CurrentTime)
{
	// First call on this weapon: adopt CurrentTime rather than integrating from the sentinel, which would
	// otherwise decay a lifetime's worth of heat in one step. Harmless while heat is 0, but it also keeps
	// the delay below meaningful on the very first shot.
	if (LastHeatDecayTime < 0.f)
	{
		LastHeatDecayTime = CurrentTime;
		return SpreadHeat;
	}

	if (SpreadHeat <= 0.f)
	{
		SpreadHeat = 0.f;
		LastHeatDecayTime = CurrentTime;
		return 0.f;
	}

	// Decay only counts from the end of the grace period, so the interval is clamped to whatever part of
	// it sits after LastHeatShotTime + delay. Rewinding LastHeatDecayTime to that boundary is what stops
	// a long grace period from being silently spent as decay on the first frame past it.
	const float DecayStartsAt = LastHeatShotTime + FMath::Max(RecoilParams.HeatDecayDelay, 0.f);
	const float IntervalStart = FMath::Max(LastHeatDecayTime, DecayStartsAt);
	LastHeatDecayTime = CurrentTime;

	if (CurrentTime > IntervalStart)
	{
		SpreadHeat = FMath::Max(0.f, SpreadHeat - RecoilParams.HeatDecayPerSecond * (CurrentTime - IntervalStart));
	}

	return SpreadHeat;
}

void AWeapon::AddRecoilHeat(float CurrentTime)
{
	// Decay is settled before the shot is added so a burst can never bank the cooling it has not had yet.
	AdvanceAndGetHeat(CurrentTime);

	SpreadHeat = FMath::Clamp(SpreadHeat + FMath::Max(RecoilParams.HeatPerShot, 0.f), 0.f, 1.f);
	LastHeatShotTime = CurrentTime;
	LastHeatDecayTime = CurrentTime;
}

void AWeapon::ResetRecoilState()
{
	SpreadHeat = 0.f;
	LastHeatShotTime = -1.0e6f;
	LastHeatDecayTime = -1.0e6f;
	KickLocationOffset = FVector::ZeroVector;
	KickRotationOffset = FRotator::ZeroRotator;
	ApplyKickToMeshes();
}

float AWeapon::GetSpreadDegrees(bool bIsAiming, bool bIsMoving, bool bIsAirborne) const
{
	float Spread = FMath::Lerp(
		FMath::Max(RecoilParams.SpreadBaseDegrees, 0.f),
		FMath::Max(RecoilParams.SpreadMaxDegrees, 0.f),
		FMath::Clamp(SpreadHeat, 0.f, 1.f));

	if (bIsAiming)
	{
		Spread *= FMath::Max(RecoilParams.AimSpreadMultiplier, 0.f);
	}

	// Airborne supersedes moving rather than compounding with it. A jump is always "moving" as well, and
	// multiplying both would put the real cone well past either number the designer authored.
	if (bIsAirborne)
	{
		Spread *= FMath::Max(RecoilParams.AirborneSpreadMultiplier, 0.f);
	}
	else if (bIsMoving)
	{
		Spread *= FMath::Max(RecoilParams.MovingSpreadMultiplier, 0.f);
	}

	return FMath::Max(Spread, 0.f);
}

float AWeapon::GetViewPunchPitch(bool bIsAiming) const
{
	float Punch = FMath::Lerp(
		RecoilParams.ViewPunchPitchMin,
		RecoilParams.ViewPunchPitchMax,
		FMath::Clamp(SpreadHeat, 0.f, 1.f));

	if (bIsAiming)
	{
		Punch *= FMath::Max(RecoilParams.AimViewPunchMultiplier, 0.f);
	}

	return Punch;
}

void AWeapon::ApplyWeaponKick(bool bIsAiming)
{
	CacheKickRestTransforms();

	const float AimScale = bIsAiming ? FMath::Max(RecoilParams.AimWeaponKickMultiplier, 0.f) : 1.f;

	// Drawn from the global RNG on purpose: unlike spread, the visible kick is never re-derived on another
	// machine, so there is nothing to keep in sync and a shared seed would only make repeat shots identical.
	const FVector KickLocation(
		-RecoilParams.WeaponKickBackward * AimScale,
		0.f,
		RecoilParams.WeaponKickUpward * AimScale);

	const FRotator KickRotation(
		RecoilParams.WeaponKickPitch * AimScale,
		FMath::FRandRange(-RecoilParams.WeaponKickYawRange, RecoilParams.WeaponKickYawRange) * AimScale,
		FMath::FRandRange(-RecoilParams.WeaponKickRollRange, RecoilParams.WeaponKickRollRange) * AimScale);

	// Accumulated toward a ceiling instead of being reset per shot. This is the whole reason the kick is
	// code-driven: sustained fire stacks into a held-back pose and settles from there, where re-playing a
	// montage every round truncates the previous shot's recovery and reads as a stutter at high fire rates.
	const float Ceiling = FMath::Max(RecoilParams.WeaponKickMaxAccumulated, 1.f);

	KickLocationOffset += KickLocation;
	KickRotationOffset += KickRotation;

	const FVector MaxLocation = KickLocation.GetAbs() * Ceiling;
	KickLocationOffset.X = FMath::Clamp(KickLocationOffset.X, -MaxLocation.X, MaxLocation.X);
	KickLocationOffset.Y = FMath::Clamp(KickLocationOffset.Y, -MaxLocation.Y, MaxLocation.Y);
	KickLocationOffset.Z = FMath::Clamp(KickLocationOffset.Z, -MaxLocation.Z, MaxLocation.Z);

	const float MaxPitch = FMath::Abs(RecoilParams.WeaponKickPitch * AimScale) * Ceiling;
	const float MaxYaw = FMath::Abs(RecoilParams.WeaponKickYawRange * AimScale) * Ceiling;
	const float MaxRoll = FMath::Abs(RecoilParams.WeaponKickRollRange * AimScale) * Ceiling;
	KickRotationOffset.Pitch = FMath::Clamp(KickRotationOffset.Pitch, -MaxPitch, MaxPitch);
	KickRotationOffset.Yaw = FMath::Clamp(KickRotationOffset.Yaw, -MaxYaw, MaxYaw);
	KickRotationOffset.Roll = FMath::Clamp(KickRotationOffset.Roll, -MaxRoll, MaxRoll);

	ApplyKickToMeshes();
}

void AWeapon::UpdateWeaponKick(float DeltaTime)
{
	if (KickLocationOffset.IsNearlyZero() && KickRotationOffset.IsNearlyZero()) return;

	CacheKickRestTransforms();

	const float Speed = FMath::Max(RecoilParams.WeaponKickRecoverySpeed, 0.1f);
	KickLocationOffset = FMath::VInterpTo(KickLocationOffset, FVector::ZeroVector, DeltaTime, Speed);
	KickRotationOffset = FMath::RInterpTo(KickRotationOffset, FRotator::ZeroRotator, DeltaTime, Speed);

	// Snapped once inside the epsilon so the early-out above can actually be reached; an exponential interp
	// never arrives on its own, which would leave both meshes being written every frame for the whole match.
	if (KickLocationOffset.IsNearlyZero(0.01f) && KickRotationOffset.IsNearlyZero(0.01f))
	{
		KickLocationOffset = FVector::ZeroVector;
		KickRotationOffset = FRotator::ZeroRotator;
	}

	ApplyKickToMeshes();
}

void AWeapon::CacheKickRestTransforms()
{
	if (bKickRestTransformsCached) return;

	if (IsValid(Mesh1P))
	{
		Mesh1PRestTransform = Mesh1P->GetRelativeTransform();
	}
	if (IsValid(Mesh3P))
	{
		Mesh3PRestTransform = Mesh3P->GetRelativeTransform();
	}
	bKickRestTransformsCached = true;
}

void AWeapon::ApplyKickToMeshes() const
{
	if (!bKickRestTransformsCached) return;

	// Composed onto the cached rest pose rather than accumulated onto the live transform, so drift is
	// impossible however many times this runs and whatever order it runs in.
	if (IsValid(Mesh1P))
	{
		Mesh1P->SetRelativeLocation(Mesh1PRestTransform.GetLocation() + Mesh1PRestTransform.GetRotation().RotateVector(KickLocationOffset));
		Mesh1P->SetRelativeRotation(Mesh1PRestTransform.GetRotation() * KickRotationOffset.Quaternion());
	}
	if (IsValid(Mesh3P))
	{
		Mesh3P->SetRelativeLocation(Mesh3PRestTransform.GetLocation() + Mesh3PRestTransform.GetRotation().RotateVector(KickLocationOffset));
		Mesh3P->SetRelativeRotation(Mesh3PRestTransform.GetRotation() * KickRotationOffset.Quaternion());
	}
}

void AWeapon::BeginPlay()
{
	Super::BeginPlay();

	// Applied on every machine rather than replicated: WeaponType is an authored default, so each machine
	// resolves the same preset independently. The authority and the firing client therefore agree on the
	// cone without a single byte crossing the network.
	if (bUseRecoilTypePreset)
	{
		RecoilParams = GetRecoilPresetForWeaponType(WeaponType);
	}

	// Before anything can kick, and before the first AttachToOwningPawn - which preserves relative
	// transforms, so what is captured here stays the correct rest pose for the weapon's whole life.
	CacheKickRestTransforms();
}

void AWeapon::SetMeshVisibilities(APawn* OwningPawn) const
{
	if (OwningPawn->IsLocallyControlled())
	{
		Mesh1P->SetHiddenInGame(false);
		Mesh3P->SetHiddenInGame(true);
	}
	else
	{
		Mesh1P->SetHiddenInGame(true);
		Mesh3P->SetHiddenInGame(false);
	}
}

