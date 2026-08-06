// Copyright Druid Mechanics


#include "AI/ShooterAIAimComponent.h"

#include "AI/ShooterAIController.h"
#include "Character/ShooterCharacter.h"
#include "Combat/CombatComponent.h"
#include "Engine/World.h"
#include "Weapon/Weapon.h"

UShooterAIAimComponent::UShooterAIAimComponent()
{
	// Ticked by hand from AShooterAIController::Tick so the within-frame order is guaranteed.
	PrimaryComponentTick.bCanEverTick = false;

	AimDownSightsMinRange = 1500.f;
	ViewPunchScale = 1.f;
	ViewPunchRecoveryDegreesPerSecond = 9.f;

	TrackedTime = 0.f;
	ErrorRefreshTimer = 0.f;
	CurrentErrorDegrees = FVector2D::ZeroVector;
	TargetErrorDegrees = FVector2D::ZeroVector;
	AccumulatedPunchPitch = 0.f;
	bTriggerHeld = false;
	bResting = false;
	BurstTimer = 0.f;
	AmmoAtLastCheck = -1;
	bAimingDownSights = false;
	bYawLocked = false;
	LockedTravelDirection = FVector::ZeroVector;
	DebugAimPoint = FVector::ZeroVector;
	bBurstActive = false;
}

AShooterAIController* UShooterAIAimComponent::GetAIController() const
{
	return Cast<AShooterAIController>(GetOwner());
}

AShooterCharacter* UShooterAIAimComponent::GetShooterPawn() const
{
	const AShooterAIController* AI = GetAIController();
	return IsValid(AI) ? AI->GetShooterPawn() : nullptr;
}

UCombatComponent* UShooterAIAimComponent::GetCombat() const
{
	const AShooterAIController* AI = GetAIController();
	return IsValid(AI) ? AI->GetCombat() : nullptr;
}

AWeapon* UShooterAIAimComponent::GetWeapon() const
{
	const UCombatComponent* Combat = GetCombat();
	return IsValid(Combat) ? Combat->CurrentWeapon : nullptr;
}

void UShooterAIAimComponent::TickAim(float DeltaTime)
{
	const AShooterAIController* AI = GetAIController();
	if (!IsValid(AI) || !IsValid(GetShooterPawn())) return;

	// Recoil the bot has not yet corrected. Bled off at a finite rate on purpose: this is the bot's recoil
	// control, and instant recovery would mean it has none.
	AccumulatedPunchPitch = FMath::Max(0.f, AccumulatedPunchPitch - ViewPunchRecoveryDegreesPerSecond * DeltaTime);

	UpdateAimError(DeltaTime);

	FVector AimPoint;
	const bool bHaveAimPoint = ComputeAimPoint(AimPoint);
	DebugAimPoint = bHaveAimPoint ? AimPoint : FVector::ZeroVector;

	UpdateRotation(DeltaTime, AimPoint, bHaveAimPoint);
	UpdateAimDownSights();
	UpdateTrigger(DeltaTime);
}

