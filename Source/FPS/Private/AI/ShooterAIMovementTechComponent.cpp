// Copyright Druid Mechanics


#include "AI/ShooterAIMovementTechComponent.h"

#include "AI/ShooterAIBlackboard.h"
#include "AI/ShooterAIController.h"
#include "Character/ShooterCharacter.h"
#include "Character/ShooterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

UShooterAIMovementTechComponent::UShooterAIMovementTechComponent()
{
	// Ticked by hand from AShooterAIController::Tick, before the aim component, so a traversal yaw claim is
	// raised before aim writes the control rotation for the frame.
	PrimaryComponentTick.bCanEverTick = false;

	SprintMinGoalDistance = 500.f;
	SprintMinHoldTime = 0.4f;

	SlideAttemptMinSpeed = 550.f;
	SlideApproachDistanceMin = 250.f;
	SlideApproachDistanceMax = 900.f;
	bSlideToEvade = true;
	SlideSprintPrimeTime = 0.45f;

	bAllowWallRun = true;
	// Just inside the character's WallRunTraceDistance of 75 - see the note on the property.
	WallProbeDistance = 68.f;
	WallRunAttemptMinSpeed = 450.f;
	WallProbeMaxNormalZ = 0.25f;
	WallProbeMaxTravelDot = 0.5f;
	WallRunLineUpDot = 0.85f;
	WallRunLineUpMaxTime = 0.6f;
	WallRunCommitTime = 0.7f;
	WallRunAttemptInterval = 2.f;
	WallJumpAfterTime = 0.55f;
	WallRunMinGoalDistance = 400.f;

	bJumpDodge = true;
	StuckTimeBeforeJump = 0.4f;

	TechAction = EShooterTechAction::None;
	TechPhaseTime = 0.f;
	WallRunAttemptTimer = 0.f;
	bWallJumpPlanned = false;
	bWallOnRight = true;

	bSprintIntent = false;
	SprintHoldTime = 0.f;
	bEvadeSprintActive = false;
	EvadeSprintTimer = 0.f;

	SlideCheckTimer = 0.f;
	JumpDodgeTimer = 0.f;
	StuckTimer = 0.f;
	bJumpPressedLastTick = false;
}

AShooterAIController* UShooterAIMovementTechComponent::GetAIController() const
{
	return Cast<AShooterAIController>(GetOwner());
}

AShooterCharacter* UShooterAIMovementTechComponent::GetShooterPawn() const
{
	const AShooterAIController* AI = GetAIController();
	return IsValid(AI) ? AI->GetShooterPawn() : nullptr;
}

UShooterAIBlackboard* UShooterAIMovementTechComponent::GetBlackboard() const
{
	const AShooterAIController* AI = GetAIController();
	return IsValid(AI) ? AI->GetKnowledge() : nullptr;
}

UShooterMovementComponent* UShooterAIMovementTechComponent::GetShooterMovement() const
{
	const AShooterAIController* AI = GetAIController();
	return IsValid(AI) ? AI->GetShooterMovement() : nullptr;
}

FVector UShooterAIMovementTechComponent::GetTravelDirection() const
{
	const UShooterMovementComponent* MoveComp = GetShooterMovement();
	if (!IsValid(MoveComp)) return FVector::ZeroVector;

	// Path following calls AddMovementInput every tick, so the current acceleration IS the direction the bot
	// is being steered. Preferred over velocity because it leads the turn - velocity still points along the
	// old heading for a moment after the route changes, and committing to a wall run off a stale heading is
	// how a bot ends up jumping at a wall it has already passed.
	const FVector InputDirection = MoveComp->GetCurrentAcceleration().GetSafeNormal2D();
	if (!InputDirection.IsNearlyZero())
	{
		return InputDirection;
	}

	return MoveComp->Velocity.GetSafeNormal2D();
}

void UShooterAIMovementTechComponent::ResetTech()
{
	if (UShooterMovementComponent* MoveComp = GetShooterMovement())
	{
		MoveComp->SetWantsToSprint(false);
	}

	if (AShooterAIController* AI = GetAIController())
	{
		AI->ReleaseTravelYawClaim();
		AI->SetFireSuppressed(false);
	}

	TechAction = EShooterTechAction::None;
	TechPhaseTime = 0.f;
	WallRunAttemptTimer = 0.f;
	bWallJumpPlanned = false;

	bSprintIntent = false;
	SprintHoldTime = 0.f;
	bEvadeSprintActive = false;
	EvadeSprintTimer = 0.f;

	SlideCheckTimer = 0.f;
	JumpDodgeTimer = 0.f;
	StuckTimer = 0.f;
}

