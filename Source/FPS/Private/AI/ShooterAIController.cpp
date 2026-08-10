// Copyright Druid Mechanics


#include "AI/ShooterAIController.h"

#include "AI/ShooterAIAimComponent.h"
#include "AI/ShooterAIBlackboard.h"
#include "AI/ShooterAIMovementTechComponent.h"
#include "Character/ShooterCharacter.h"
#include "Character/ShooterMovementComponent.h"
#include "Combat/CombatComponent.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Health/HealthComponent.h"
#include "Interfaces/PlayerInterface.h"
#include "NavigationSystem.h"
#include "Weapon/Weapon.h"

AShooterAIController::AShooterAIController()
{
	PrimaryActorTick.bCanEverTick = true;

	// Required for the bot to survive its own death. AController::PawnPendingDestroy destroys any controller
	// that has no PlayerState the moment its pawn is destroyed, so without this the controller died with the
	// body and the respawn request was handed an already-destroyed controller - which is why a killed bot
	// never came back. With a PlayerState the controller outlives the pawn and can be re-possessed.
	bWantsPlayerState = true;

	// The bot writes its own control rotation every frame from the aim component. Left at its default this
	// would overwrite that with the pawn's current orientation, and the bot would never turn onto a target.
	bSetControlRotationFromPawnOrientation = false;

	// Path following must not rotate the pawn toward its route: the bot has to be able to face its target
	// while moving sideways, exactly as a strafing player does. This is the whole reason look direction and
	// travel direction can be separate concerns at all.
	bAllowStrafe = true;

	Knowledge = CreateDefaultSubobject<UShooterAIBlackboard>("Knowledge");
	AimLogic = CreateDefaultSubobject<UShooterAIAimComponent>("AimLogic");
	MovementTech = CreateDefaultSubobject<UShooterAIMovementTechComponent>("MovementTech");

	Skill = EShooterBotSkill::Regular;

	// Sized so a reposition is a step off the current angle rather than a lap of the arena.
	RepositionSearchRadius = 800.f;
	GoalAcceptanceRadius = 120.f;
	ReloadWatchdogTime = 3.f;
	HuntRepathInterval = 2.5f;
	ApproachRepathInterval = 0.75f;
	MoveGoalTimeout = 8.f;
	bSearchTowardTargetWhenLost = true;
	bDebugDrawAI = false;

	SteerRadialGain = 2.2f;
	SteerTangentWeightStrafe = 0.9f;
	SteerTangentWeightApproach = 0.3f;
	SteerRepositionTangentBoost = 1.3f;
	SteerRepositionBiasWeight = 0.7f;
	SteerRangeMultiplier = 3.f;
	SteerTurnInterpSpeed = 6.f;
	SteerNoiseMaxDegrees = 28.f;
	SteerNoiseIntervalMin = 0.6f;
	SteerNoiseIntervalMax = 1.6f;
	SteerWhiskerLength = 220.f;
	SteerWhiskerAngleDegrees = 45.f;
	SteerAvoidWeight = 1.6f;
	SteerNavProbeDistance = 160.f;
	SteerNavMaxStepHeight = 90.f;
	SteerBlockedRecoveryTime = 0.6f;
	SteerRecoveryPathTime = 2.f;

	bSteering = false;
	SteeringDirection = FVector::ZeroVector;
	SteerBlockedTime = 0.f;
	SteerRecoveryTimer = 0.f;
	SteerNoiseCurrent = 0.f;
	SteerNoiseTarget = 0.f;
	SteerNoiseTimer = 0.f;
	RepositionArcRemaining = 0.f;
	RepositionBiasDirection = FVector::ZeroVector;

	CurrentAction = EShooterBotAction::Idle;
	CurrentLatches = EShooterAILatch::None;
	ActionElapsed = 0.f;
	ActionMinCommit = 0.f;
	ActionMaxDuration = 0.f;
	RetreatSuppressedTimer = 0.f;
	TimeSinceRepositionRoll = 0.f;

	MoveGoal = FVector::ZeroVector;
	bHasMoveGoal = false;
	MoveGoalElapsed = 0.f;
	HuntRepathTimer = 0.f;
	ApproachRepathTimer = 0.f;
	StrafeLegRemaining = 0.f;
	StrafeSign = 1;
	ReloadWatchdogTimer = 0.f;
}

void AShooterAIController::BeginPlay()
{
	Super::BeginPlay();

	// Custom means "the numbers on the Blueprint are the truth" - see the note on the property.
	if (Skill != EShooterBotSkill::Custom)
	{
		Difficulty = GetShooterBotDifficultyPreset(Skill);
	}

	// Every component is ticked by hand from this class so the order within a frame is fixed: knowledge,
	// then decision, then locomotion, then aim. Aim runs last because a traversal yaw claim has to be raised
	// before aim writes the control rotation, or the wall-run attach test sees last frame's facing.
	if (IsValid(Knowledge))
	{
		Knowledge->SetComponentTickEnabled(false);
	}
	if (IsValid(AimLogic))
	{
		AimLogic->SetComponentTickEnabled(false);
	}
	if (IsValid(MovementTech))
	{
		MovementTech->SetComponentTickEnabled(false);
	}
}

void AShooterAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// Fresh pawn, fresh brain. A respawn reuses this controller, so nothing about the previous life may
	// survive - a stale target position would send the new pawn running at a corpse's last position.
	CurrentAction = EShooterBotAction::Idle;
	CurrentLatches = EShooterAILatch::None;
	ActionElapsed = 0.f;
	ActionMinCommit = 0.f;
	ActionMaxDuration = 0.f;
	RetreatSuppressedTimer = 0.f;
	TimeSinceRepositionRoll = 0.f;

	bHasMoveGoal = false;
	MoveGoalElapsed = 0.f;
	HuntRepathTimer = 0.f;
	ApproachRepathTimer = 0.f;
	StrafeLegRemaining = 0.f;
	ReloadWatchdogTimer = 0.f;

	bSteering = false;
	SteeringDirection = FVector::ZeroVector;
	SteerBlockedTime = 0.f;
	SteerRecoveryTimer = 0.f;
	RepositionArcRemaining = 0.f;
	RepositionBiasDirection = FVector::ZeroVector;

	if (IsValid(Knowledge))
	{
		Knowledge->ResetKnowledge();
	}
	if (IsValid(MovementTech))
	{
		MovementTech->ResetTech();
	}
	if (IsValid(AimLogic))
	{
		AimLogic->HoldFire();
		AimLogic->ReleaseTravelYawClaim();
	}

	// Being shot is the highest-priority interrupt, and the only way to know about it without polling is the
	// health component's own delegate. Rebound per possession because a respawn brings a new pawn with a new
	// component; the old binding died with the old pawn.
	if (BoundHealth.IsValid())
	{
		BoundHealth->OnHealthChanged.RemoveDynamic(this, &ThisClass::OnPawnHealthChanged);
		BoundHealth = nullptr;
	}

	if (UHealthComponent* PawnHealth = UHealthComponent::FindHealthComponent(InPawn))
	{
		PawnHealth->OnHealthChanged.AddDynamic(this, &ThisClass::OnPawnHealthChanged);
		BoundHealth = PawnHealth;
	}
}