bool UShooterAIAimComponent::ComputeAimPoint(FVector& OutAimPoint)
{
	const AShooterAIController* AI = GetAIController();
	if (!IsValid(AI)) return false;

	const APawn* Target = AI->GetTargetPawn();
	if (!IsValid(Target)) return false;

	const FShooterBotDifficulty& D = AI->GetDifficulty();

	if (AI->HasLineOfSight())
	{
		const FVector TargetEye = Target->GetPawnViewLocation();
		const FVector TargetVelocity = Target->GetVelocity();

		// The bot aims at where the target *was*, and TargetLeadFraction is how well it compensates for its
		// own staleness. Fraction 0 leaves the full lag in, so a strafing player is chronically under-led;
		// 1 cancels it exactly; above 1 over-leads and the bot shoots in front of the player. Expressing both
		// tracking lag and leading skill through one term is what keeps them from fighting each other - two
		// independent offsets would let a bot with poor leading still track perfectly, which is not a thing
		// a human hand does.
		const float EffectiveLag = D.AimTrackingLag * (1.f - D.TargetLeadFraction);
		OutAimPoint = TargetEye - TargetVelocity * EffectiveLag;
		return true;
	}

	// No sight, but the target was seen recently enough for the remembered position to still mean something -
	// keep the gun pointed at it rather than letting the aim drift. The trigger logic refuses to fire without
	// line of sight, so this cannot become shooting through a wall.
	//
	// Gated on TargetMemoryTime, not merely on being in Hunt: the hunt is now open-ended (a 1v1 bot never
	// gives up), so without the freshness test a long search would leave the bot staring at a position the
	// player left a minute ago while it walked past them.
	if (AI->GetBotState() == EShooterBotState::Hunt && AI->GetTimeSinceLineOfSight() <= D.TargetMemoryTime)
	{
		OutAimPoint = AI->GetLastKnownTargetLocation();
		return true;
	}

	// Nothing worth aiming at. Look where it is going, so a searching bot faces along its route instead of
	// walking sideways with a frozen aim - and so it sees the player when it rounds a corner.
	if (const APawn* Bot = AI->GetShooterPawn())
	{
		const FVector Travel = Bot->GetVelocity().GetSafeNormal2D();
		if (!Travel.IsNearlyZero())
		{
			OutAimPoint = AI->GetEyeLocation() + Travel * 1000.f;
			return true;
		}
	}

	return false;
}

void UShooterAIAimComponent::UpdateAimError(float DeltaTime)
{
	const AShooterAIController* AI = GetAIController();
	if (!IsValid(AI)) return;

	const FShooterBotDifficulty& D = AI->GetDifficulty();

	// Tracking time only accumulates while the bot actually has the target. Losing sight resets it, so
	// peeking in and out of cover costs the bot its settled aim every time - which is exactly why peeking
	// beats standing in the open, and it falls out of this one line rather than needing its own rule.
	if (AI->HasAcquiredTarget())
	{
		TrackedTime += DeltaTime;
	}
	else
	{
		TrackedTime = 0.f;
	}

	const float SettleAlpha = FMath::Clamp(TrackedTime / FMath::Max(D.AimSettleTime, KINDA_SMALL_NUMBER), 0.f, 1.f);
	const float ErrorMagnitude = FMath::Lerp(D.AimErrorInitialDegrees, D.AimErrorSettledDegrees, SettleAlpha);

	ErrorRefreshTimer -= DeltaTime;
	if (ErrorRefreshTimer <= 0.f)
	{
		ErrorRefreshTimer = D.AimErrorRefreshInterval;

		// Uniform over the disc rather than over the radius - a linear radius piles the error up near the
		// centre and the bot reads as far more accurate than the number says it is. Same reasoning, and the
		// same sqrt, as the bullet-spread cone in AWeapon::WeaponTrace.
		const float Angle = FMath::FRandRange(0.f, 2.f * PI);
		const float Radius = FMath::Sqrt(FMath::FRand()) * ErrorMagnitude;
		TargetErrorDegrees = FVector2D(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius);
	}

	// Interped so the error slides rather than snapping. Speed is tied to the refresh interval so the offset
	// arrives at roughly the moment a new one is chosen - that reads as a hand micro-correcting, where a snap
	// reads as a teleporting crosshair.
	const float InterpSpeed = 2.f / FMath::Max(D.AimErrorRefreshInterval, KINDA_SMALL_NUMBER);
	CurrentErrorDegrees = FMath::Vector2DInterpTo(CurrentErrorDegrees, TargetErrorDegrees, DeltaTime, InterpSpeed);
}

