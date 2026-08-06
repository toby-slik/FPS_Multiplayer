// Copyright Druid Mechanics


#include "AI/ShooterAIController.h"

#include "AI/ShooterAIAimComponent.h"
#include "AI/ShooterAIMovementTechComponent.h"
#include "Character/ShooterCharacter.h"
#include "Character/ShooterMovementComponent.h"
#include "Combat/CombatComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Health/HealthComponent.h"
#include "Interfaces/PlayerInterface.h"
#include "NavigationSystem.h"
#include "Weapon/Weapon.h"

AShooterAIController::AShooterAIController()
{
	PrimaryActorTick.bCanEverTick = true;

	// The bot writes its own control rotation every frame from the aim component. Left at its default this
	// would overwrite that with the pawn's current orientation, and the bot would never turn onto a target.
	bSetControlRotationFromPawnOrientation = false;

	// Path following must not rotate the pawn toward its route: the bot has to be able to face its target
	// while moving sideways, exactly as a strafing player does. The one time facing along the route matters
	// is a wall run, and the movement tech component claims the yaw explicitly for that.
	bAllowStrafe = true;

	AimLogic = CreateDefaultSubobject<UShooterAIAimComponent>("AimLogic");
	MovementTech = CreateDefaultSubobject<UShooterAIMovementTechComponent>("MovementTech");

	Skill = EShooterBotSkill::Regular;

	SightRange = 8000.f;
	SightHalfAngleDegrees = 80.f;
	HearingRange = 4000.f;
	TargetRefreshInterval = 0.5f;
	RepositionSearchRadius = 1400.f;
	GoalAcceptanceRadius = 120.f;
	ReloadWatchdogTime = 3.f;
	HuntRepathInterval = 2.5f;
	MoveGoalTimeout = 8.f;
	bSearchTowardTargetWhenLost = true;
	bDebugDrawAI = false;

	BotState = EShooterBotState::Idle;
	bHasLineOfSight = false;
	bTargetAcquired = false;
	LineOfSightHeldTime = 0.f;
	TimeSinceLineOfSight = TNumericLimits<float>::Max();
	TargetRefreshTimer = 0.f;
	LastKnownTargetLocation = FVector::ZeroVector;
	LastKnownTargetVelocity = FVector::ZeroVector;
	DecisionTimer = 0.f;
	StateTime = 0.f;
	MoveGoal = FVector::ZeroVector;
	bHasMoveGoal = false;
	MoveGoalElapsed = 0.f;
	HuntRepathTimer = 0.f;
	StrafeTimer = 0.f;
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

	// Ticked by hand from this class so the order within a frame is fixed: perception, then the state
	// machine, then movement tech, then aim. Aim runs last because a wall-run attempt has to be able to
	// claim the yaw before aim writes the control rotation.
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
	BotState = EShooterBotState::Idle;
	StateTime = 0.f;
	DecisionTimer = 0.f;
	bHasLineOfSight = false;
	bTargetAcquired = false;
	LineOfSightHeldTime = 0.f;
	TimeSinceLineOfSight = TNumericLimits<float>::Max();
	bHasMoveGoal = false;
	MoveGoalElapsed = 0.f;
	HuntRepathTimer = 0.f;
	ReloadWatchdogTimer = 0.f;
	TargetPawn = nullptr;

	if (IsValid(MovementTech))
	{
		MovementTech->ResetTech();
	}
	if (IsValid(AimLogic))
	{
		AimLogic->HoldFire();
	}
}