void AShooterAIController::OnUnPossess()
{
	// The trigger is a latched bool on UCombatComponent. Losing the pawn without dropping it would leave a
	// dead bot's combat component believing the trigger is still held.
	if (IsValid(AimLogic))
	{
		AimLogic->HoldFire();
		AimLogic->ReleaseTravelYawClaim();
	}
	if (IsValid(MovementTech))
	{
		MovementTech->ResetTech();
	}

	if (BoundHealth.IsValid())
	{
		BoundHealth->OnHealthChanged.RemoveDynamic(this, &ThisClass::OnPawnHealthChanged);
		BoundHealth = nullptr;
	}

	Super::OnUnPossess();
}

void AShooterAIController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	AShooterCharacter* Bot = GetShooterPawn();
	if (!IsValid(Bot)) return;

	// A dying pawn keeps its capsule and its controller until the respawn timer fires. Without this the bot
	// keeps shooting and pathing from inside its own death.
	if (!IPlayerInterface::Execute_IsAlive(Bot))
	{
		if (IsValid(AimLogic))
		{
			AimLogic->HoldFire();
			AimLogic->ReleaseTravelYawClaim();
		}
		EndSteering();
		StopMoving();
		return;
	}

	if (IsValid(Knowledge))
	{
		Knowledge->TickBlackboard(DeltaTime);
	}

	UpdateActionSelection(DeltaTime);
	UpdateWeaponHousekeeping(DeltaTime);

	if (IsValid(MovementTech))
	{
		MovementTech->TickMovementTech(DeltaTime);
	}

	if (IsValid(AimLogic))
	{
		AimLogic->TickAim(DeltaTime);
	}

	if (bDebugDrawAI)
	{
		DrawDebug();
	}
}

/* --- Accessors --- */

AShooterCharacter* AShooterAIController::GetShooterPawn() const
{
	return Cast<AShooterCharacter>(GetPawn());
}

UCombatComponent* AShooterAIController::GetCombat() const
{
	return UCombatComponent::FindCombatComponent(GetPawn());
}

UShooterMovementComponent* AShooterAIController::GetShooterMovement() const
{
	const AShooterCharacter* Bot = GetShooterPawn();
	return IsValid(Bot) ? Bot->GetShooterMovement() : nullptr;
}

FVector AShooterAIController::GetEyeLocation() const
{
	const APawn* Bot = GetPawn();
	if (!IsValid(Bot)) return FVector::ZeroVector;

	// Deliberately the pawn's view location rather than the camera's. This is the same origin
	// AController::GetActorEyesViewPoint hands AWeapon::WeaponTrace for an AI pawn, so what the bot aims
	// with and what it shoots from cannot disagree.
	return Bot->GetPawnViewLocation();
}

APawn* AShooterAIController::GetTargetPawn() const
{
	return IsValid(Knowledge) ? Knowledge->GetTargetPawn() : nullptr;
}

bool AShooterAIController::HasLineOfSight() const
{
	return IsValid(Knowledge) && Knowledge->HasLineOfSight();
}

bool AShooterAIController::HasAcquiredTarget() const
{
	return IsValid(Knowledge) && Knowledge->HasAcquiredTarget();
}

bool AShooterAIController::IsTargetLive() const
{
	return IsValid(Knowledge) && Knowledge->IsTargetLive();
}

float AShooterAIController::GetTargetStillness() const
{
	return IsValid(Knowledge) ? Knowledge->GetTargetStillness() : 0.f;
}

FVector AShooterAIController::GetLastKnownTargetLocation() const
{
	return IsValid(Knowledge) ? Knowledge->GetLastKnownTargetLocation() : FVector::ZeroVector;
}

float AShooterAIController::GetPreferredRange() const
{
	float Range = Difficulty.PreferredEngagementRange;

	// A slow, heavy, high-damage weapon wants to fight further out than a fast one - that is the whole of the
	// GDD's playstyle trade-off expressed as bot behaviour, and it comes free from the weapon's own data
	// rather than from a per-weapon AI table.
	if (const UCombatComponent* Combat = GetCombat())
	{
		if (const AWeapon* Weapon = Combat->CurrentWeapon)
		{
			if (Weapon->FireType == EFireType::SemiAuto)
			{
				Range *= 1.6f;
			}
		}
	}

	return Range;
}

bool AShooterAIController::WantsToShoot() const
{
	if (!IsValid(Knowledge)) return false;
	if (!Knowledge->HasAcquiredTarget()) return false;
	if (!Knowledge->HasLineOfSight()) return false;

	return Knowledge->GetMagAmmo() > 0;
}

float AShooterAIController::GetDistanceToMoveGoal() const
{
	const APawn* Bot = GetPawn();
	if (!bHasMoveGoal || !IsValid(Bot)) return TNumericLimits<float>::Max();

	return FVector::Dist(Bot->GetActorLocation(), MoveGoal);
}

/* --- Yaw arbitration --- */

bool AShooterAIController::RequestTravelYawClaim(const FVector& TravelDirection, bool bForce)
{
	return IsValid(AimLogic) && AimLogic->RequestTravelYawClaim(TravelDirection, bForce);
}

void AShooterAIController::ReleaseTravelYawClaim()
{
	if (IsValid(AimLogic))
	{
		AimLogic->ReleaseTravelYawClaim();
	}
}

bool AShooterAIController::HasTravelYawClaim() const
{
	return IsValid(AimLogic) && AimLogic->HasTravelYawClaim();
}

bool AShooterAIController::IsTravelYawAligned(float DotThreshold) const
{
	return IsValid(AimLogic) && AimLogic->IsTravelYawAligned(DotThreshold);
}

void AShooterAIController::SetFireSuppressed(bool bSuppressed)
{
	if (IsValid(AimLogic))
	{
		AimLogic->SetFireSuppressed(bSuppressed);
	}
}

/* --- Action selection --- */

void AShooterAIController::UpdateActionSelection(float DeltaTime)
{
	ActionElapsed += DeltaTime;
	TimeSinceRepositionRoll += DeltaTime;
	RetreatSuppressedTimer = FMath::Max(0.f, RetreatSuppressedTimer - DeltaTime);

	// Abandon a move goal the bot is never going to reach. See the note on MoveGoalTimeout: an action latched
	// on GoalPending can otherwise wait forever on an unreachable point, and a stationary bot is the worst
	// looking failure this system has.
	if (bHasMoveGoal)
	{
		MoveGoalElapsed += DeltaTime;
		if (MoveGoalElapsed >= MoveGoalTimeout)
		{
			bHasMoveGoal = false;
			MoveGoalElapsed = 0.f;
			StopMovement();
		}
	}

	const EShooterAIInterrupt Interrupt = IsValid(Knowledge)
		? Knowledge->ConsumeInterrupt()
		: EShooterAIInterrupt::None;

	// The three - and only three - reasons the bot may change its mind. Anything else and the current action
	// simply continues; that commitment is the point.
	bool bInvalidated = false;

	if (Interrupt != EShooterAIInterrupt::None)
	{
		// Interrupts bypass the commitment window entirely. Being shot at is not something to finish your
		// current strafe leg before considering.
		bInvalidated = true;
	}
	else if (ActionElapsed >= ActionMinCommit)
	{
		if (!EvaluateLatches(CurrentLatches))
		{
			bInvalidated = true;
		}
		else if (ActionMaxDuration > 0.f && ActionElapsed >= ActionMaxDuration)
		{
			bInvalidated = true;
		}
	}

	if (bInvalidated)
	{
		EShooterAILatch Latches = EShooterAILatch::None;
		float MinCommit = Difficulty.ActionMinCommitTime;
		float MaxDuration = 0.f;

		const EShooterBotAction Chosen = SelectAction(Interrupt, Latches, MinCommit, MaxDuration);
		EnterAction(Chosen, Latches, MinCommit, MaxDuration);
	}

	DriveAction(DeltaTime);
}

