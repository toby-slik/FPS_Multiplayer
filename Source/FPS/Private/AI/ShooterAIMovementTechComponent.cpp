// Copyright Druid Mechanics


#include "AI/ShooterAIMovementTechComponent.h"

#include "AI/ShooterAIController.h"
#include "Character/ShooterCharacter.h"
#include "Character/ShooterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

UShooterAIMovementTechComponent::UShooterAIMovementTechComponent()
{
	// Ticked by hand from AShooterAIController::Tick, before the aim component, so a wall-run attempt can
	// claim the yaw for the frame before aim writes the control rotation.
	PrimaryComponentTick.bCanEverTick = false;

	SprintMinGoalDistance = 500.f;

	SlideAttemptMinSpeed = 550.f;
	SlideApproachDistanceMin = 250.f;
	SlideApproachDistanceMax = 900.f;
	bSlideToEvade = true;

	bAllowWallRun = true;
	WallProbeDistance = 90.f;
	WallRunAttemptMinSpeed = 450.f;
	WallProbeMaxNormalZ = 0.25f;
	WallRunCommitTime = 0.6f;
	WallRunAttemptInterval = 2.f;
	WallJumpAtRunFraction = 0.7f;

	bJumpDodge = true;
	StuckTimeBeforeJump = 0.4f;

	SlideCheckTimer = 0.f;
	WallRunAttemptTimer = 0.f;
	WallRunCommitTimer = 0.f;
	JumpDodgeTimer = 0.f;
	StuckTimer = 0.f;
	bJumpPressedLastTick = false;
	bWallRunJumpQueued = false;
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

	SlideCheckTimer = 0.f;
	WallRunAttemptTimer = 0.f;
	WallRunCommitTimer = 0.f;
	JumpDodgeTimer = 0.f;
	StuckTimer = 0.f;
	bWallRunJumpQueued = false;
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

	UpdateSprint();
	UpdateSlide(DeltaTime);
	UpdateWallRun(DeltaTime);
	UpdateJump(DeltaTime);
}

/* --- Sprint --- */

void UShooterAIMovementTechComponent::UpdateSprint()
{
	const AShooterAIController* AI = GetAIController();
	UShooterMovementComponent* MoveComp = GetShooterMovement();
	const AShooterCharacter* Bot = GetShooterPawn();
	if (!IsValid(AI) || !IsValid(MoveComp) || !IsValid(Bot)) return;

	// Sprinting blocks firing. That single rule is what makes this decision tactical rather than cosmetic:
	// the bot has to choose between covering ground and being able to shoot, exactly as the player does. So
	// anything it could shoot at right now beats any distance it might close.
	const bool bCouldShoot = AI->HasAcquiredTarget() && AI->HasLineOfSight();

	bool bWantSprint = false;
	if (!bCouldShoot && AI->HasMoveGoal())
	{
		const float DistanceToGoal = FVector::Dist(Bot->GetActorLocation(), AI->GetMoveGoal());
		bWantSprint = DistanceToGoal > SprintMinGoalDistance;
	}

	// Only written on a change. SetWantsToSprint(false) clears the predicted sprint flag outright, so
	// hammering it every frame would fight the movement component's own state rather than express intent.
	if (bWantSprint != MoveComp->WantsToSprint())
	{
		MoveComp->SetWantsToSprint(bWantSprint);
	}
}

/* --- Slide --- */

void UShooterAIMovementTechComponent::UpdateSlide(float DeltaTime)
{
	const AShooterAIController* AI = GetAIController();
	UShooterMovementComponent* MoveComp = GetShooterMovement();
	const AShooterCharacter* Bot = GetShooterPawn();
	if (!IsValid(AI) || !IsValid(MoveComp) || !IsValid(Bot)) return;

	SlideCheckTimer -= DeltaTime;
	if (SlideCheckTimer > 0.f) return;
	SlideCheckTimer = AI->GetDifficulty().SlideCheckInterval;

	if (MoveComp->IsSliding() || MoveComp->IsWallRunning()) return;
	if (!MoveComp->IsMovingOnGround()) return;
	if (!MoveComp->IsSprinting()) return;
	if (MoveComp->Velocity.Size2D() < SlideAttemptMinSpeed) return;

	// Two reasons to slide, both of which a good player would recognise.
	bool bWantSlide = false;

	// One: slide into the destination. Arriving in a slide is faster than arriving in a sprint and ends with
	// the bot low and already able to shoot, because entering a slide releases the sprint that was blocking
	// its trigger.
	if (AI->HasMoveGoal())
	{
		const float DistanceToGoal = FVector::Dist(Bot->GetActorLocation(), AI->GetMoveGoal());
		bWantSlide = DistanceToGoal >= SlideApproachDistanceMin && DistanceToGoal <= SlideApproachDistanceMax;
	}

	// Two: slide to evade. Crossing open ground while the target can see it is the worst place to be running
	// in a straight line, and a slide both changes its silhouette and moves it faster than sprint.
	if (!bWantSlide && bSlideToEvade && AI->HasLineOfSight())
	{
		bWantSlide = true;
	}

	if (!bWantSlide) return;
	if (!RollTechChance()) return;

	// Intent only. CanStartSlide inside the movement component still applies the real speed floor, the ground
	// check and SlideCooldown - this cannot force a slide the player could not have started.
	MoveComp->RequestSlide();
}

/* --- Wall run --- */