bool UShooterAIMovementTechComponent::RollTechChance() const
{
	const AShooterAIController* AI = GetAIController();
	if (!IsValid(AI)) return false;

	return FMath::FRand() < AI->GetDifficulty().MovementTechChance;
}

void UShooterAIMovementTechComponent::TickMovementTech(float DeltaTime)
{
	if (!IsValid(GetShooterPawn()) || !IsValid(GetShooterMovement())) return;

	ReleaseJumpIfPending();

	// Wall run first: it is the only tech that claims the yaw, and both the slide and the jump layers have to
	// know to stay out of its way this frame.
	UpdateWallRun(DeltaTime);
	UpdateSprint(DeltaTime);
	UpdateSlide(DeltaTime);
	UpdateJump(DeltaTime);
}

/* --- Sprint --- */

void UShooterAIMovementTechComponent::UpdateSprint(float DeltaTime)
{
	const AShooterAIController* AI = GetAIController();
	UShooterMovementComponent* MoveComp = GetShooterMovement();
	const AShooterCharacter* Bot = GetShooterPawn();
	if (!IsValid(AI) || !IsValid(MoveComp) || !IsValid(Bot)) return;

	SprintHoldTime += DeltaTime;

	// The fire path calls IPlayerInterface::CancelSprint, which clears bWantsToSprint underneath this
	// component. Treat that as the authority rather than re-asserting on the next frame.
	//
	// This is not cosmetic. UCombatComponent::Local_FireWeapon returns early on IsOwnerSprinting(), so a bot
	// that re-asserts sprint the frame after the trigger cancelled it presses the trigger and drops every
	// round - for as long as the hold window lasts. Gating only the intent *flip* and not the *write* left
	// exactly that hole open.
	//
	// Not applied during an evasive slide prime: the burst is a deliberate, bounded claim on the sprint flag,
	// and the trigger is suppressed for its duration so nothing is competing for it anyway.
	if (bSprintIntent && !MoveComp->WantsToSprint() && !bEvadeSprintActive)
	{
		bSprintIntent = false;
		SprintHoldTime = 0.f;
		return;
	}

	// Sprinting blocks firing. That single rule is what makes this decision tactical rather than cosmetic:
	// the bot has to choose between covering ground and being able to shoot, exactly as the player does. So
	// anything it could shoot at right now beats any distance it might close.
	//
	// Read off WantsToShoot rather than off the tactical action, because the trigger is no longer tied to the
	// action - the aim layer will take a shot during an approach.
	//
	// Only ever true while *pathing*. A steering bot is fighting at close range, where the trigger is worth
	// more than the speed; the one exception is the sprint-to-slide below, which buys its own.
	bool bWantSprint =
		!AI->WantsToShoot() &&
		AI->HasMoveGoal() &&
		AI->GetDistanceToMoveGoal() > SprintMinGoalDistance &&
		TechAction != EShooterTechAction::WallRunActive;

	// An evasive slide has to reach SlideMinStartSpeed first, and it outranks the shooting veto - that is
	// exactly the trade a player makes when they sprint-cancel into a slide mid-duel. Bounded by
	// SlideSprintPrimeTime so it can never become a bot that just runs around not shooting.
	if (bEvadeSprintActive)
	{
		bWantSprint = true;
	}

	// Committed for at least SprintMinHoldTime, and - crucially - the write is inside the same gate as the
	// flip, so the flag is only ever touched on a deliberate change of mind.
	if (bWantSprint != bSprintIntent && (SprintHoldTime >= SprintMinHoldTime || bEvadeSprintActive))
	{
		bSprintIntent = bWantSprint;
		SprintHoldTime = 0.f;
		MoveComp->SetWantsToSprint(bSprintIntent);
	}
}

/* --- Slide --- */