EShooterBotAction AShooterAIController::SelectAction(EShooterAIInterrupt Interrupt, EShooterAILatch& OutLatches,
	float& OutMinCommit, float& OutMaxDuration) const
{
	OutMinCommit = Difficulty.ActionMinCommitTime;
	OutMaxDuration = 0.f;
	OutLatches = EShooterAILatch::None;

	if (!IsValid(Knowledge) || !Knowledge->HasTarget())
	{
		// Idle polls quickly - it is the one action that exists purely to wait for something to happen.
		OutMinCommit = 0.2f;
		OutMaxDuration = 0.5f;
		return EShooterBotAction::Idle;
	}

	const float HealthFraction = Knowledge->GetSelfHealthFraction();
	const bool bMagEmpty = Knowledge->GetMagAmmo() <= 0;
	const bool bCanReload = Knowledge->CanReload();

	// --- Break off, in priority order. Both of these are reasons to stop trading and go somewhere the
	// player is not, and they are checked before engagement so a losing fight is left rather than finished.

	// A dry gun with reserve left is unconditional: there is nothing else to do.
	if (bMagEmpty && bCanReload)
	{
		OutLatches = EShooterAILatch::TargetKnown | EShooterAILatch::MagEmpty;
		OutMinCommit = FMath::Max(Difficulty.ActionMinCommitTime, 1.f);
		OutMaxDuration = Difficulty.RetreatMaxDuration;
		return EShooterBotAction::Retreat;
	}

	// Health is suppressed for a while after a retreat ends - health does not regenerate, so without the
	// cooldown the bot would retreat, time out, re-select Retreat and kite for the rest of the match.
	if (HealthFraction <= Difficulty.RetreatHealthFraction && RetreatSuppressedTimer <= 0.f)
	{
		OutLatches = EShooterAILatch::TargetKnown;
		OutMinCommit = FMath::Max(Difficulty.ActionMinCommitTime, 1.f);
		OutMaxDuration = Difficulty.RetreatMaxDuration;
		return EShooterBotAction::Retreat;
	}

	// --- Being shot. The chapter's canonical interrupt, and the one that needs a response of its own rather
	// than merely resetting the current action's clock.
	//
	// Rate limited in the blackboard (DamageInterruptCooldown), because an automatic weapon lands a round
	// every FireTime and an interrupt per round would re-select several times a second - which is the
	// commitment model switched off exactly when the bot most needs to hold a plan.
	if (Interrupt == EShooterAIInterrupt::TookDamage)
	{
		if (!Knowledge->HasLineOfSight())
		{
			// Shot from an angle it cannot see. UShooterAIBlackboard::RegisterDamageFrom has already written
			// the shooter's position into the knowledge block and reset the sight timer, so the aim layer is
			// already swinging onto them and this hunt walks at a real position rather than searching blind.
			OutLatches = EShooterAILatch::TargetKnown | EShooterAILatch::TargetHidden;
			OutMinCommit = FMath::Max(Difficulty.ActionMinCommitTime, 0.8f);
			OutMaxDuration = 2.5f;
			return EShooterBotAction::Hunt;
		}

		// Shot while it can see them: break the angle instead of standing in it. This is the one place a
		// reposition is not a flourish - continuing to trade from a position you are losing on is the mistake.
		OutLatches = EShooterAILatch::TargetKnown;
		OutMinCommit = Difficulty.RepositionMinDuration;
		OutMaxDuration = Difficulty.RepositionMaxDuration;
		return EShooterBotAction::Reposition;
	}

	// --- No live target: search. Deliberately never falls through to Idle once TargetMemoryTime expires. It
	// used to, and the result in PIE was a bot that lost sight once, hunted, timed out and then stood in a
	// corner permanently. In a 1v1 there is exactly one opponent and giving up on them is never right.
	if (!Knowledge->IsTargetLive())
	{
		OutLatches = EShooterAILatch::TargetKnown | EShooterAILatch::TargetHidden;
		OutMinCommit = FMath::Max(Difficulty.ActionMinCommitTime, 1.f);
		OutMaxDuration = Difficulty.HuntMaxDuration;
		return EShooterBotAction::Hunt;
	}

	// --- A live target. Everything from here is a fighting choice.
	const float Distance = Knowledge->GetDistanceToTarget();
	const float PreferredRange = GetPreferredRange();
	const float ApproachStopRange = PreferredRange * (1.f + Difficulty.EngagementRangeTolerance);

	// Two thresholds, not one. Approach stops at the inner edge of the band and Strafe only gives up at the
	// outer edge (the TargetNear latch), so the pair cannot swap every time the range wobbles across a
	// single line.
	if (Distance > ApproachStopRange)
	{
		OutLatches = EShooterAILatch::TargetKnown | EShooterAILatch::TargetLive
			| EShooterAILatch::HealthOk | EShooterAILatch::TargetFar;
		OutMaxDuration = 4.f;
		return EShooterBotAction::Approach;
	}

	// Occasionally refuse a winnable trade and cross to a new angle instead. This is what stops the bot from
	// standing in one doorway for a whole duel, and it is the cheapest single behaviour that makes it feel
	// like it has a plan.
	//
	// RepositionChance is authored per *second*, so it is converted against the time actually elapsed since
	// the last roll rather than compared directly - selection now runs at irregular intervals, so a raw
	// comparison would fire at whatever rate the bot happened to be invalidated at.
	if (Interrupt == EShooterAIInterrupt::None && CurrentAction == EShooterBotAction::Strafe)
	{
		const float RollWindow = FMath::Clamp(TimeSinceRepositionRoll, 0.f, 5.f);
		const float RollChance = 1.f - FMath::Pow(1.f - FMath::Clamp(Difficulty.RepositionChance, 0.f, 1.f), RollWindow);

		if (FMath::FRand() < RollChance)
		{
			// No GoalPending latch any more: a reposition is now a steered arc with no destination, so its
			// only exit is its own duration.
			OutLatches = EShooterAILatch::TargetKnown;
			OutMinCommit = Difficulty.RepositionMinDuration;
			OutMaxDuration = Difficulty.RepositionMaxDuration;
			return EShooterBotAction::Reposition;
		}
	}

	OutLatches = EShooterAILatch::TargetKnown | EShooterAILatch::TargetLive
		| EShooterAILatch::HealthOk | EShooterAILatch::MagHasAmmo | EShooterAILatch::TargetNear;
	OutMaxDuration = 8.f;
	return EShooterBotAction::Strafe;
}

