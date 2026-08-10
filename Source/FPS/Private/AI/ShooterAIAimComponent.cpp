// Copyright Druid Mechanics


#include "AI/ShooterAIAimComponent.h"

#include "AI/ShooterAIBlackboard.h"
#include "AI/ShooterAIController.h"
#include "Character/ShooterCharacter.h"
#include "Combat/CombatComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Weapon/Weapon.h"

UShooterAIAimComponent::UShooterAIAimComponent()
{
	// Ticked by hand from AShooterAIController::Tick so the within-frame order is guaranteed.
	PrimaryComponentTick.bCanEverTick = false;

	AimDownSightsMinRange = 1500.f;
	ViewPunchScale = 1.f;
	ViewPunchRecoveryDegreesPerSecond = 9.f;

	// Tightened from 75. At 75 the bot could legally swing three quarters of a right angle off a player it was
	// mid-duel with in order to line up a wall run, which was the largest single contributor to "it stops
	// looking at me". Wall runs are now confined to travel that is broadly toward the target anyway.
	TraverseMaxLookAwayDegrees = 50.f;
	TraverseMaxLookAwayTime = 1.6f;
	TraverseTurnRateMultiplier = 2.5f;

	AimState = EShooterAimState::Search;

	TrackedTime = 0.f;
	ErrorRefreshTimer = 0.f;
	CurrentErrorDegrees = FVector2D::ZeroVector;
	TargetErrorDegrees = FVector2D::ZeroVector;
	DebugErrorScale = 1.f;

	AccumulatedPunchPitch = 0.f;

	bTriggerHeld = false;
	bBurstActive = false;
	bResting = false;
	BurstTimer = 0.f;
	AmmoAtLastCheck = -1;

	bFireSuppressed = false;
	bAimingDownSights = false;

	bTravelYawClaimed = false;
	bTravelYawRenewedThisFrame = false;
	ClaimedTravelDirection = FVector::ZeroVector;
	TravelYawClaimElapsed = 0.f;

	DebugLookPoint = FVector::ZeroVector;
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

UShooterAIBlackboard* UShooterAIAimComponent::GetBlackboard() const
{
	const AShooterAIController* AI = GetAIController();
	return IsValid(AI) ? AI->GetKnowledge() : nullptr;
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

	UpdateAimState(DeltaTime);
	UpdateAimError(DeltaTime);

	FVector LookPoint;
	const bool bHaveLookPoint = ComputeLookPoint(LookPoint);
	DebugLookPoint = bHaveLookPoint ? LookPoint : FVector::ZeroVector;

	UpdateRotation(DeltaTime, LookPoint, bHaveLookPoint);
	UpdateAimDownSights();
	UpdateTrigger(DeltaTime);

	// The claim expires unless locomotion asked for it again this frame. Renewal is the whole handshake: a
	// movement layer that dies, resets or simply stops wanting the yaw cannot leave the bot facing away.
	if (!bTravelYawRenewedThisFrame)
	{
		ReleaseTravelYawClaim();
	}
	bTravelYawRenewedThisFrame = false;
}

/* --- Aim state --- */

void UShooterAIAimComponent::UpdateAimState(float DeltaTime)
{
	const UShooterAIBlackboard* BB = GetBlackboard();
	const bool bTargetLive = IsValid(BB) && BB->IsTargetLive();

	if (bTravelYawClaimed)
	{
		TravelYawClaimElapsed += DeltaTime;
		AimState = EShooterAimState::Traverse;
		return;
	}

	// One input, and only one: does the bot have something worth looking at. Nothing about which tactical
	// action is running reaches this decision, which is what guarantees the bot's view stays on a live
	// target through a retreat, a reposition or a strafe.
	AimState = bTargetLive ? EShooterAimState::Track : EShooterAimState::Search;
}

/* --- Yaw claim --- */

bool UShooterAIAimComponent::RequestTravelYawClaim(const FVector& TravelDirection, bool bForce)
{
	const FVector Direction = TravelDirection.GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		ReleaseTravelYawClaim();
		return false;
	}

	const AShooterAIController* AI = GetAIController();
	const UShooterAIBlackboard* BB = GetBlackboard();
	if (!IsValid(AI) || !IsValid(BB))
	{
		ReleaseTravelYawClaim();
		return false;
	}

	if (!bForce && BB->IsTargetLive())
	{
		// Hard cap on how long the bot may be looking anywhere but at a live opponent.
		if (bTravelYawClaimed && TravelYawClaimElapsed >= TraverseMaxLookAwayTime)
		{
			ReleaseTravelYawClaim();
			return false;
		}

		// And a hard cap on how far. Measured against the last known position rather than the live one so a
		// target that ducks behind cover mid-claim does not instantly revoke it.
		const FVector ToTarget = (BB->GetLastKnownTargetEyeLocation() - AI->GetEyeLocation()).GetSafeNormal2D();
		if (!ToTarget.IsNearlyZero())
		{
			const float CosLimit = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(TraverseMaxLookAwayDegrees, 0.f, 180.f)));
			if ((ToTarget | Direction) < CosLimit)
			{
				ReleaseTravelYawClaim();
				return false;
			}
		}
	}

	if (!bTravelYawClaimed)
	{
		TravelYawClaimElapsed = 0.f;
	}

	bTravelYawClaimed = true;
	bTravelYawRenewedThisFrame = true;
	ClaimedTravelDirection = Direction;
	return true;
}