void AShooterAIController::OnUnPossess()
{
	// The trigger is a latched bool on UCombatComponent. Losing the pawn without dropping it would leave a
	// dead bot's combat component believing the trigger is still held.
	if (IsValid(AimLogic))
	{
		AimLogic->HoldFire();
	}
	if (IsValid(MovementTech))
	{
		MovementTech->ResetTech();
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
		}
		StopMoving();
		return;
	}

	UpdatePerception(DeltaTime);
	UpdateStateMachine(DeltaTime);
	UpdateWeaponHousekeeping(DeltaTime);

	if (IsValid(MovementTech))
	{
		MovementTech->TickMovementTech(DeltaTime);
	}

	if (IsValid(AimLogic))
	{
		// The yaw lock has to be handed over before aim runs, not after, or the wall-run attach test sees
		// last frame's facing.
		AimLogic->SetYawLockToTravel(
			IsValid(MovementTech) && MovementTech->WantsYawLockedToTravel(),
			IsValid(MovementTech) ? MovementTech->GetTravelDirection() : FVector::ZeroVector);

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

float AShooterAIController::HealthFraction() const
{
	if (const UHealthComponent* BotHealth = UHealthComponent::FindHealthComponent(GetPawn()))
	{
		return BotHealth->GetHealthNormalized();
	}
	return 1.f;
}

/* --- Perception --- */

void AShooterAIController::RefreshTarget()
{
	// A 1v1 game has exactly one opponent, so the player controller list *is* the candidate list. Iterating
	// it rather than running a perception component's stimuli queries is both cheaper and simpler, and it
	// cannot pick up a stray pawn that happens to implement the interface.
	APawn* Best = nullptr;
	float BestDistanceSquared = TNumericLimits<float>::Max();

	const APawn* Bot = GetPawn();
	if (!IsValid(Bot) || !IsValid(GetWorld())) return;

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!IsValid(PC)) continue;

		APawn* Candidate = PC->GetPawn();
		if (!IsValid(Candidate) || Candidate == Bot) continue;
		if (!Candidate->Implements<UPlayerInterface>()) continue;
		if (!IPlayerInterface::Execute_IsAlive(Candidate)) continue;

		const float DistanceSquared = FVector::DistSquared(Candidate->GetActorLocation(), Bot->GetActorLocation());
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			Best = Candidate;
		}
	}

	// Only overwritten when a live candidate exists, so losing sight of a target does not erase it - that is
	// what TargetMemoryTime is for.
	if (IsValid(Best))
	{
		TargetPawn = Best;
	}
	else if (TargetPawn.IsValid() && !IPlayerInterface::Execute_IsAlive(TargetPawn.Get()))
	{
		TargetPawn = nullptr;
	}
}

bool AShooterAIController::TraceLineOfSight(const APawn* Target) const
{
	if (!IsValid(Target) || !IsValid(GetWorld())) return false;

	const FVector Start = GetEyeLocation();
	const FVector End = Target->GetPawnViewLocation();

	if (FVector::DistSquared(Start, End) > FMath::Square(SightRange)) return false;

	// Vision cone. Taken from the control rotation rather than from the capsule so it matches where the bot
	// is actually looking, which is what makes flanking it work.
	const FVector ToTarget = (End - Start).GetSafeNormal();
	const FVector Facing = GetControlRotation().Vector();
	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(SightHalfAngleDegrees));
	if ((ToTarget | Facing) < CosHalfAngle) return false;

	// Visibility channel on purpose: both the Pawn capsule profile and the CharacterMesh profile ignore it,
	// so a clear shot returns *no blocking hit* and only world geometry can occlude. Tracing on a channel
	// pawns block would report the target's own capsule as the obstruction.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BotLineOfSight), false, GetPawn());
	Params.AddIgnoredActor(Target);

	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
}

void AShooterAIController::UpdatePerception(float DeltaTime)
{
	TargetRefreshTimer -= DeltaTime;
	if (TargetRefreshTimer <= 0.f)
	{
		TargetRefreshTimer = TargetRefreshInterval;
		RefreshTarget();
	}

	APawn* Target = TargetPawn.Get();
	if (!IsValid(Target))
	{
		bHasLineOfSight = false;
		bTargetAcquired = false;
		LineOfSightHeldTime = 0.f;
		return;
	}

	bHasLineOfSight = TraceLineOfSight(Target);

	if (bHasLineOfSight)
	{
		LineOfSightHeldTime += DeltaTime;
		TimeSinceLineOfSight = 0.f;
		LastKnownTargetLocation = Target->GetActorLocation();
		LastKnownTargetVelocity = Target->GetVelocity();
	}
	else
	{
		LineOfSightHeldTime = 0.f;
		TimeSinceLineOfSight += DeltaTime;

		// Being shot at from outside the vision cone. A fired weapon is the loudest tell in the game, and a
		// bot that can be freely shot in the back reads as broken rather than as easy - so this hands it the
		// shooter's position without granting sight. It still has to turn, acquire and settle before it can
		// shoot back, which is where its difficulty is actually priced.
		if (HearingRange > 0.f && Target->Implements<UPlayerInterface>())
		{
			if (const AWeapon* TargetWeapon = IPlayerInterface::Execute_GetCurrentWeapon(Target))
			{
				const bool bInEarshot = FVector::DistSquared(Target->GetActorLocation(), GetPawn()->GetActorLocation())
					<= FMath::Square(HearingRange);

				if (bInEarshot && TargetWeapon->WeaponStatus == EWeaponStatus::Firing)
				{
					LastKnownTargetLocation = Target->GetActorLocation();
					LastKnownTargetVelocity = Target->GetVelocity();
					TimeSinceLineOfSight = 0.f;
				}
			}
		}
	}

	// The reaction delay. Everything downstream reads bTargetAcquired, never bHasLineOfSight, so this single
	// comparison is the whole of the bot's reaction time.
	bTargetAcquired = bHasLineOfSight && LineOfSightHeldTime >= Difficulty.ReactionTime;
}