bool AShooterAIController::EvaluateLatches(EShooterAILatch Mask) const
{
	if (Mask == EShooterAILatch::None) return false;
	if (!IsValid(Knowledge)) return false;

	if (EnumHasAnyFlags(Mask, EShooterAILatch::TargetKnown) && !Knowledge->HasTarget())
	{
		return false;
	}

	// TargetLive rather than raw line of sight on purpose: an action must not be torn down because a railing
	// crossed the trace for one frame. The debounce lives in the blackboard, and the memory window means a
	// brief break in sight does not count as losing the target at all.
	if (EnumHasAnyFlags(Mask, EShooterAILatch::TargetLive) && !Knowledge->IsTargetLive())
	{
		return false;
	}

	if (EnumHasAnyFlags(Mask, EShooterAILatch::TargetHidden) && Knowledge->HasLineOfSight())
	{
		return false;
	}

	if (EnumHasAnyFlags(Mask, EShooterAILatch::HealthOk) &&
		Knowledge->GetSelfHealthFraction() <= Difficulty.RetreatHealthFraction && RetreatSuppressedTimer <= 0.f)
	{
		return false;
	}

	if (EnumHasAnyFlags(Mask, EShooterAILatch::MagHasAmmo) && Knowledge->GetMagAmmo() <= 0)
	{
		return false;
	}

	if (EnumHasAnyFlags(Mask, EShooterAILatch::MagEmpty) && Knowledge->GetMagAmmo() > 0)
	{
		return false;
	}

	if (EnumHasAnyFlags(Mask, EShooterAILatch::GoalPending) && (!bHasMoveGoal || HasReachedMoveGoal()))
	{
		return false;
	}

	const float PreferredRange = GetPreferredRange();

	if (EnumHasAnyFlags(Mask, EShooterAILatch::TargetFar))
	{
		if (Knowledge->GetDistanceToTarget() <= PreferredRange * (1.f + Difficulty.EngagementRangeTolerance))
		{
			return false;
		}
	}

	if (EnumHasAnyFlags(Mask, EShooterAILatch::TargetNear))
	{
		if (Knowledge->GetDistanceToTarget() > PreferredRange * (1.f + 2.f * Difficulty.EngagementRangeTolerance))
		{
			return false;
		}
	}

	return true;
}

void AShooterAIController::EnterAction(EShooterBotAction NewAction, EShooterAILatch Latches, float MinCommit, float MaxDuration)
{
	// A retreat that is ending starts the cooldown, so the next selection has to pick something else.
	if (CurrentAction == EShooterBotAction::Retreat && NewAction != EShooterBotAction::Retreat)
	{
		RetreatSuppressedTimer = Difficulty.RetreatCooldown;
	}

	const bool bSameAction = (NewAction == CurrentAction);

	CurrentAction = NewAction;
	CurrentLatches = Latches;
	ActionMinCommit = MinCommit;
	ActionMaxDuration = MaxDuration;
	ActionElapsed = 0.f;

	if (NewAction == EShooterBotAction::Reposition || NewAction == EShooterBotAction::Strafe)
	{
		TimeSinceRepositionRoll = 0.f;
	}

	// The reposition arc belongs to exactly one action and must not leak into the next one.
	if (NewAction != EShooterBotAction::Reposition)
	{
		RepositionArcRemaining = 0.f;
		RepositionBiasDirection = FVector::ZeroVector;
	}

	switch (NewAction)
	{
	case EShooterBotAction::Idle:
		EndSteering();
		StopMoving();
		break;

	case EShooterBotAction::Hunt:
		// Crossing the arena genuinely is a pathfinding problem, so the hunt keeps its move goals.
		EndSteering();
		HuntRepathTimer = 0.f;
		break;

	case EShooterBotAction::Approach:
		// Mode is decided per tick in DriveApproach with hysteresis: steer once inside engagement range,
		// path when the target is across the map.
		ApproachRepathTimer = 0.f;
		SteerRecoveryTimer = 0.f;
		break;

	case EShooterBotAction::Strafe:
		// StrafeSign is deliberately NOT touched here. Flipping the side on entry is exactly what made the
		// old bot reverse direction every time the action churned out of and back into a fight. The side is
		// owned by the leg timer in DriveStrafe and by nothing else.
		//
		// No destination either: a strafe is continuous steering now, not a walk between survey markers.
		if (!bSameAction)
		{
			StrafeLegRemaining = FMath::FRandRange(
				FMath::Min(Difficulty.StrafeLegDurationMin, Difficulty.StrafeLegDurationMax),
				FMath::Max(Difficulty.StrafeLegDurationMin, Difficulty.StrafeLegDurationMax));
		}
		SteerRecoveryTimer = 0.f;
		BeginSteering();
		break;

	case EShooterBotAction::Reposition:
	{
		// An arc, not a waypoint. The ring search still does the thinking about *which way* is a better
		// angle, but its answer is consumed as a direction to lean rather than as a destination to walk to -
		// which is the difference between the bot swinging wide off the current angle and the bot teleporting
		// its intent to a marker 800 units away.
		RepositionArcRemaining = Difficulty.RepositionMaxDuration;
		RepositionBiasDirection = FVector::ZeroVector;

		FVector Destination;
		if (FindTacticalDestination(false, Destination) && IsValid(GetPawn()))
		{
			RepositionBiasDirection = (Destination - GetPawn()->GetActorLocation()).GetSafeNormal2D();
		}

		// A reposition is a deliberate change of angle, so this is the one place the strafe side flips
		// outside the leg timer - the whole point of the action is to stop orbiting the way it was.
		StrafeSign = -StrafeSign;
		StrafeLegRemaining = FMath::Max(StrafeLegRemaining, Difficulty.RepositionMaxDuration);

		SteerRecoveryTimer = 0.f;
		BeginSteering();
		break;
	}

	case EShooterBotAction::Retreat:
	{
		// Retreating means crossing to somewhere out of sight, which needs a route.
		EndSteering();

		FVector Destination;
		if (FindTacticalDestination(true, Destination))
		{
			RequestMoveTo(Destination);
		}
		break;
	}
	}
}

void AShooterAIController::DriveAction(float DeltaTime)
{
	switch (CurrentAction)
	{
	case EShooterBotAction::Hunt:       DriveHunt(DeltaTime);     break;
	case EShooterBotAction::Approach:   DriveApproach(DeltaTime); break;
	case EShooterBotAction::Strafe:     DriveStrafe(DeltaTime);   break;
	case EShooterBotAction::Reposition: DriveReposition(DeltaTime); break;
	case EShooterBotAction::Retreat:    DriveRetreat();           break;
	case EShooterBotAction::Idle:
	default:
		break;
	}
}

void AShooterAIController::DriveHunt(float DeltaTime)
{
	const APawn* Bot = GetPawn();
	if (!IsValid(Bot) || !IsValid(Knowledge)) return;

	HuntRepathTimer -= DeltaTime;

	// Re-target on arrival, on a timer, or whenever there is no goal at all. The timer and the no-goal case
	// are what stop the hunt from stalling: arrival alone is not enough, because the interesting failures are
	// exactly the ones where the bot never arrives.
	if (bHasMoveGoal && !HasReachedMoveGoal() && HuntRepathTimer > 0.f) return;

	HuntRepathTimer = HuntRepathInterval;

	// First choice: somewhere it could actually see the target from. That is a real search - moving to take
	// an angle rather than walking at a remembered spot.
	FVector Destination;
	if (FindTacticalDestination(false, Destination))
	{
		RequestMoveTo(Destination);
		return;
	}

	// Nothing on the ring can see the target. Head for the target itself rather than standing still - see the
	// note on bSearchTowardTargetWhenLost for why this small omniscience is a deliberate choice.
	if (bSearchTowardTargetWhenLost)
	{
		if (const APawn* Target = Knowledge->GetTargetPawn())
		{
			RequestMoveTo(Target->GetActorLocation());
			return;
		}
	}

	RequestMoveTo(Knowledge->GetLastKnownTargetLocation());
}