void UShooterAIAimComponent::UpdateRotation(float DeltaTime, const FVector& AimPoint, bool bHaveAimPoint)
{
	AShooterAIController* AI = GetAIController();
	if (!IsValid(AI)) return;

	const FShooterBotDifficulty& D = AI->GetDifficulty();
	const FRotator CurrentRotation = AI->GetControlRotation();

	FRotator DesiredRotation = CurrentRotation;

	if (bHaveAimPoint)
	{
		const FVector Eye = AI->GetEyeLocation();
		const FVector ToAim = AimPoint - Eye;
		if (ToAim.IsNearlyZero()) return;

		DesiredRotation = ToAim.Rotation();
		DesiredRotation.Pitch += CurrentErrorDegrees.X + AccumulatedPunchPitch;
		DesiredRotation.Yaw += CurrentErrorDegrees.Y;
	}
	else if (!bYawLocked)
	{
		// Nothing to look at and no traversal claim on the yaw - leave the aim where it is rather than
		// snapping it to level, which would look like the bot forgetting itself every time it loses sight.
		return;
	}

	if (bYawLocked && !LockedTravelDirection.IsNearlyZero())
	{
		// The movement tech layer owns the yaw for the duration of a wall-run attempt. Pitch is left on the
		// target so the bot comes off the wall already looking roughly the right way - see the note on
		// SetYawLockToTravel for why this trade is acceptable.
		DesiredRotation.Yaw = LockedTravelDirection.Rotation().Yaw;
	}

	DesiredRotation.Pitch = FMath::Clamp(FRotator::NormalizeAxis(DesiredRotation.Pitch), -89.f, 89.f);
	DesiredRotation.Roll = 0.f;

	// Constant rate, not proportional: this is a hand on a mouse, and it has a top speed. A proportional
	// interp would make the bot snap most of the way instantly on a large angle, which is what reads as an
	// aimbot even when the final accuracy is poor.
	const FRotator NewRotation = FMath::RInterpConstantTo(CurrentRotation, DesiredRotation, DeltaTime, D.MaxTurnRateDegrees);

	// Control rotation is the aim: AWeapon::WeaponTrace resolves its direction from the pawn's view rotation,
	// which for a locally controlled pawn is this. The bot is genuinely pointing the gun, not being handed a
	// hit result.
	AI->SetControlRotation(NewRotation);
}

float UShooterAIAimComponent::GetAimErrorToTargetDegrees() const
{
	const AShooterAIController* AI = GetAIController();
	if (!IsValid(AI)) return 180.f;

	const APawn* Target = AI->GetTargetPawn();
	if (!IsValid(Target)) return 180.f;

	const FVector ToTarget = (Target->GetPawnViewLocation() - AI->GetEyeLocation()).GetSafeNormal();
	if (ToTarget.IsNearlyZero()) return 180.f;

	const FVector Facing = AI->GetControlRotation().Vector();
	return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(ToTarget | Facing, -1.f, 1.f)));
}

void UShooterAIAimComponent::UpdateAimDownSights()
{
	AShooterAIController* AI = GetAIController();
	UCombatComponent* Combat = GetCombat();
	if (!IsValid(AI) || !IsValid(Combat)) return;

	const APawn* Target = AI->GetTargetPawn();

	bool bWantAim = false;
	if (IsValid(Target) && AI->HasAcquiredTarget() && AI->GetBotState() == EShooterBotState::Engage)
	{
		const float Distance = FVector::Dist(Target->GetActorLocation(), AI->GetEyeLocation());
		bWantAim = Distance >= AimDownSightsMinRange;
	}

	if (bWantAim == bAimingDownSights) return;

	bAimingDownSights = bWantAim;

	// Routed through the same entry points the player's right mouse button uses, so the bot pays the same
	// costs: aiming cancels its sprint and narrows its own turning exactly as it does for a human.
	if (bAimingDownSights)
	{
		Combat->Initiate_Aim_Pressed();
	}
	else
	{
		Combat->Initiate_Aim_Released();
	}
}