void UShooterAIMovementTechComponent::UpdateSlide(float DeltaTime)
{
	AShooterAIController* AI = GetAIController();
	UShooterMovementComponent* MoveComp = GetShooterMovement();
	if (!IsValid(AI) || !IsValid(MoveComp)) return;

	// --- Sprint-to-slide burst, handled ahead of the throttle because it has to run every frame.
	//
	// A slide needs sprint (CanStartSlide gates on bSprinting) and sprint blocks firing, so a bot fighting at
	// close range can never slide by accident - it has to *choose* to stop shooting for a beat and buy the
	// speed. That is the same commitment a player makes, and expressing it as a short bounded burst is what
	// keeps in-fight slides available now that a steering bot has no move goal to sprint toward.
	if (bEvadeSprintActive)
	{
		EvadeSprintTimer -= DeltaTime;

		const bool bReady =
			MoveComp->IsMovingOnGround() &&
			MoveComp->IsSprinting() &&
			MoveComp->Velocity.Size2D() >= SlideAttemptMinSpeed;

		if (bReady)
		{
			// Intent only. CanStartSlide still applies the real speed floor, the ground check and
			// SlideCooldown - this cannot force a slide the player could not have started.
			MoveComp->RequestSlide();
			bEvadeSprintActive = false;
		}
		else if (EvadeSprintTimer <= 0.f || MoveComp->IsSliding() || MoveComp->IsWallRunning())
		{
			bEvadeSprintActive = false;
		}

		if (!bEvadeSprintActive)
		{
			// The slide itself clears bWantsToSprint inside EnterSlide, so the trigger is free again the
			// moment the bot is actually sliding - firing while sliding is allowed and always was.
			AI->SetFireSuppressed(false);
		}
		return;
	}

	SlideCheckTimer -= DeltaTime;
	if (SlideCheckTimer > 0.f) return;
	SlideCheckTimer = AI->GetDifficulty().SlideCheckInterval;

	// A wall run in progress owns the bot's movement for its duration; a slide would cancel the line-up or
	// drop it off the wall.
	if (TechAction != EShooterTechAction::None) return;

	if (MoveComp->IsSliding() || MoveComp->IsWallRunning()) return;
	if (!MoveComp->IsMovingOnGround()) return;

	// --- Steering, i.e. fighting at close range. There is no move goal to sprint toward, so an evasive slide
	// has to prime its own speed first. This is the branch that keeps slides in firefights.
	if (AI->IsSteering())
	{
		if (!bSlideToEvade) return;
		if (!AI->HasLineOfSight()) return;
		if (!RollTechChance()) return;

		bEvadeSprintActive = true;
		EvadeSprintTimer = SlideSprintPrimeTime;
		AI->SetFireSuppressed(true);
		return;
	}

	// --- Pathing. The bot is already sprinting toward somewhere, so the slide is free.
	if (!MoveComp->IsSprinting()) return;
	if (MoveComp->Velocity.Size2D() < SlideAttemptMinSpeed) return;

	// Slide into the destination. Arriving in a slide is faster than arriving in a sprint and ends with the
	// bot low and already able to shoot, because entering a slide releases the sprint that was blocking its
	// trigger.
	if (!AI->HasMoveGoal()) return;

	const float DistanceToGoal = AI->GetDistanceToMoveGoal();
	if (DistanceToGoal < SlideApproachDistanceMin || DistanceToGoal > SlideApproachDistanceMax) return;

	if (!RollTechChance()) return;

	// Intent only. CanStartSlide inside the movement component still applies the real speed floor, the ground
	// check and SlideCooldown - this cannot force a slide the player could not have started.
	MoveComp->RequestSlide();
}

/* --- Wall run --- */