void UShooterAIAimComponent::ReleaseTravelYawClaim()
{
	bTravelYawClaimed = false;
	TravelYawClaimElapsed = 0.f;
	ClaimedTravelDirection = FVector::ZeroVector;
}

bool UShooterAIAimComponent::IsTravelYawAligned(float DotThreshold) const
{
	if (!bTravelYawClaimed) return false;

	const AShooterCharacter* Bot = GetShooterPawn();
	if (!IsValid(Bot)) return false;

	// The capsule's forward vector, not the control rotation's - that is the vector
	// UShooterMovementComponent::TryStartWallRun actually tests movement input against.
	const FVector Forward = Bot->GetActorForwardVector().GetSafeNormal2D();
	if (Forward.IsNearlyZero()) return false;

	return (Forward | ClaimedTravelDirection) >= DotThreshold;
}

/* --- Look point --- */

bool UShooterAIAimComponent::ComputeLookPoint(FVector& OutLookPoint) const
{
	const AShooterAIController* AI = GetAIController();
	const UShooterAIBlackboard* BB = GetBlackboard();
	if (!IsValid(AI) || !IsValid(BB)) return false;

	const FShooterBotDifficulty& D = AI->GetDifficulty();

	// --- A live target outranks everything. This single ordering is the whole "never turn your view away"
	// rule; it holds in every tactical action because no tactical action is consulted here.
	if (BB->IsTargetLive())
	{
		if (BB->HasRawLineOfSight())
		{
			// The bot aims at where the target *was*, and TargetLeadFraction is how well it compensates for
			// its own staleness. Fraction 0 leaves the full lag in, so a strafing player is chronically
			// under-led; 1 cancels it exactly; above 1 over-leads. Expressing both tracking lag and leading
			// skill through one term is what keeps them from fighting each other - two independent offsets
			// would let a bot with poor leading still track perfectly, which is not a thing a human hand does.
			const float EffectiveLag = D.AimTrackingLag * (1.f - D.TargetLeadFraction);
			OutLookPoint = BB->GetPredictedTargetEyeLocation(-EffectiveLag);
			return true;
		}

		// Seen recently but not right now. Keep the gun on the remembered position: the trigger refuses to
		// fire without line of sight, so this cannot become shooting through a wall, and it is what makes the
		// bot re-acquire instantly when the player steps back out.
		OutLookPoint = BB->GetLastKnownTargetEyeLocation();
		return true;
	}

	// --- Search. Nothing worth aiming at, so look where the bot is going - it should see what it is walking
	// into, and it should be facing the right way when it rounds a corner.
	if (const AShooterCharacter* Bot = AI->GetShooterPawn())
	{
		const FVector Travel = Bot->GetVelocity().GetSafeNormal2D();
		if (!Travel.IsNearlyZero())
		{
			OutLookPoint = AI->GetEyeLocation() + Travel * 1000.f;
			return true;
		}
	}

	return false;
}

/* --- Aim error --- */