/* --- State machine --- */

void AShooterAIController::UpdateStateMachine(float DeltaTime)
{
	StateTime += DeltaTime;
	DecisionTimer -= DeltaTime;

	// Abandon a move goal the bot is never going to reach, and force an immediate re-decision. See the note on
	// MoveGoalTimeout: without this, any state that guards on "I still have an unreached goal" can latch
	// forever, and a stationary bot is the worst-looking failure this system has.
	if (bHasMoveGoal)
	{
		MoveGoalElapsed += DeltaTime;
		if (MoveGoalElapsed >= MoveGoalTimeout)
		{
			bHasMoveGoal = false;
			MoveGoalElapsed = 0.f;
			StopMovement();
			DecisionTimer = 0.f;
		}
	}

	// Re-decided on an interval rather than per frame. At the boundary between two states - the preferred
	// range, the retreat health threshold - a per-frame decision oscillates and the bot reads as indecisive.
	if (DecisionTimer <= 0.f)
	{
		DecisionTimer = Difficulty.DecisionInterval;

		const EShooterBotState Decided = DecideState();
		if (Decided != BotState)
		{
			EnterState(Decided);
		}
	}

	DriveState(DeltaTime);
}

EShooterBotState AShooterAIController::DecideState() const
{
	const APawn* Bot = GetPawn();
	const APawn* Target = TargetPawn.Get();
	if (!IsValid(Bot) || !IsValid(Target)) return EShooterBotState::Idle;

	const UCombatComponent* Combat = GetCombat();
	const AWeapon* Weapon = IsValid(Combat) ? Combat->CurrentWeapon : nullptr;

	const bool bDryMag = IsValid(Weapon) && Weapon->Ammo <= 0 && IsValid(Combat) && Combat->CurrentReserveAmmo > 0;

	// Break off, in priority order. Low health and an empty gun are both reasons to stop trading and go
	// somewhere the player is not - and they are checked before engagement so a losing fight is left rather
	// than finished.
	if (HealthFraction() <= Difficulty.RetreatHealthFraction) return EShooterBotState::Retreat;
	if (bDryMag) return EShooterBotState::Retreat;

	if (bHasLineOfSight)
	{
		// Already committed to crossing to a new angle - let it finish rather than snapping back to Engage
		// the instant sight is regained, which would leave the bot jittering on the spot.
		if (BotState == EShooterBotState::Reposition && bHasMoveGoal &&
			FVector::Dist(Bot->GetActorLocation(), MoveGoal) > GoalAcceptanceRadius)
		{
			return EShooterBotState::Reposition;
		}

		// Occasionally refuse a winnable trade and move instead. This is what stops the bot from standing in
		// one doorway for a whole duel, and it is the cheapest single behaviour that makes it feel like it
		// has a plan.
		if (BotState == EShooterBotState::Engage && FMath::FRand() < Difficulty.RepositionChance)
		{
			return EShooterBotState::Reposition;
		}

		return EShooterBotState::Engage;
	}

	// Sight lost, but a live opponent exists, so the bot never stops looking.
	//
	// This deliberately does NOT fall through to Idle once TargetMemoryTime expires. It used to, and the
	// result in PIE was a bot that lost sight once, hunted, timed out, and then stood in a corner
	// permanently - Idle issues no move order, and Engage is the only state that fires, so it was parked for
	// the rest of the match. In a 1v1 there is exactly one opponent and giving up on them is never the right
	// answer. TargetMemoryTime still governs what the *aim* does with a stale position; it no longer governs
	// whether the bot keeps searching.
	return EShooterBotState::Hunt;
}