bool UShooterAIMovementTechComponent::ProbeForWall(const FVector& Travel, bool bRightSide, FHitResult& OutHit) const
{
	const AShooterCharacter* Bot = GetShooterPawn();
	if (!IsValid(Bot) || !IsValid(GetWorld())) return false;

	const UCapsuleComponent* Capsule = Bot->GetCapsuleComponent();
	if (!IsValid(Capsule)) return false;

	const FVector TravelDirection = Travel.GetSafeNormal2D();
	if (TravelDirection.IsNearlyZero()) return false;

	// Perpendicular to *travel*, not to the capsule's facing. Up x Forward is UE's right-hand convention, so
	// this matches the vector UShooterMovementComponent::FindRunnableWall will use once the yaw has been
	// lined up onto travel - which is the whole point of the line-up phase.
	FVector Side = FVector::CrossProduct(FVector::UpVector, TravelDirection).GetSafeNormal();
	if (!bRightSide)
	{
		Side *= -1.f;
	}

	const FVector Start = Capsule->GetComponentLocation();
	const FVector End = Start + Side * WallProbeDistance;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(BotWallProbe), false, Bot);
	if (!GetWorld()->LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, Params)) return false;

	// Same rejection the movement component applies - floors, ceilings and steep ramps are not walls.
	if (FMath::Abs(OutHit.ImpactNormal.Z) > WallProbeMaxNormalZ) return false;

	// And the check the old probe was missing: the surface has to run *alongside* the route. A wall the bot
	// is about to hit head-on passes every other test and produces a jump into a dead stop.
	if (FMath::Abs(OutHit.ImpactNormal | TravelDirection) > WallProbeMaxTravelDot) return false;

	return true;
}

void UShooterAIMovementTechComponent::UpdateWallRun(float DeltaTime)
{
	AShooterAIController* AI = GetAIController();
	UShooterMovementComponent* MoveComp = GetShooterMovement();
	if (!IsValid(AI) || !IsValid(MoveComp)) return;

	WallRunAttemptTimer = FMath::Max(0.f, WallRunAttemptTimer - DeltaTime);

	if (!bAllowWallRun)
	{
		if (TechAction != EShooterTechAction::None)
		{
			AbortWallRun();
		}
		return;
	}

	// Only runs while an attempt is actually in flight, so the debug readout is the age of the current phase
	// rather than the age of the bot.
	if (TechAction == EShooterTechAction::None && !MoveComp->IsWallRunning())
	{
		TechPhaseTime = 0.f;
	}
	else
	{
		TechPhaseTime += DeltaTime;
	}

	// --- Attached. The yaw claim is forced from here on: PhysWallRun derives its along-wall direction from
	// the capsule's forward vector, so releasing the yaw mid-run does not return the bot's aim to the player,
	// it drops the bot off the wall. The run is already bounded by the character's WallRunMaxDuration.
	if (MoveComp->IsWallRunning())
	{
		if (TechAction != EShooterTechAction::WallRunActive)
		{
			TechAction = EShooterTechAction::WallRunActive;
			TechPhaseTime = 0.f;
			bWallJumpPlanned = RollTechChance();
		}

		// Velocity while attached is already along the wall, so it is the most accurate travel direction
		// available - more so than path-following input, which is aiming at a goal past the wall.
		AI->RequestTravelYawClaim(MoveComp->Velocity.GetSafeNormal2D(), /*bForce*/ true);

		// The wall jump is the payoff for being on the wall at all - it is the only exit that keeps the
		// momentum. UShooterMovementComponent::DoJump routes a jump made while wall running into TryWallJump,
		// so this is a plain jump press.
		if (bWallJumpPlanned && TechPhaseTime >= WallJumpAfterTime)
		{
			PressJump();
			bWallJumpPlanned = false;
		}
		return;
	}

	// Left the wall (jumped off, timed out, or lost it). Start the cooldown so the bot does not immediately
	// re-commit to the same surface.
	if (TechAction == EShooterTechAction::WallRunActive)
	{
		AbortWallRun();
		return;
	}

	// --- Lining up on the ground. This is the phase the old bot did not have, and its absence is why it
	// never wall ran: it jumped first and turned afterwards, so the yaw arrived after the attach window had
	// already closed.
	if (TechAction == EShooterTechAction::WallRunLineUp)
	{
		const FVector Travel = GetTravelDirection();

		// The claim is what actually turns the capsule, and it can be refused - the aim layer will not let
		// locomotion look away from a live target by more than TraverseMaxLookAwayDegrees. A refusal is a
		// clean abort, not a stall.
		if (!AI->RequestTravelYawClaim(Travel, /*bForce*/ false))
		{
			AbortWallRun();
			return;
		}

		FHitResult WallHit;
		if (TechPhaseTime > WallRunLineUpMaxTime || !ProbeForWall(Travel, bWallOnRight, WallHit))
		{
			AbortWallRun();
			return;
		}

		if (!AI->IsTravelYawAligned(WallRunLineUpDot)) return;

		// Aligned and still beside the wall. Commit.
		TechAction = EShooterTechAction::WallRunCommit;
		TechPhaseTime = 0.f;

		// A wall run needs the bot airborne and falling - TryStartWallRun refuses while grounded and while
		// still rising faster than WallRunMaxStartVerticalSpeed. Jumping is how it gets there.
		if (MoveComp->IsMovingOnGround())
		{
			PressJump();
		}
		return;
	}

	// --- Airborne, holding the yaw while the movement component's own attach test runs every frame. Nothing
	// to drive here: all the bot has to do is keep facing along travel until it attaches or the window shuts.
	if (TechAction == EShooterTechAction::WallRunCommit)
	{
		if (!AI->RequestTravelYawClaim(GetTravelDirection(), /*bForce*/ false) || TechPhaseTime > WallRunCommitTime)
		{
			AbortWallRun();
		}
		return;
	}

	TryBeginWallRun();
}