bool UShooterAIMovementTechComponent::ProbeForWall(bool bRightSide, FHitResult& OutHit) const
{
	const AShooterCharacter* Bot = GetShooterPawn();
	if (!IsValid(Bot) || !IsValid(GetWorld())) return false;

	const UCapsuleComponent* Capsule = Bot->GetCapsuleComponent();
	if (!IsValid(Capsule)) return false;

	const FVector Start = Capsule->GetComponentLocation();
	const FVector Direction = bRightSide ? Capsule->GetRightVector() : -Capsule->GetRightVector();
	const FVector End = Start + Direction * WallProbeDistance;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(BotWallProbe), false, Bot);
	if (!GetWorld()->LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, Params)) return false;

	// Same rejection the movement component applies - floors, ceilings and steep ramps are not walls. Probing
	// on the same channel and with the same normal test is what keeps the bot from committing to a surface
	// UShooterMovementComponent::FindRunnableWall would then refuse.
	return FMath::Abs(OutHit.ImpactNormal.Z) <= WallProbeMaxNormalZ;
}

void UShooterAIMovementTechComponent::UpdateWallRun(float DeltaTime)
{
	const AShooterAIController* AI = GetAIController();
	UShooterMovementComponent* MoveComp = GetShooterMovement();
	AShooterCharacter* Bot = GetShooterPawn();
	if (!IsValid(AI) || !IsValid(MoveComp) || !IsValid(Bot)) return;

	WallRunAttemptTimer = FMath::Max(0.f, WallRunAttemptTimer - DeltaTime);
	WallRunCommitTimer = FMath::Max(0.f, WallRunCommitTimer - DeltaTime);

	if (!bAllowWallRun)
	{
		WallRunCommitTimer = 0.f;
		return;
	}

	// --- Already on a wall: hold the yaw, and leave with a wall jump rather than sliding off the end. ---
	if (MoveComp->IsWallRunning())
	{
		// Keeps the yaw claimed for the whole run. PhysWallRun flips its along-wall direction to match the
		// capsule's forward vector and bleeds speed unless input points along it, so letting the bot turn
		// back to its target mid-run would stall the run rather than merely look wrong.
		WallRunCommitTimer = FMath::Max(WallRunCommitTimer, 0.1f);

		if (!bWallRunJumpQueued && RollTechChance())
		{
			bWallRunJumpQueued = true;
		}

		// The wall jump is the payoff for being on the wall at all - it is the only exit that keeps the
		// momentum. UShooterMovementComponent::DoJump routes a jump made while wall running into TryWallJump,
		// so this is a plain jump press.
		if (bWallRunJumpQueued && WallRunCommitTimer <= 0.15f)
		{
			PressJump();
			bWallRunJumpQueued = false;
		}
		return;
	}

	// --- Mid-attempt: airborne, heading at a wall, waiting for the movement component to attach. ---
	if (WallRunCommitTimer > 0.f)
	{
		// Nothing to drive: the attach test runs inside the movement component's own update every frame. All
		// the bot has to do is keep facing along its travel direction until it succeeds or the window closes,
		// and WantsYawLockedToTravel is what delivers that.
		return;
	}

	// --- Look for a new opportunity. ---
	if (WallRunAttemptTimer > 0.f) return;

	// Traversal states only. Wall running while engaging would cost the bot its aim for the length of the run
	// (the yaw lock is what makes the run possible at all), which is a bad trade in a firefight.
	const EShooterBotState State = AI->GetBotState();
	if (State != EShooterBotState::Hunt && State != EShooterBotState::Reposition && State != EShooterBotState::Retreat)
	{
		return;
	}

	if (MoveComp->IsSliding()) return;
	if (MoveComp->Velocity.Size2D() < WallRunAttemptMinSpeed) return;
	if (GetTravelDirection().IsNearlyZero()) return;

	FHitResult WallHit;
	if (!ProbeForWall(true, WallHit) && !ProbeForWall(false, WallHit)) return;

	if (!RollTechChance())
	{
		// Rolled against it - don't re-roll every frame, or a high enough frame rate makes any chance
		// certain within a few frames and MovementTechChance stops meaning anything.
		WallRunAttemptTimer = WallRunAttemptInterval;
		return;
	}

	WallRunAttemptTimer = WallRunAttemptInterval;
	WallRunCommitTimer = WallRunCommitTime;

	// A wall run needs the bot airborne and falling - TryStartWallRun refuses while grounded and while still
	// rising faster than WallRunMaxStartVerticalSpeed. Jumping is how it gets there; the commit window above
	// is what keeps it facing the right way long enough for the attach test to pass on the way up.
	if (MoveComp->IsMovingOnGround())
	{
		PressJump();
	}
}

bool UShooterAIMovementTechComponent::WantsYawLockedToTravel() const
{
	const UShooterMovementComponent* MoveComp = GetShooterMovement();
	if (!IsValid(MoveComp)) return false;

	return MoveComp->IsWallRunning() || WallRunCommitTimer > 0.f;
}

/* --- Jumping --- */

void UShooterAIMovementTechComponent::UpdateJump(float DeltaTime)
{
	const AShooterAIController* AI = GetAIController();
	const UShooterMovementComponent* MoveComp = GetShooterMovement();
	const AShooterCharacter* Bot = GetShooterPawn();
	if (!IsValid(AI) || !IsValid(MoveComp) || !IsValid(Bot)) return;

	// A wall-run attempt owns the jump for its window - spending it on a dodge here would waste the air jump
	// the attach is relying on.
	if (WallRunCommitTimer > 0.f || MoveComp->IsWallRunning()) return;

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

	if (AI->GetBotState() != EShooterBotState::Engage) return;
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