void AShooterAIController::EnterState(EShooterBotState NewState)
{
	BotState = NewState;
	StateTime = 0.f;

	// The trigger is latched on UCombatComponent, so every state transition drops it explicitly. Leaving it
	// held into a Retreat is how a bot ends up firing at a wall while running away.
	if (IsValid(AimLogic) && NewState != EShooterBotState::Engage)
	{
		AimLogic->HoldFire();
	}

	switch (NewState)
	{
	case EShooterBotState::Idle:
		StopMoving();
		break;

	case EShooterBotState::Hunt:
		RequestMoveTo(LastKnownTargetLocation);
		break;

	case EShooterBotState::Engage:
		// Destination is chosen per frame in DriveEngage from the strafe timer; nothing to set up here.
		StrafeTimer = 0.f;
		break;

	case EShooterBotState::Reposition:
	{
		FVector Destination;
		// A reposition wants a *different* angle it can still shoot from, so it asks for a point with line of
		// sight rather than one without.
		if (FindTacticalDestination(false, Destination))
		{
			RequestMoveTo(Destination);
		}
		else
		{
			// No usable angle found - fall back to fighting from here rather than standing still. Re-entered
			// properly rather than by assigning BotState, so Engage's own setup runs. Engage never routes back
			// to Reposition from its entry, so this cannot recurse.
			EnterState(EShooterBotState::Engage);
		}
		break;
	}

	case EShooterBotState::Retreat:
	{
		FVector Destination;
		if (FindTacticalDestination(true, Destination))
		{
			RequestMoveTo(Destination);
		}
		break;
	}
	}
}

void AShooterAIController::DriveState(float DeltaTime)
{
	switch (BotState)
	{
	case EShooterBotState::Engage:    DriveEngage(DeltaTime); break;
	case EShooterBotState::Hunt:      DriveHunt(DeltaTime);    break;
	case EShooterBotState::Reposition:DriveReposition();      break;
	case EShooterBotState::Retreat:   DriveRetreat();         break;
	case EShooterBotState::Idle:
	default:
		break;
	}
}

void AShooterAIController::DriveEngage(float DeltaTime)
{
	const APawn* Target = TargetPawn.Get();
	const APawn* Bot = GetPawn();
	if (!IsValid(Target) || !IsValid(Bot)) return;

	StrafeTimer -= DeltaTime;
	if (StrafeTimer <= 0.f)
	{
		StrafeTimer = FMath::FRandRange(
			FMath::Min(Difficulty.StrafeChangeIntervalMin, Difficulty.StrafeChangeIntervalMax),
			FMath::Max(Difficulty.StrafeChangeIntervalMin, Difficulty.StrafeChangeIntervalMax));

		// Reversing on a timer rather than on a wall hit: unpredictable direction changes are what make the
		// bot hard to track, and a bot that only ever turns when it runs out of room is trivially readable.
		StrafeSign = -StrafeSign;
		RequestMoveTo(ComputeStrafeDestination());
	}
	else if (bHasMoveGoal && FVector::Dist(Bot->GetActorLocation(), MoveGoal) <= GoalAcceptanceRadius)
	{
		// Arrived early - pick the next leg immediately so the bot never stands still in a firefight.
		RequestMoveTo(ComputeStrafeDestination());
	}
}

void AShooterAIController::DriveHunt(float DeltaTime)
{
	const APawn* Bot = GetPawn();
	if (!IsValid(Bot)) return;

	HuntRepathTimer -= DeltaTime;

	const bool bArrived = bHasMoveGoal && FVector::Dist(Bot->GetActorLocation(), MoveGoal) <= GoalAcceptanceRadius;

	// Re-target on arrival, on a timer, or whenever there is no goal at all. The timer and the no-goal case
	// are what stop the hunt from stalling: arrival alone is not enough, because the interesting failures are
	// exactly the ones where the bot never arrives.
	if (!bArrived && bHasMoveGoal && HuntRepathTimer > 0.f) return;

	HuntRepathTimer = HuntRepathInterval;

	// First choice: somewhere it could actually see the target from. That is a real search - moving to take an
	// angle rather than walking at a remembered spot.
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
		if (const APawn* Target = TargetPawn.Get())
		{
			RequestMoveTo(Target->GetActorLocation());
			return;
		}
	}

	// Last resort: the remembered position. Only useful while it is still fresh, but better than nothing.
	RequestMoveTo(LastKnownTargetLocation);
}

void AShooterAIController::DriveReposition()
{
	const APawn* Bot = GetPawn();
	if (!IsValid(Bot)) return;

	if (bHasMoveGoal && FVector::Dist(Bot->GetActorLocation(), MoveGoal) <= GoalAcceptanceRadius)
	{
		// Arrived. Next decision tick will send it back to Engage; clearing the goal here is what lets
		// DecideState's "let a reposition finish" guard fall through.
		bHasMoveGoal = false;
	}
}