void UShooterAIMovementTechComponent::TryBeginWallRun()
{
	AShooterAIController* AI = GetAIController();
	const UShooterMovementComponent* MoveComp = GetShooterMovement();
	if (!IsValid(AI) || !IsValid(MoveComp)) return;

	if (WallRunAttemptTimer > 0.f) return;

	// Traversal actions only. Wall running mid-strafe would cost the bot its aim for the length of the run
	// (the yaw claim is what makes the run possible at all), which is a bad trade inside a firefight.
	// Approach is included on purpose - closing distance is exactly when a player would use the walls.
	if (!IsShooterBotTraversalAction(AI->GetAction())) return;

	if (!AI->HasMoveGoal()) return;
	if (AI->GetDistanceToMoveGoal() < WallRunMinGoalDistance) return;

	if (MoveComp->IsSliding() || MoveComp->IsWallRunning()) return;
	if (!MoveComp->IsMovingOnGround()) return;
	if (MoveComp->Velocity.Size2D() < WallRunAttemptMinSpeed) return;

	const FVector Travel = GetTravelDirection();
	if (Travel.IsNearlyZero()) return;

	FHitResult WallHit;
	bool bFoundRight = ProbeForWall(Travel, true, WallHit);
	if (!bFoundRight && !ProbeForWall(Travel, false, WallHit)) return;

	if (!RollTechChance())
	{
		// Rolled against it - don't re-roll every frame, or a high enough frame rate makes any chance certain
		// within a few frames and MovementTechChance stops meaning anything.
		WallRunAttemptTimer = WallRunAttemptInterval;
		return;
	}

	// The aim layer gets the final word: no wall run may start by turning the bot away from a live target.
	if (!AI->RequestTravelYawClaim(Travel, /*bForce*/ false))
	{
		WallRunAttemptTimer = WallRunAttemptInterval;
		return;
	}

	bWallOnRight = bFoundRight;
	TechAction = EShooterTechAction::WallRunLineUp;
	TechPhaseTime = 0.f;
	bWallJumpPlanned = false;
}

void UShooterAIMovementTechComponent::AbortWallRun()
{
	if (AShooterAIController* AI = GetAIController())
	{
		AI->ReleaseTravelYawClaim();
	}

	TechAction = EShooterTechAction::None;
	TechPhaseTime = 0.f;
	bWallJumpPlanned = false;
	WallRunAttemptTimer = WallRunAttemptInterval;
}

/* --- Jumping --- */