bool AShooterAIController::ShouldActionSteer() const
{
	if (!IsValid(Knowledge)) return false;
	if (!Knowledge->HasLineOfSight()) return false;

	// Hysteresis, so the bot does not swap locomotion models every time the range wobbles across one line.
	const float SteerRange = GetPreferredRange() * SteerRangeMultiplier;
	const float Threshold = bSteering ? SteerRange * 1.3f : SteerRange;

	return Knowledge->GetDistanceToTarget() <= Threshold;
}

void AShooterAIController::DriveApproach(float DeltaTime)
{
	if (!IsValid(GetPawn())) return;

	SteerRecoveryTimer = FMath::Max(0.f, SteerRecoveryTimer - DeltaTime);

	// A recovery path is in flight because steering wedged. Let it finish rather than immediately steering
	// back into the same corner.
	if (SteerRecoveryTimer > 0.f && bHasMoveGoal && !HasReachedMoveGoal()) return;

	if (ShouldActionSteer())
	{
		BeginSteering();
		UpdateSteering(DeltaTime);
		return;
	}

	// Too far to steer: this is a genuine pathfinding problem, so cross the map with a route.
	EndSteering();

	ApproachRepathTimer -= DeltaTime;

	// Re-pathed on an interval rather than every frame. Issuing MoveToLocation onto the target's live
	// position each tick restarts the path query constantly and produces a visibly stuttering route; a
	// committed route re-planned a few times a second reads as a bot running at you.
	if (ApproachRepathTimer > 0.f && bHasMoveGoal && !HasReachedMoveGoal()) return;

	ApproachRepathTimer = ApproachRepathInterval;
	RequestMoveTo(ComputeApproachDestination());
}

void AShooterAIController::DriveStrafe(float DeltaTime)
{
	if (!IsValid(GetPawn())) return;

	StrafeLegRemaining -= DeltaTime;
	SteerRecoveryTimer = FMath::Max(0.f, SteerRecoveryTimer - DeltaTime);

	// One leg, one commitment - but the leg now owns only the *side*, not a destination. A human circling an
	// opponent holds a direction and applies continuous input; they do not walk between survey markers, and
	// the old waypointed strafe is exactly what read as point-and-click.
	if (StrafeLegRemaining <= 0.f)
	{
		// Reversing on a timer rather than on a wall hit: unpredictable direction changes are what make the
		// bot hard to track, and a bot that only ever turns when it runs out of room is trivially readable.
		StrafeSign = -StrafeSign;

		StrafeLegRemaining = FMath::FRandRange(
			FMath::Min(Difficulty.StrafeLegDurationMin, Difficulty.StrafeLegDurationMax),
			FMath::Max(Difficulty.StrafeLegDurationMin, Difficulty.StrafeLegDurationMax));
	}

	if (SteerRecoveryTimer > 0.f && bHasMoveGoal && !HasReachedMoveGoal()) return;

	BeginSteering();
	UpdateSteering(DeltaTime);
}

void AShooterAIController::DriveReposition(float DeltaTime)
{
	if (!IsValid(GetPawn())) return;

	SteerRecoveryTimer = FMath::Max(0.f, SteerRecoveryTimer - DeltaTime);

	if (SteerRecoveryTimer > 0.f && bHasMoveGoal && !HasReachedMoveGoal()) return;

	// The arc's extra tangential weight is applied and decayed inside UpdateSteering. The action itself ends
	// on its own duration - there is no destination to arrive at any more.
	BeginSteering();
	UpdateSteering(DeltaTime);
}

void AShooterAIController::DriveRetreat()
{
	// Out of sight and standing somewhere the player is not: this is the moment to reload, which is the whole
	// point of retreating with a dry mag.
	if (!HasLineOfSight() && ShouldReloadNow())
	{
		if (UCombatComponent* Combat = GetCombat())
		{
			Combat->Initiate_ReloadWeapon();
		}
	}
}

/* --- Movement --- */

void AShooterAIController::RequestMoveTo(const FVector& Location)
{
	MoveGoal = Location;
	bHasMoveGoal = true;

	// Restarts the unreachable-goal clock. Every new goal gets a full MoveGoalTimeout to be reached.
	MoveGoalElapsed = 0.f;

	// bCanStrafe true is what keeps path following from rotating the pawn onto its route and stealing the
	// yaw the aim component is writing - i.e. it is what lets the bot strafe, retreat or wall run while
	// still facing the player.
	MoveToLocation(Location, GoalAcceptanceRadius, /*bStopOnOverlap*/ false, /*bUsePathfinding*/ true,
		/*bProjectDestinationToNavigation*/ true, /*bCanStrafe*/ true, nullptr, /*bAllowPartialPath*/ true);
}

void AShooterAIController::StopMoving()
{
	bHasMoveGoal = false;
	MoveGoalElapsed = 0.f;
	StopMovement();
}

/* --- Steering --- */

void AShooterAIController::BeginSteering()
{
	if (bSteering) return;

	bSteering = true;
	SteerBlockedTime = 0.f;

	// Path following and the steering layer both write AddMovementInput. Leaving a path running would mean
	// the pawn is steered by the sum of two disagreeing systems, which is worse than either alone.
	StopMoving();

	if (SteeringDirection.IsNearlyZero())
	{
		const APawn* Bot = GetPawn();
		SteeringDirection = IsValid(Bot) ? Bot->GetActorForwardVector().GetSafeNormal2D() : FVector::ForwardVector;
	}
}

void AShooterAIController::EndSteering()
{
	bSteering = false;
	SteerBlockedTime = 0.f;
}

void AShooterAIController::UpdateSteerNoise(float DeltaTime)
{
	SteerNoiseTimer -= DeltaTime;
	if (SteerNoiseTimer <= 0.f)
	{
		SteerNoiseTimer = FMath::FRandRange(
			FMath::Min(SteerNoiseIntervalMin, SteerNoiseIntervalMax),
			FMath::Max(SteerNoiseIntervalMin, SteerNoiseIntervalMax));
		SteerNoiseTarget = FMath::FRandRange(-1.f, 1.f);
	}

	// Interped rather than stepped, so the wander reads as a body drifting off its line rather than as a
	// direction change. A stepped value would look exactly like the waypoint snapping this replaces.
	SteerNoiseCurrent = FMath::FInterpTo(SteerNoiseCurrent, SteerNoiseTarget, DeltaTime, 2.f);
}