void AShooterAIController::DriveRetreat()
{
	const APawn* Bot = GetPawn();
	if (!IsValid(Bot)) return;

	// Out of sight and standing somewhere the player is not: this is the moment to reload, which is the
	// whole point of retreating with a dry mag.
	if (!bHasLineOfSight && ShouldReloadNow())
	{
		if (UCombatComponent* Combat = GetCombat())
		{
			Combat->Initiate_ReloadWeapon();
		}
	}

	if (bHasMoveGoal && FVector::Dist(Bot->GetActorLocation(), MoveGoal) <= GoalAcceptanceRadius)
	{
		bHasMoveGoal = false;
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
	// yaw the aim component is writing.
	MoveToLocation(Location, GoalAcceptanceRadius, /*bStopOnOverlap*/ false, /*bUsePathfinding*/ true,
		/*bProjectDestinationToNavigation*/ true, /*bCanStrafe*/ true, nullptr, /*bAllowPartialPath*/ true);
}

void AShooterAIController::StopMoving()
{
	bHasMoveGoal = false;
	MoveGoalElapsed = 0.f;
	StopMovement();
}

FVector AShooterAIController::ComputeStrafeDestination() const
{
	const APawn* Target = TargetPawn.Get();
	const APawn* Bot = GetPawn();
	if (!IsValid(Target) || !IsValid(Bot)) return IsValid(Bot) ? Bot->GetActorLocation() : FVector::ZeroVector;

	const FVector TargetLocation = Target->GetActorLocation();
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
	if (!IsValid(Bot) || !IsValid(GetWorld())) return false;

	UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!IsValid(NavSystem)) return false;

	const APawn* Target = TargetPawn.Get();
	const FVector ReferencePoint = IsValid(Target) ? Target->GetActorLocation() : LastKnownTargetLocation;
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
	// have been shooting in. A completely dry mag is handled by the Retreat state instead.
	if (!bHasLineOfSight && ShouldReloadNow())
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

/* --- Debug --- */

void AShooterAIController::DrawDebug() const
{
	const APawn* Bot = GetPawn();
	if (!IsValid(Bot) || !IsValid(GetWorld())) return;

	const FVector Eye = GetEyeLocation();

	static const TCHAR* StateNames[] = { TEXT("Idle"), TEXT("Hunt"), TEXT("Engage"), TEXT("Reposition"), TEXT("Retreat") };
	const int32 StateIndex = static_cast<int32>(BotState);

	DrawDebugString(GetWorld(), FVector(0.f, 0.f, 110.f),
		FString::Printf(TEXT("%s | %s%s"),
			StateIndex < UE_ARRAY_COUNT(StateNames) ? StateNames[StateIndex] : TEXT("?"),
			bHasLineOfSight ? TEXT("LOS") : TEXT("no LOS"),
			bTargetAcquired ? TEXT(" | acquired") : TEXT("")),
		const_cast<APawn*>(Bot), FColor::White, 0.f, true);

	// Line of sight to the target: green when it can shoot, red when it is blind.
	if (const APawn* Target = TargetPawn.Get())
	{
		DrawDebugLine(GetWorld(), Eye, Target->GetPawnViewLocation(),
			bHasLineOfSight ? FColor::Green : FColor::Red, false, -1.f, 0, 1.5f);
	}

	// Last known position - where the bot thinks the player is when it cannot see them.
	if (TargetPawn.IsValid())
	{
		DrawDebugSphere(GetWorld(), LastKnownTargetLocation, 40.f, 12, FColor::Yellow, false, -1.f, 0, 1.f);
	}

	// Current move goal and the straight line to it. The real path is whatever the nav mesh chose; this is
	// the intent, which is the part worth seeing.
	if (bHasMoveGoal)
	{
		DrawDebugSphere(GetWorld(), MoveGoal, 50.f, 12, FColor::Cyan, false, -1.f, 0, 1.f);
		DrawDebugLine(GetWorld(), Bot->GetActorLocation(), MoveGoal, FColor::Cyan, false, -1.f, 0, 1.f);
	}

	// Where the bot is actually pointing the gun, including its aim error. The gap between this and the
	// green LOS line is the bot's inaccuracy made visible - the single most useful thing to watch when
	// tuning difficulty.
	if (IsValid(AimLogic))
	{
		const FVector AimPoint = AimLogic->GetDebugAimPoint();
		if (!AimPoint.IsNearlyZero())
		{
			DrawDebugLine(GetWorld(), Eye, AimPoint, FColor::Magenta, false, -1.f, 0, 1.f);
			DrawDebugPoint(GetWorld(), AimPoint, 12.f, FColor::Magenta, false, -1.f);
		}
	}
}
