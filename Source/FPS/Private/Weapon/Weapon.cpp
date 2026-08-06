// Copyright Druid Mechanics


#include "Weapon/Weapon.h"

#include "KismetTraceUtils.h"
#include "Combat/CombatComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/AttachmentData.h"
#include "FPS/FPS.h"
#include "GameFramework/Pawn.h"
#include "Interfaces/PlayerInterface.h"
#include "Kismet/KismetMathLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
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

void AWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Unconditional, not COND_OwnerOnly. Every machine needs this: the owner predicts spread, mag size and
	// reload timing from it, and a simulated proxy derives the observed weapon's kick from it too. Unlike
	// bAiming there is nothing for COND_SkipOwner to save either, because equipping is never client-predicted
	// - the owner learns what it is carrying from here.
	DOREPLIFETIME(AWeapon, Attachments);
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

	// AController, not APlayerController. This whole function used to sit inside an APlayerController cast,
	// which meant an AI-controlled pawn skipped the trace entirely and returned an untouched OutHit - the bot
	// spent ammo, played its montage and hit nothing, with nothing logged. AController::GetActorEyesViewPoint
	// resolves to the pawn's own view point, which is the correct aim origin for a bot; APlayerController's
	// override is what gives a human the camera-accurate one, and that path is unchanged for players.
	if (AController* ShooterController = InstigatingPawn->GetController(); IsValid(ShooterController))
	{
		FVector EyesWorldLocation;
		FRotator EyesWorldRotation;
		ShooterController->GetActorEyesViewPoint(EyesWorldLocation, EyesWorldRotation);
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
		Ammo = FMath::Clamp(Ammo -1, 0, GetEffectiveMagCapacity());
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
	Ammo = FMath::Clamp(Ammo -1, 0, GetEffectiveMagCapacity());
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
		Ammo = FMath::Clamp(AuthAmmo - Sequence, 0, GetEffectiveMagCapacity());
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

	const float HeatThisShot = FMath::Max(RecoilParams.HeatPerShot, 0.f) * FMath::Max(EffectiveStats.RecoilHeatMultiplier, 0.f);

	SpreadHeat = FMath::Clamp(SpreadHeat + HeatThisShot, 0.f, 1.f);
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

	// The two attachment spread terms are stance-exclusive on purpose: a laser sight is the hip-fire answer and
	// an optic is the ranged answer, so neither can be stacked into a weapon that is accurate in both stances.
	// That split is the entire "better hip-fire accuracy vs better ranged accuracy" trade-off from the GDD.
	if (bIsAiming)
	{
		Spread *= FMath::Max(RecoilParams.AimSpreadMultiplier, 0.f);
		Spread *= FMath::Max(EffectiveStats.AimSpreadMultiplier, 0.f);
	}
	else
	{
		Spread *= FMath::Max(EffectiveStats.HipFireSpreadMultiplier, 0.f);
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

	return Punch * FMath::Max(EffectiveStats.ViewPunchMultiplier, 0.f);
}

float AWeapon::GetViewPunchYawRange(bool bIsAiming) const
{
	// Scaled by heat so the first round of a burst goes exactly where it is aimed and only sustained fire
	// wanders sideways. A cold weapon with any yaw at all makes single-shot weapons feel broken.
	return FMath::Max(RecoilParams.ViewPunchYawRange, 0.f)
		* (bIsAiming ? FMath::Max(RecoilParams.AimViewPunchMultiplier, 0.f) : 1.f)
		* FMath::Max(EffectiveStats.ViewPunchMultiplier, 0.f)
		* FMath::Clamp(SpreadHeat, 0.f, 1.f);
}

void AWeapon::ApplyWeaponKick(bool bIsAiming)
{
	CacheKickRestTransforms();

	// Folded into the same scalar as the aim multiplier rather than applied afterwards, because the accumulation
	// ceiling below is derived from these same numbers - scaling the kick without scaling its ceiling would let
	// a foregrip'd weapon stack more shots' worth of a smaller kick and end up in the same held-back pose.
	const float AimScale = (bIsAiming ? FMath::Max(RecoilParams.AimWeaponKickMultiplier, 0.f) : 1.f)
		* FMath::Max(EffectiveStats.WeaponKickMultiplier, 0.f);

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

	// Authority only, and it fills the replicated array rather than a separate one, so a client never applies
	// these itself - it receives them like any other equipped attachment and cannot disagree about them.
	if (HasAuthority())
	{
		for (UAttachmentData* Definition : DefaultAttachments)
		{
			if (!IsValid(Definition)) continue;

			Auth_SetAttachment(Definition, Definition->BaseRarity);
		}
	}

	// Also on clients, where initial replication of Attachments can land before BeginPlay and therefore before
	// OnRep_Attachments is of any use.
	RecalculateEffectiveStats();
	RefreshAttachmentVisuals();
}

bool AWeapon::SupportsSlot(EAttachmentSlot Slot) const
{
	if (Slot == EAttachmentSlot::None) return false;

	return SupportedSlots.Contains(Slot);
}

bool AWeapon::CanEquipAttachment(const UAttachmentData* Definition) const
{
	if (!IsValid(Definition)) return false;
	if (!SupportsSlot(Definition->Slot)) return false;
	if (!Definition->IsCompatibleWithWeaponType(WeaponType)) return false;

	return true;
}

FEquippedAttachment AWeapon::GetAttachmentInSlot(EAttachmentSlot Slot) const
{
	for (const FEquippedAttachment& Attachment : Attachments)
	{
		if (Attachment.Slot == Slot)
		{
			return Attachment;
		}
	}

	return FEquippedAttachment();
}

bool AWeapon::Auth_SetAttachment(UAttachmentData* Definition, EAttachmentRarity InstanceRarity)
{
	if (!HasAuthority()) return false;
	if (!CanEquipAttachment(Definition)) return false;

	FEquippedAttachment NewAttachment;
	NewAttachment.Slot = Definition->Slot;
	NewAttachment.Definition = Definition;
	// Clamped up, never down. An instance below its own definition's base rarity would give
	// GetValueForRaritySteps a negative step count, and a "worse than authored" attachment is not a state the
	// design has - the reroll only ever rolls up.
	NewAttachment.Rarity = FMath::Max(InstanceRarity, Definition->BaseRarity);

	// One per slot, so this replaces rather than appends. Index-based rather than remove-then-add so the
	// array order stays stable across an attachment swap and clients see a single changed entry.
	bool bReplaced = false;
	for (FEquippedAttachment& Existing : Attachments)
	{
		if (Existing.Slot != NewAttachment.Slot) continue;

		Existing = NewAttachment;
		bReplaced = true;
		break;
	}
	if (!bReplaced)
	{
		Attachments.Add(NewAttachment);
	}

	RecalculateEffectiveStats();
	ClampAmmoToEffectiveCapacity();
	RefreshAttachmentVisuals();

	// The weapon replicates at 5Hz to keep idle inventory cheap, which is fine for state nobody is waiting on
	// but not for this: the loadout screen and the post-match steal both change attachments at a moment the
	// player is looking straight at the result.
	ForceNetUpdate();

	return true;
}

bool AWeapon::Auth_ClearAttachmentSlot(EAttachmentSlot Slot)
{
	if (!HasAuthority()) return false;

	const int32 RemovedCount = Attachments.RemoveAll([Slot](const FEquippedAttachment& Attachment)
	{
		return Attachment.Slot == Slot;
	});
	if (RemovedCount == 0) return false;

	RecalculateEffectiveStats();
	ClampAmmoToEffectiveCapacity();
	RefreshAttachmentVisuals();
	ForceNetUpdate();

	return true;
}

int32 AWeapon::GetEffectiveMagCapacity() const
{
	// Floored at 1 rather than at 0. A capacity of 0 makes Ammo permanently 0, which reads as a weapon that
	// silently refuses to fire for the rest of the match with nothing on screen to explain it.
	return FMath::Max(MagCapacity + EffectiveStats.MagCapacityBonus, 1);
}

float AWeapon::GetEffectiveAimFieldOfView() const
{
	// Clamped to a sane camera range: a modifier stack that reaches 0 or goes negative would otherwise hand
	// SetFieldOfView a value that renders nothing at all.
	return FMath::Clamp(AimFieldOfView * FMath::Max(EffectiveStats.AimFieldOfViewMultiplier, 0.f), 5.f, 170.f);
}

float AWeapon::GetEffectiveAimLookSensitivityScale() const
{
	return FMath::Max(AimLookSensitivityScale * FMath::Max(EffectiveStats.AimLookSensitivityMultiplier, 0.f), 0.f);
}

float AWeapon::GetReloadPlayRate() const
{
	// Floored well above 0: a play rate of 0 freezes the montage, and the reload notify that refills the mag
	// would then never fire, leaving the weapon empty with no way to recover short of a weapon swap.
	return FMath::Max(EffectiveStats.ReloadSpeedMultiplier, 0.1f);
}

float AWeapon::GetEquipPlayRate() const
{
	// Same trap as the reload rate: the equip montage's blend-out is what hands control back, so a rate of 0
	// would leave the weapon stuck in EWeaponStatus::Cycling and unable to fire.
	return FMath::Max(EffectiveStats.EquipSpeedMultiplier, 0.1f);
}

void AWeapon::OnRep_Attachments()
{
	RecalculateEffectiveStats();
	ClampAmmoToEffectiveCapacity();
	RefreshAttachmentVisuals();

	// The HUD delegates live on UCombatComponent, and the ammo counter's rounds-max has just changed. Reached
	// through the component's own static finder rather than by casting the owner, matching how the rest of the
	// combat code avoids knowing the pawn's class.
	if (UCombatComponent* Combat = UCombatComponent::FindCombatComponent(GetOwner()); IsValid(Combat))
	{
		Combat->NotifyAttachmentsChanged(this);
	}
}

void AWeapon::RecalculateEffectiveStats()
{
	EffectiveStats.Reset();

	for (const FEquippedAttachment& Attachment : Attachments)
	{
		// A replicated reference to a data asset resolves by path, so on a client it can in principle still be
		// null on the frame the array arrives. Skipped rather than treated as an error: BeginPlay recalculates
		// as well, and the hard references in DefaultAttachments keep these assets resident in practice.
		if (!IsValid(Attachment.Definition)) continue;

		const int32 RaritySteps = static_cast<int32>(Attachment.Rarity) - static_cast<int32>(Attachment.Definition->BaseRarity);

		for (const FWeaponStatModifier& Modifier : Attachment.Definition->Modifiers)
		{
			EffectiveStats.ApplyModifier(Modifier, RaritySteps);
		}
	}
}

void AWeapon::ClampAmmoToEffectiveCapacity()
{
	Ammo = FMath::Min(Ammo, GetEffectiveMagCapacity());
}

void AWeapon::RefreshAttachmentVisuals_Implementation()
{
	// Intentionally empty - see the declaration. Override in the weapon Blueprint alongside FireEffects.
}

void AWeapon::SetMeshVisibilities(APawn* OwningPawn) const
{
	if (!IsValid(OwningPawn)) return;

	// Player-viewed, not merely locally controlled. A bot is locally controlled on the authority, so the old
	// test showed its Mesh1P - parented under a bOnlyOwnerSee arms mesh no human owns - and hid its Mesh3P.
	// The result on a listen-server host was a bot walking around visibly holding nothing.
	const bool bFirstPerson = OwningPawn->Implements<UPlayerInterface>()
		? IPlayerInterface::Execute_IsFirstPersonViewer(OwningPawn)
		: OwningPawn->IsLocallyControlled();

	if (bFirstPerson)
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