void UShooterAIAimComponent::UpdateAimError(float DeltaTime)
{
	const AShooterAIController* AI = GetAIController();
	const UShooterAIBlackboard* BB = GetBlackboard();
	if (!IsValid(AI) || !IsValid(BB)) return;

	const FShooterBotDifficulty& D = AI->GetDifficulty();

	// Tracking time only accumulates while the bot actually has the target. Losing sight resets it, so
	// peeking in and out of cover costs the bot its settled aim every time - which is exactly why peeking
	// beats standing in the open, and it falls out of this one line rather than needing its own rule.
	if (BB->HasAcquiredTarget())
	{
		TrackedTime += DeltaTime;
	}
	else
	{
		TrackedTime = 0.f;
	}

	const float SettleAlpha = FMath::Clamp(TrackedTime / FMath::Max(D.AimSettleTime, KINDA_SMALL_NUMBER), 0.f, 1.f);
	float ErrorMagnitude = FMath::Lerp(D.AimErrorInitialDegrees, D.AimErrorSettledDegrees, SettleAlpha);

	// The core mechanic, applied to accuracy. A target standing still collapses the cone toward nothing; a
	// target sliding, jumping or wall running blows it wide open. Two independent axes of skill on purpose:
	// settle time rewards the bot for *holding* an angle, stillness rewards it for the target being easy.
	DebugErrorScale = D.AimErrorStillnessResponse.Evaluate(BB->GetTargetStillness());
	ErrorMagnitude *= DebugErrorScale;

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
	else
	{
		// Rescale the standing offset toward the current magnitude as well, so a target who stops moving is
		// punished inside the same beat rather than at the next refresh. Without this the response to
		// stillness is quantised to AimErrorRefreshInterval and reads as laggy.
		const float TargetLength = TargetErrorDegrees.Size();
		if (TargetLength > KINDA_SMALL_NUMBER && ErrorMagnitude < TargetLength)
		{
			TargetErrorDegrees *= (ErrorMagnitude / TargetLength);
		}
	}

	// Interped so the error slides rather than snapping. Speed is tied to the refresh interval so the offset
	// arrives at roughly the moment a new one is chosen - that reads as a hand micro-correcting, where a snap
	// reads as a teleporting crosshair.
	const float InterpSpeed = 2.f / FMath::Max(D.AimErrorRefreshInterval, KINDA_SMALL_NUMBER);
	CurrentErrorDegrees = FMath::Vector2DInterpTo(CurrentErrorDegrees, TargetErrorDegrees, DeltaTime, InterpSpeed);
}

/* --- Rotation --- */

void UShooterAIAimComponent::UpdateRotation(float DeltaTime, const FVector& LookPoint, bool bHaveLookPoint)
{
	AShooterAIController* AI = GetAIController();
	const UShooterAIBlackboard* BB = GetBlackboard();
	if (!IsValid(AI) || !IsValid(BB)) return;

	const FShooterBotDifficulty& D = AI->GetDifficulty();
	const FRotator CurrentRotation = AI->GetControlRotation();

	FRotator DesiredRotation = CurrentRotation;

	if (bHaveLookPoint)
	{
		const FVector Eye = AI->GetEyeLocation();
		const FVector ToLook = LookPoint - Eye;
		if (!ToLook.IsNearlyZero())
		{
			DesiredRotation = ToLook.Rotation();
			DesiredRotation.Pitch += CurrentErrorDegrees.X + AccumulatedPunchPitch;
			DesiredRotation.Yaw += CurrentErrorDegrees.Y;
		}
	}
	else if (AimState != EShooterAimState::Traverse)
	{
		// Nothing to look at and no traversal claim on the yaw - leave the aim where it is rather than
		// snapping it to level, which would look like the bot forgetting itself every time it loses sight.
		return;
	}

	float TurnRate = D.MaxTurnRateDegrees;

	if (AimState == EShooterAimState::Traverse && !ClaimedTravelDirection.IsNearlyZero())
	{
		// Locomotion owns the yaw for the duration of the claim. Pitch stays on the look point so the bot
		// comes off the wall already looking roughly the right way.
		DesiredRotation.Yaw = ClaimedTravelDirection.Rotation().Yaw;

		// The claim has a short deadline (see TraverseTurnRateMultiplier). This is not an aim advantage: the
		// fire gate below refuses to shoot for the whole claim.
		TurnRate *= TraverseTurnRateMultiplier;
	}
	else if (AimState == EShooterAimState::Track)
	{
		// The core mechanic, applied to tracking. A still target gets snapped onto; a moving one out-turns
		// the bot's hand and the crosshair visibly trails behind them.
		TurnRate *= D.TurnRateStillnessResponse.Evaluate(BB->GetTargetStillness());
	}

	DesiredRotation.Pitch = FMath::Clamp(FRotator::NormalizeAxis(DesiredRotation.Pitch), -89.f, 89.f);
	DesiredRotation.Roll = 0.f;

	// Constant rate, not proportional: this is a hand on a mouse, and it has a top speed. A proportional
	// interp would make the bot snap most of the way instantly on a large angle, which is what reads as an
	// aimbot even when the final accuracy is poor.
	const FRotator NewRotation = FMath::RInterpConstantTo(
		CurrentRotation, DesiredRotation, DeltaTime, FMath::Max(TurnRate, 1.f));

	// Control rotation is the aim: AWeapon::WeaponTrace resolves its direction from the pawn's view rotation,
	// which for a locally controlled pawn is this. The bot is genuinely pointing the gun, not being handed a
	// hit result. It is also what turns the capsule, because BP_EnemyBot has bUseControllerRotationYaw set -
	// which is why the traversal claim above is able to satisfy the wall run's forward-input test at all.
	AI->SetControlRotation(NewRotation);
}