void UShooterAIAimComponent::UpdateTrigger(float DeltaTime)
{
	AShooterAIController* AI = GetAIController();
	UCombatComponent* Combat = GetCombat();
	AWeapon* Weapon = GetWeapon();

	if (!IsValid(AI) || !IsValid(Combat) || !IsValid(Weapon))
	{
		ReleaseTrigger();
		return;
	}

	const FShooterBotDifficulty& D = AI->GetDifficulty();

	// Every round that has actually left the gun since the last tick gets punished with view punch. Counted
	// from the ammo delta rather than from the presses this component made, because an automatic weapon fires
	// most of its burst from UCombatComponent's own FireTimer loop, which this component never sees.
	if (AmmoAtLastCheck >= 0 && Weapon->Ammo < AmmoAtLastCheck)
	{
		const int32 RoundsFired = AmmoAtLastCheck - Weapon->Ammo;
		for (int32 Index = 0; Index < RoundsFired; ++Index)
		{
			ApplyViewPunch();
		}
	}
	AmmoAtLastCheck = Weapon->Ammo;

	const bool bCanFire =
		AI->GetBotState() == EShooterBotState::Engage &&
		AI->HasAcquiredTarget() &&
		AI->HasLineOfSight() &&
		Weapon->Ammo > 0 &&
		GetAimErrorToTargetDegrees() <= D.FireAngleToleranceDegrees;

	if (!bCanFire)
	{
		ReleaseTrigger();
		bBurstActive = false;
		bResting = false;
		BurstTimer = 0.f;
		return;
	}

	BurstTimer -= DeltaTime;

	if (bResting)
	{
		if (BurstTimer > 0.f) return;
		bResting = false;
	}

	if (!bBurstActive)
	{
		bBurstActive = true;
		BurstTimer = FMath::FRandRange(
			FMath::Min(D.BurstDurationMin, D.BurstDurationMax),
			FMath::Max(D.BurstDurationMin, D.BurstDurationMax));
	}
	else if (BurstTimer <= 0.f)
	{
		// Burst over. The rest window is the bot's trigger discipline, and it is the window the player is
		// meant to peek or push into - a bot that never stops firing has no counterplay.
		ReleaseTrigger();
		bBurstActive = false;
		bResting = true;
		BurstTimer = FMath::FRandRange(
			FMath::Min(D.BurstRestMin, D.BurstRestMax),
			FMath::Max(D.BurstRestMin, D.BurstRestMax));
		return;
	}

	if (Weapon->FireType == EFireType::Auto)
	{
		// One press holds the trigger; UCombatComponent's FireTimer loop carries the rest of the burst,
		// exactly as it does for a held mouse button.
		if (!bTriggerHeld && Weapon->WeaponStatus == EWeaponStatus::Idle)
		{
			PressTrigger();
		}
	}
	else if (Weapon->WeaponStatus == EWeaponStatus::Idle)
	{
		// Semi-automatic: the auto loop deliberately does not re-fire, so each round needs its own press.
		// FireTime still gates the cadence, so this cannot outpace the weapon.
		PressTrigger();
		ReleaseTrigger();
	}
}

void UShooterAIAimComponent::PressTrigger()
{
	if (UCombatComponent* Combat = GetCombat())
	{
		// The same entry point the player's fire input calls, so the bot inherits the whole path: the sprint
		// cancel, the dry-fire-into-reload behaviour, spread, the authoritative trace and damage.
		Combat->Initiate_FireWeapon_Pressed();
		bTriggerHeld = true;
	}
}

void UShooterAIAimComponent::ReleaseTrigger()
{
	if (!bTriggerHeld) return;

	if (UCombatComponent* Combat = GetCombat())
	{
		Combat->Initiate_FireWeapon_Released();
	}
	bTriggerHeld = false;
}

void UShooterAIAimComponent::HoldFire()
{
	ReleaseTrigger();
	bBurstActive = false;
	bResting = false;
	BurstTimer = 0.f;
}

void UShooterAIAimComponent::ApplyViewPunch()
{
	const AWeapon* Weapon = GetWeapon();
	const UCombatComponent* Combat = GetCombat();
	if (!IsValid(Weapon) || !IsValid(Combat)) return;

	const float Punch = Weapon->GetViewPunchPitch(Combat->bAiming) * ViewPunchScale;

	// Capped by the weapon's own accumulation ceiling, so a long burst walks the bot's aim up by exactly as
	// much as it walks a player's - no more, and not into the sky.
	const float Ceiling = Weapon->RecoilParams.ViewPunchMaxAccumulatedPitch;
	AccumulatedPunchPitch = FMath::Min(AccumulatedPunchPitch + Punch, FMath::Max(Ceiling, 0.f));
}

void UShooterAIAimComponent::SetYawLockToTravel(bool bLock, const FVector& TravelDirection)
{
	bYawLocked = bLock;
	if (bLock && !TravelDirection.IsNearlyZero())
	{
		LockedTravelDirection = TravelDirection.GetSafeNormal();
	}
}