void UShooterAIMovementTechComponent::UpdateJump(float DeltaTime)
{
	const AShooterAIController* AI = GetAIController();
	const UShooterMovementComponent* MoveComp = GetShooterMovement();
	if (!IsValid(AI) || !IsValid(MoveComp)) return;

	// A wall-run action owns the jump for its whole duration - spending it on a dodge here would burn the air
	// jump the attach is relying on.
	if (TechAction != EShooterTechAction::None || MoveComp->IsWallRunning()) return;

	// --- Unstick. The nav mesh will happily route the bot at a ledge or a lip its capsule cannot step over;
	// this is what gets it across. Detected as "asking to move and barely moving" rather than by geometry,
	// which covers every cause including being pushed against a wall by the player. ---
	const bool bAskingToMove = !MoveComp->GetCurrentAcceleration().IsNearlyZero();
	const bool bBarelyMoving = MoveComp->Velocity.Size2D() < 50.f;
	if (bAskingToMove && bBarelyMoving && MoveComp->IsMovingOnGround())
	{
		StuckTimer += DeltaTime;
		if (StuckTimer >= StuckTimeBeforeJump)
		{
			StuckTimer = 0.f;
			PressJump();
			return;
		}
	}
	else
	{
		StuckTimer = 0.f;
	}

	// --- Evasive jumping while fighting. This is most of what makes a Veteran bot hard to track, and it is
	// also where the double jump earns its place: the second jump redirects horizontal momentum onto the
	// movement input, so a jump made mid-strafe genuinely changes direction in the air. ---
	if (!bJumpDodge) return;

	JumpDodgeTimer -= DeltaTime;
	if (JumpDodgeTimer > 0.f) return;
	JumpDodgeTimer = AI->GetDifficulty().JumpDodgeInterval;

	if (!IsShooterBotFightingAction(AI->GetAction())) return;
	if (!AI->HasLineOfSight()) return;
	if (!RollTechChance()) return;

	// Airborne with a jump left means this becomes the directional air jump rather than a ground jump.
	// AShooterCharacter::OnJumped_Implementation reads GetCurrentAcceleration for the redirect, and path
	// following is supplying that every tick, so the bot's air jump steers exactly like a player's.
	PressJump();
}

void UShooterAIMovementTechComponent::PressJump()
{
	AShooterCharacter* Bot = GetShooterPawn();
	if (!IsValid(Bot)) return;

	Bot->Jump();
	bJumpPressedLastTick = true;
}

void UShooterAIMovementTechComponent::ReleaseJumpIfPending()
{
	if (!bJumpPressedLastTick) return;

	AShooterCharacter* Bot = GetShooterPawn();
	if (IsValid(Bot))
	{
		// Released a frame after the press, never in the same frame.
		//
		// Both failure modes are real. Releasing in the same tick can clear bPressedJump before
		// CheckJumpInput runs, and the jump simply never happens. Leaving it held spends the air jump on the
		// very next frame - CheckJumpInput jumps again as soon as JumpCurrentCount is still under
		// JumpMaxCount - so a single intended jump becomes an instant double jump with no height gained.
		Bot->StopJumping();
	}

	bJumpPressedLastTick = false;
}

/* --- Debug --- */

void UShooterAIMovementTechComponent::DrawTechDebug() const
{
	const AShooterAIController* AI = GetAIController();
	const AShooterCharacter* Bot = GetShooterPawn();
	if (!IsValid(AI) || !IsValid(Bot) || !IsValid(GetWorld())) return;

	static const TCHAR* TechNames[] = { TEXT("-"), TEXT("LineUp"), TEXT("Commit"), TEXT("Active") };
	const int32 TechIndex = static_cast<int32>(TechAction);

	DrawDebugString(GetWorld(), FVector(0.f, 0.f, 100.f),
		FString::Printf(TEXT("tech %s %.2fs | cd %.1fs%s"),
			TechIndex < UE_ARRAY_COUNT(TechNames) ? TechNames[TechIndex] : TEXT("?"),
			TechPhaseTime,
			WallRunAttemptTimer,
			bSprintIntent ? TEXT(" | sprint") : TEXT("")),
		const_cast<AShooterCharacter*>(Bot), FColor::Silver, 0.f, true);

	// Both wall probes, drawn live. Green means a runnable surface: if these are never green while the bot
	// runs past a wall, the problem is WallProbeDistance or WallProbeMaxTravelDot, not the decision logic.
	const UCapsuleComponent* Capsule = Bot->GetCapsuleComponent();
	const FVector Travel = GetTravelDirection();
	if (IsValid(Capsule) && !Travel.IsNearlyZero())
	{
		const FVector Start = Capsule->GetComponentLocation();
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Travel).GetSafeNormal();

		FHitResult Probe;
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const bool bRight = (Index == 0);
			const FVector End = Start + (bRight ? Side : -Side) * WallProbeDistance;
			const bool bHit = ProbeForWall(Travel, bRight, Probe);
			DrawDebugLine(GetWorld(), Start, End, bHit ? FColor::Green : FColor::Blue, false, -1.f, 0, 2.f);
		}
	}
}