void AShooterAIController::UpdateSteering(float DeltaTime)
{
	APawn* Bot = GetPawn();
	if (!bSteering || !IsValid(Bot) || !IsValid(Knowledge)) return;

	const APawn* Target = Knowledge->GetTargetPawn();
	const FVector TargetLocation = IsValid(Target) ? Target->GetActorLocation() : Knowledge->GetLastKnownTargetLocation();

	const FVector Origin = Bot->GetActorLocation();
	FVector ToBot = Origin - TargetLocation;
	ToBot.Z = 0.f;

	const float Range = ToBot.Size();
	const FVector Radial = Range > KINDA_SMALL_NUMBER ? ToBot / Range : Bot->GetActorForwardVector().GetSafeNormal2D();
	const FVector Tangent = FVector::CrossProduct(FVector::UpVector, Radial) * static_cast<float>(StrafeSign);

	// --- Radial: a continuous correction proportional to how wrong the range is, not a destination at the
	// right range. Radial points from the target to the bot, so a positive weight backs off and a negative
	// one closes.
	const float PreferredRange = GetPreferredRange();
	const float RangeError = (PreferredRange - Range) / FMath::Max(PreferredRange, 1.f);
	const float RadialWeight = FMath::Clamp(RangeError * SteerRadialGain, -1.f, 1.f);

	// --- Tangential: orbit on the committed side.
	float TangentWeight = (CurrentAction == EShooterBotAction::Approach)
		? SteerTangentWeightApproach
		: SteerTangentWeightStrafe;

	FVector Desired = Radial * RadialWeight + Tangent * TangentWeight;

	// --- The reposition arc. A bounded burst of extra tangential weight plus a lean toward the cover
	// direction the ring search scored, both decaying across the arc - so it reads as the bot swinging wide
	// off the angle it was holding and then settling back into a normal orbit.
	if (RepositionArcRemaining > 0.f)
	{
		RepositionArcRemaining = FMath::Max(0.f, RepositionArcRemaining - DeltaTime);

		const float ArcAlpha = RepositionArcRemaining / FMath::Max(Difficulty.RepositionMaxDuration, KINDA_SMALL_NUMBER);
		Desired += Tangent * (SteerRepositionTangentBoost * ArcAlpha);
		Desired += RepositionBiasDirection * (SteerRepositionBiasWeight * ArcAlpha);
	}

	Desired = Desired.GetSafeNormal2D();
	if (Desired.IsNearlyZero())
	{
		Desired = Tangent;
	}

	// --- Wander, so the bot does not orbit like a turret platform on rails.
	UpdateSteerNoise(DeltaTime);
	Desired = Desired.RotateAngleAxis(SteerNoiseCurrent * SteerNoiseMaxDegrees, FVector::UpVector);

	// --- Deflect off geometry and onto navigable ground.
	bool bBlocked = false;
	Desired = ResolveSteeringObstacles(Desired, bBlocked);

	if (bBlocked)
	{
		SteerBlockedTime += DeltaTime;

		// The honest answer to the trade-off: steering cannot pathfind, so when it wedges on concave
		// geometry it hands the problem to the nav mesh for a bounded window rather than grinding a wall.
		if (SteerBlockedTime >= SteerBlockedRecoveryTime)
		{
			EndSteering();
			SteerRecoveryTimer = SteerRecoveryPathTime;
			RequestMoveTo(ComputeStrafeDestination());
			return;
		}
	}
	else
	{
		SteerBlockedTime = 0.f;
	}

	// Smoothed, so the applied direction has body inertia instead of snapping between frames.
	const FVector Smoothed = FMath::VInterpTo(SteeringDirection, Desired, DeltaTime, SteerTurnInterpSpeed).GetSafeNormal2D();
	SteeringDirection = Smoothed.IsNearlyZero() ? Desired : Smoothed;

	// World-space input, and the pawn's yaw is owned by the aim layer - so this is a strafe in the literal
	// sense: the bot moves sideways while still facing its target. AddMovementInput accumulates into the
	// pawn's ControlInputVector and is consumed by UCharacterMovementComponent::PerformMovement, so the
	// movement-tech layer's GetCurrentAcceleration reads this exactly as it read path following's input.
	Bot->AddMovementInput(SteeringDirection, 1.f);
}

FVector AShooterAIController::ResolveSteeringObstacles(const FVector& Desired, bool& bOutBlocked) const
{
	bOutBlocked = false;

	const AShooterCharacter* Bot = GetShooterPawn();
	if (!IsValid(Bot) || !IsValid(GetWorld())) return Desired;

	const FVector Origin = Bot->GetActorLocation();

	// --- Whiskers. Short traces ahead and to each side; a hit pushes the direction away along the surface
	// normal, weighted by how close it is. This is what makes the bot slide along a wall instead of pressing
	// into it, and it costs three line traces rather than a path query.
	FVector Repulsion = FVector::ZeroVector;

	for (int32 Index = 0; Index < 3; ++Index)
	{
		const float Angle = (Index == 0) ? 0.f : (Index == 1 ? SteerWhiskerAngleDegrees : -SteerWhiskerAngleDegrees);
		const float Length = (Index == 0) ? SteerWhiskerLength : SteerWhiskerLength * 0.7f;
		const FVector Direction = Desired.RotateAngleAxis(Angle, FVector::UpVector);

		FCollisionQueryParams Params(SCENE_QUERY_STAT(BotSteerWhisker), false, Bot);
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * Length, ECC_Visibility, Params))
		{
			continue;
		}

		FVector Normal = Hit.ImpactNormal;
		Normal.Z = 0.f;
		Normal = Normal.GetSafeNormal();
		if (Normal.IsNearlyZero()) continue;

		const float Closeness = 1.f - FMath::Clamp(Hit.Distance / FMath::Max(Length, 1.f), 0.f, 1.f);
		Repulsion += Normal * Closeness * SteerAvoidWeight;
	}

	FVector Adjusted = Desired;
	if (!Repulsion.IsNearlyZero())
	{
		const FVector Deflected = (Desired + Repulsion).GetSafeNormal2D();
		if (!Deflected.IsNearlyZero())
		{
			Adjusted = Deflected;
		}
	}

	// --- Nav containment. Whiskers stop the bot walking into things; this stops it walking off things, and
	// keeps a steered bot on ground a path would have used without ever asking for a path.
	UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!IsValid(NavSystem)) return Adjusted;

	const UCapsuleComponent* Capsule = Bot->GetCapsuleComponent();
	const float FootZ = Origin.Z - (IsValid(Capsule) ? Capsule->GetScaledCapsuleHalfHeight() : 88.f);

	// Widening deflections. The first that lands on the nav mesh at a sane height wins, so the bot rounds a
	// corner rather than stopping at it.
	const float Deflections[] = { 0.f, 35.f, -35.f, 70.f, -70.f, 110.f, -110.f };
	const FVector ProjectionExtent(60.f, 60.f, 150.f);

	for (const float Deflection : Deflections)
	{
		const FVector Candidate = Adjusted.RotateAngleAxis(Deflection, FVector::UpVector);
		const FVector Step(
			Origin.X + Candidate.X * SteerNavProbeDistance,
			Origin.Y + Candidate.Y * SteerNavProbeDistance,
			FootZ);

		FNavLocation Projected;
		if (!NavSystem->ProjectPointToNavigation(Step, Projected, ProjectionExtent)) continue;

		// Compared against the feet, not the actor origin: the actor origin sits a capsule half-height above
		// the floor, so comparing against it would report every single step as a drop.
		if (FMath::Abs(Projected.Location.Z - FootZ) > SteerNavMaxStepHeight) continue;

		return Candidate;
	}

	bOutBlocked = true;
	return Adjusted;
}