/* --- Aiming down sights --- */

void UShooterAIAimComponent::UpdateAimDownSights()
{
	const AShooterAIController* AI = GetAIController();
	const UShooterAIBlackboard* BB = GetBlackboard();
	UCombatComponent* Combat = GetCombat();
	if (!IsValid(AI) || !IsValid(BB) || !IsValid(Combat)) return;

	// Driven by the aim layer's own state, never by the tactical action: the bot should be able to hold its
	// sights on the player through a reposition exactly as a player does.
	bool bWantAim = false;
	if (AimState == EShooterAimState::Track && BB->HasAcquiredTarget())
	{
		bWantAim = BB->GetDistanceToTarget() >= AimDownSightsMinRange;
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

/* --- Trigger --- */

void UShooterAIAimComponent::UpdateTrigger(float DeltaTime)
{
	const AShooterAIController* AI = GetAIController();
	const UShooterAIBlackboard* BB = GetBlackboard();
	UCombatComponent* Combat = GetCombat();
	AWeapon* Weapon = GetWeapon();

	if (!IsValid(AI) || !IsValid(BB) || !IsValid(Combat) || !IsValid(Weapon))
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

	// Deliberately not conditioned on the tactical action. Shooting is an aim-layer concern, so the bot will
	// take a free shot during an approach or a reposition exactly as a player would - the old bot could only
	// fire in one state, which meant every reposition was several seconds of running past an open target with
	// the trigger down.
	const bool bCanFire =
		!bFireSuppressed &&
		AimState == EShooterAimState::Track &&
		AI->WantsToShoot() &&
		BB->GetAngleToTargetDegrees() <= D.FireAngleToleranceDegrees;

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

void UShooterAIAimComponent::SetFireSuppressed(bool bSuppressed)
{
	if (bFireSuppressed == bSuppressed) return;

	bFireSuppressed = bSuppressed;

	// Dropped immediately rather than at the next UpdateTrigger, so the trigger is never left latched on
	// UCombatComponent across the frame the suppression starts.
	if (bFireSuppressed)
	{
		HoldFire();
	}
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

/* --- Debug --- */

void UShooterAIAimComponent::DrawAimDebug() const
{
	const AShooterAIController* AI = GetAIController();
	if (!IsValid(AI) || !IsValid(GetWorld())) return;

	const FVector Eye = AI->GetEyeLocation();

	// Where the bot is actually pointing the gun, including its aim error. The gap between this and the green
	// LOS line the controller draws is the bot's inaccuracy made visible - the single most useful thing to
	// watch while tuning the stillness response.
	if (!DebugLookPoint.IsNearlyZero())
	{
		DrawDebugLine(GetWorld(), Eye, DebugLookPoint, FColor::Magenta, false, -1.f, 0, 1.f);
		DrawDebugPoint(GetWorld(), DebugLookPoint, 12.f, FColor::Magenta, false, -1.f);
	}

	// The traversal claim, when one is held. Orange means locomotion currently owns the yaw - if this is on
	// screen while the bot is not visibly wall running, the wall-run commit is failing.
	if (bTravelYawClaimed)
	{
		DrawDebugLine(GetWorld(), Eye, Eye + ClaimedTravelDirection * 400.f, FColor::Orange, false, -1.f, 0, 3.f);
	}
}