bool AShooterAIController::HasReachedMoveGoal() const
{
	if (!bHasMoveGoal) return false;

	const APawn* Bot = GetPawn();
	if (!IsValid(Bot)) return false;

	return FVector::Dist(Bot->GetActorLocation(), MoveGoal) <= GoalAcceptanceRadius;
}

FVector AShooterAIController::ComputeApproachDestination() const
{
	const APawn* Bot = GetPawn();
	if (!IsValid(Bot) || !IsValid(Knowledge)) return FVector::ZeroVector;

	const APawn* Target = Knowledge->GetTargetPawn();
	const FVector TargetLocation = IsValid(Target) ? Target->GetActorLocation() : Knowledge->GetLastKnownTargetLocation();

	FVector ToBot = Bot->GetActorLocation() - TargetLocation;
	ToBot.Z = 0.f;

	const float CurrentRange = ToBot.Size();
	if (CurrentRange <= KINDA_SMALL_NUMBER) return TargetLocation;

	// A point on the bot-to-target line at the preferred range. Deliberately not the target's own position:
	// the bot should stop where it can fight from, not run into the player's face.
	return TargetLocation + (ToBot / CurrentRange) * GetPreferredRange();
}

FVector AShooterAIController::ComputeStrafeDestination() const
{
	const APawn* Bot = GetPawn();
	if (!IsValid(Bot) || !IsValid(Knowledge)) return FVector::ZeroVector;

	const APawn* Target = Knowledge->GetTargetPawn();
	const FVector TargetLocation = IsValid(Target) ? Target->GetActorLocation() : Knowledge->GetLastKnownTargetLocation();

	FVector ToBot = Bot->GetActorLocation() - TargetLocation;
	ToBot.Z = 0.f;

	const float CurrentRange = ToBot.Size();
	const FVector RadialDirection = CurrentRange > KINDA_SMALL_NUMBER ? ToBot / CurrentRange : Bot->GetActorForwardVector();
	const FVector TangentDirection = FVector::CrossProduct(FVector::UpVector, RadialDirection) * static_cast<float>(StrafeSign);

	// Hold the preferred range on the radial axis while moving along the tangent: the bot circles rather than
	// walking straight at the player, which is both harder to hit and how the range trade-off actually gets
	// expressed in play.
	const float PreferredRange = GetPreferredRange();
	const float RadialCorrection = FMath::Clamp(PreferredRange - CurrentRange, -PreferredRange, PreferredRange);

	return TargetLocation
		+ RadialDirection * (CurrentRange + RadialCorrection)
		+ TangentDirection * FMath::Max(PreferredRange * 0.5f, 300.f);
}

bool AShooterAIController::FindTacticalDestination(bool bBreakLineOfSight, FVector& OutLocation) const
{
	const APawn* Bot = GetPawn();
	if (!IsValid(Bot) || !IsValid(GetWorld()) || !IsValid(Knowledge)) return false;

	UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!IsValid(NavSystem)) return false;

	const APawn* Target = Knowledge->GetTargetPawn();
	const FVector ReferencePoint = IsValid(Target) ? Target->GetActorLocation() : Knowledge->GetLastKnownTargetLocation();
	const FVector Origin = Bot->GetActorLocation();
	const float EyeOffset = GetEyeLocation().Z - Origin.Z;

	// Candidate points on a ring around the bot, then projected onto the nav mesh. Sampled by hand rather
	// than via a random-reachable-point query because the test that matters is not "is it reachable" but
	// "can I see the player from there" - and that has to be evaluated per candidate.
	constexpr int32 CandidateCount = 12;
	const float AngleOffset = FMath::FRand() * 2.f * PI;

	bool bFoundAny = false;
	float BestScore = -TNumericLimits<float>::Max();
	FVector BestLocation = Origin;

	for (int32 Index = 0; Index < CandidateCount; ++Index)
	{
		const float Angle = AngleOffset + (2.f * PI * static_cast<float>(Index)) / static_cast<float>(CandidateCount);
		const FVector Offset(FMath::Cos(Angle) * RepositionSearchRadius, FMath::Sin(Angle) * RepositionSearchRadius, 0.f);

		FNavLocation Projected;
		if (!NavSystem->ProjectPointToNavigation(Origin + Offset, Projected, FVector(200.f, 200.f, 400.f)))
		{
			continue;
		}

		const FVector CandidateEye = Projected.Location + FVector(0.f, 0.f, EyeOffset);

		// Would the bot be able to see the player from there? A retreat wants no, a reposition wants yes.
		bool bCandidateHasSight = false;
		if (IsValid(Target))
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(BotTacticalSight), false, Bot);
			Params.AddIgnoredActor(Target);
			FHitResult Hit;
			bCandidateHasSight = !GetWorld()->LineTraceSingleByChannel(
				Hit, CandidateEye, Target->GetPawnViewLocation(), ECC_Visibility, Params);
		}

		if (bBreakLineOfSight == bCandidateHasSight)
		{
			continue;
		}

		// Among the candidates that qualify, prefer the one that changes the angle most - measured as
		// distance from where the bot already is - and, when retreating, that also puts distance between it
		// and the player.
		float Score = FVector::Dist(Projected.Location, Origin);
		if (bBreakLineOfSight)
		{
			Score += FVector::Dist(Projected.Location, ReferencePoint);
		}

		if (Score > BestScore)
		{
			BestScore = Score;
			BestLocation = Projected.Location;
			bFoundAny = true;
		}
	}

	if (bFoundAny)
	{
		OutLocation = BestLocation;
	}
	return bFoundAny;
}

/* --- Weapon housekeeping --- */

bool AShooterAIController::ShouldReloadNow() const
{
	const UCombatComponent* Combat = GetCombat();
	if (!IsValid(Combat)) return false;

	const AWeapon* Weapon = Combat->CurrentWeapon;
	if (!IsValid(Weapon)) return false;
	if (Weapon->WeaponStatus != EWeaponStatus::Idle) return false;
	if (Combat->CurrentReserveAmmo <= 0) return false;

	const int32 Capacity = Weapon->GetEffectiveMagCapacity();
	if (Capacity <= 0) return false;
	if (Weapon->Ammo >= Capacity) return false;

	return static_cast<float>(Weapon->Ammo) / static_cast<float>(Capacity) <= Difficulty.ReloadAtAmmoFraction;
}

void AShooterAIController::UpdateWeaponHousekeeping(float DeltaTime)
{
	UCombatComponent* Combat = GetCombat();
	if (!IsValid(Combat)) return;

	const AWeapon* Weapon = Combat->CurrentWeapon;
	if (!IsValid(Weapon)) return;

	// Tactical reload: only ever out of sight, so the bot is never caught mid-animation in a duel it could
	// have been shooting in. A completely dry mag is handled by the Retreat action instead.
	if (!HasLineOfSight() && ShouldReloadNow())
	{
		Combat->Initiate_ReloadWeapon();
	}

	if (Weapon->WeaponStatus == EWeaponStatus::Reloading)
	{
		ReloadWatchdogTimer += DeltaTime;
		if (ReloadWatchdogTimer >= ReloadWatchdogTime)
		{
			CompleteReloadIfStalled();
			ReloadWatchdogTimer = 0.f;
		}
	}
	else
	{
		ReloadWatchdogTimer = 0.f;
	}
}

void AShooterAIController::CompleteReloadIfStalled()
{
	APawn* Bot = GetPawn();
	if (!IsValid(Bot) || !Bot->Implements<UPlayerInterface>()) return;

	const UCombatComponent* Combat = GetCombat();
	if (!IsValid(Combat) || !IsValid(Combat->CurrentWeapon)) return;
	if (Combat->CurrentWeapon->WeaponStatus != EWeaponStatus::Reloading) return;

	UE_LOG(LogTemp, Warning,
		TEXT("%s: reload watchdog fired - the third-person reload montage for %s appears to carry no reload notify. "
		     "Completing the reload from the controller instead."),
		*GetNameSafe(this), *Combat->CurrentWeapon->WeaponType.ToString());

	// Exactly the call the anim notify would have made, so an authored 3P notify and this path cannot
	// double-refill: whichever arrives first leaves the weapon Idle and the other is refused by the guard
	// above (and by Server_CompleteReload's own status check).
	IPlayerInterface::Execute_Notify_ReloadWeapon(Bot);
}

void AShooterAIController::OnPawnHealthChanged(UHealthComponent* HealthComponent, float OldValue, float NewValue,
	AActor* DamageInstigator)
{
	// Only damage interrupts. A heal or a max-health change is not a reason to abandon what you were doing.
	if (NewValue >= OldValue) return;

	if (IsValid(Knowledge))
	{
		// Not RaiseInterrupt directly: RegisterDamageFrom also rate limits the interrupt and folds the shot
		// into the knowledge block, which is what lets the bot turn onto an attacker it never saw.
		// UCombatComponent passes its own owning pawn as the instigator, so this is the shooter.
		Knowledge->RegisterDamageFrom(DamageInstigator);
	}
}

/* --- Debug --- */

void AShooterAIController::DrawDebug() const
{
	const APawn* Bot = GetPawn();
	if (!IsValid(Bot) || !IsValid(GetWorld()) || !IsValid(Knowledge)) return;

	const FVector Eye = GetEyeLocation();

	static const TCHAR* ActionNames[] = {
		TEXT("Idle"), TEXT("Hunt"), TEXT("Approach"), TEXT("Strafe"), TEXT("Reposition"), TEXT("Retreat") };
	static const TCHAR* InterruptNames[] = {
		TEXT("-"), TEXT("Damage"), TEXT("TgtLost"), TEXT("TgtAcq"), TEXT("Reload"), TEXT("HpCrit") };

	const int32 ActionIndex = static_cast<int32>(CurrentAction);
	const int32 InterruptIndex = static_cast<int32>(Knowledge->GetLastConsumedInterrupt());

	const float Stillness = Knowledge->GetTargetStillness();

	// One string with everything that decides what the bot is doing. The stillness readout is the important
	// one while tuning: it should visibly climb toward 1 within a beat of the player stopping, and collapse
	// the instant they slide or jump.
	DrawDebugString(GetWorld(), FVector(0.f, 0.f, 120.f),
		FString::Printf(TEXT("%s[%s] %.1fs/%.1fs | %s%s | still %.2f | int %s"),
			ActionIndex < UE_ARRAY_COUNT(ActionNames) ? ActionNames[ActionIndex] : TEXT("?"),
			bSteering ? TEXT("steer") : (bHasMoveGoal ? TEXT("path") : TEXT("-")),
			ActionElapsed,
			ActionMaxDuration,
			Knowledge->HasLineOfSight() ? TEXT("LOS") : TEXT("no LOS"),
			Knowledge->HasAcquiredTarget() ? TEXT("+acq") : TEXT(""),
			Stillness,
			InterruptIndex < UE_ARRAY_COUNT(InterruptNames) ? InterruptNames[InterruptIndex] : TEXT("?")),
		const_cast<APawn*>(Bot), FColor::White, 0.f, true);

	// Line of sight to the target: green when it can shoot, red when it is blind. Thickness scales with
	// stillness, so how hard the bot is currently locked on is readable at a glance from across the arena.
	if (const APawn* Target = Knowledge->GetTargetPawn())
	{
		DrawDebugLine(GetWorld(), Eye, Target->GetPawnViewLocation(),
			Knowledge->HasLineOfSight() ? FColor::Green : FColor::Red, false, -1.f, 0,
			1.f + Stillness * 5.f);
	}

	// Last known position - where the bot thinks the player is when it cannot see them.
	if (Knowledge->HasTarget())
	{
		DrawDebugSphere(GetWorld(), Knowledge->GetLastKnownTargetLocation(), 40.f, 12, FColor::Yellow, false, -1.f, 0, 1.f);
	}

	// The cyan goal sphere is drawn ONLY when the bot is genuinely pathing. It reads as the bot's intent, and
	// while steering there is no destination to read - drawing one there is what made the movement look like
	// point-and-click even on the frames where it was not.
	if (bHasMoveGoal && !bSteering)
	{
		DrawDebugSphere(GetWorld(), MoveGoal, 50.f, 12, FColor::Cyan, false, -1.f, 0, 1.f);
		DrawDebugLine(GetWorld(), Bot->GetActorLocation(), MoveGoal, FColor::Cyan, false, -1.f, 0, 1.f);
	}

	if (bSteering)
	{
		DrawSteeringDebug();
	}

	// Where the bot is actually pointing the gun, including its aim error. The gap between this and the
	// green LOS line is the bot's inaccuracy made visible - the single most useful thing to watch when
	// tuning difficulty.
	if (IsValid(AimLogic))
	{
		AimLogic->DrawAimDebug();
	}

	if (IsValid(MovementTech))
	{
		MovementTech->DrawTechDebug();
	}
}

void AShooterAIController::DrawSteeringDebug() const
{
	const AShooterCharacter* Bot = GetShooterPawn();
	if (!IsValid(Bot) || !IsValid(GetWorld())) return;

	const FVector Origin = Bot->GetActorLocation();

	// The steering vector replaces the goal sphere as "what the bot intends". Red once the blocked timer is
	// running, which is the cue that it is about to fall back to a path.
	const FColor Colour = SteerBlockedTime > 0.f ? FColor::Red : FColor::Emerald;
	DrawDebugDirectionalArrow(GetWorld(), Origin, Origin + SteeringDirection * 300.f, 45.f, Colour, false, -1.f, 0, 4.f);

	// The whiskers, so a bot that keeps deflecting has a visible reason.
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const float Angle = (Index == 0) ? 0.f : (Index == 1 ? SteerWhiskerAngleDegrees : -SteerWhiskerAngleDegrees);
		const float Length = (Index == 0) ? SteerWhiskerLength : SteerWhiskerLength * 0.7f;
		const FVector Direction = SteeringDirection.RotateAngleAxis(Angle, FVector::UpVector);

		FCollisionQueryParams Params(SCENE_QUERY_STAT(BotSteerWhiskerDebug), false, Bot);
		FHitResult Hit;
		const bool bHit = GetWorld()->LineTraceSingleByChannel(
			Hit, Origin, Origin + Direction * Length, ECC_Visibility, Params);

		DrawDebugLine(GetWorld(), Origin, Origin + Direction * Length,
			bHit ? FColor::Orange : FColor::Blue, false, -1.f, 0, 1.f);
	}
}
