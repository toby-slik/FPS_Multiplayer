// Copyright Druid Mechanics


#include "Character/ShooterMovementComponent.h"

#include "Character/ShooterCharacter.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

/* ---------------------------------------------------------------------------
 * FSavedMove_Shooter
 * ------------------------------------------------------------------------- */

void FSavedMove_Shooter::Clear()
{
	Super::Clear();

	Saved_bWantsToSprint = 0;
	Saved_bWantsToSlide = 0;
	Saved_bSprinting = 0;
	Saved_bSliding = 0;
	Saved_WallRunSide = EWallRunSide::None;
	Saved_WallRunNormal = FVector::ZeroVector;
	Saved_SlideTimeRemaining = 0.f;
	Saved_SlideCooldownRemaining = 0.f;
	Saved_SlideBufferRemaining = 0.f;
	Saved_WallRunTimeRemaining = 0.f;
	Saved_WallRunCooldownRemaining = 0.f;
	Saved_SameWallCooldownRemaining = 0.f;
	Saved_WallRunActor = nullptr;
	Saved_LastWallRunActor = nullptr;
}

uint8 FSavedMove_Shooter::GetCompressedFlags() const
{
	uint8 Result = Super::GetCompressedFlags();

	// Only intent travels to the server. Everything else the server recomputes by running the
	// same simulation, which is the whole point of doing this inside the movement component.
	if (Saved_bWantsToSprint)
	{
		Result |= FLAG_Custom_0;
	}
	if (Saved_bWantsToSlide)
	{
		Result |= FLAG_Custom_1;
	}

	return Result;
}

bool FSavedMove_Shooter::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const
{
	const FSavedMove_Shooter* Other = static_cast<const FSavedMove_Shooter*>(NewMove.Get());
	if (Other == nullptr)
	{
		return false;
	}

	// Combining moves with different intent would lose the frame the intent changed on.
	if (Saved_bWantsToSprint != Other->Saved_bWantsToSprint)
	{
		return false;
	}
	if (Saved_bWantsToSlide != Other->Saved_bWantsToSlide)
	{
		return false;
	}

	return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
}

void FSavedMove_Shooter::SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);

	if (!IsValid(C))
	{
		return;
	}

	const UShooterMovementComponent* Move = Cast<UShooterMovementComponent>(C->GetCharacterMovement());
	if (Move == nullptr)
	{
		return;
	}

	// Captured BEFORE the move executes, which is what makes replay reproduce it faithfully.
	Saved_bWantsToSprint = Move->bWantsToSprint;
	Saved_bWantsToSlide = Move->bWantsToSlide;
	Saved_bSprinting = Move->bSprinting;
	Saved_bSliding = Move->bSliding;
	Saved_WallRunSide = Move->WallRunSide;
	Saved_WallRunNormal = Move->WallRunNormal;
	Saved_SlideTimeRemaining = Move->SlideTimeRemaining;
	Saved_SlideCooldownRemaining = Move->SlideCooldownRemaining;
	Saved_SlideBufferRemaining = Move->SlideBufferRemaining;
	Saved_WallRunTimeRemaining = Move->WallRunTimeRemaining;
	Saved_WallRunCooldownRemaining = Move->WallRunCooldownRemaining;
	Saved_SameWallCooldownRemaining = Move->SameWallCooldownRemaining;
	Saved_WallRunActor = Move->WallRunActor;
	Saved_LastWallRunActor = Move->LastWallRunActor;
}

void FSavedMove_Shooter::PrepMoveFor(ACharacter* C)
{
	Super::PrepMoveFor(C);

	if (!IsValid(C))
	{
		return;
	}

	UShooterMovementComponent* Move = Cast<UShooterMovementComponent>(C->GetCharacterMovement());
	if (Move == nullptr)
	{
		return;
	}

	// Restore the pre-move snapshot so a replayed move starts from the state it originally did.
	Move->bWantsToSprint = Saved_bWantsToSprint;
	Move->bWantsToSlide = Saved_bWantsToSlide;
	Move->bSprinting = Saved_bSprinting;
	Move->bSliding = Saved_bSliding;
	Move->WallRunSide = Saved_WallRunSide;
	Move->WallRunNormal = Saved_WallRunNormal;
	Move->SlideTimeRemaining = Saved_SlideTimeRemaining;
	Move->SlideCooldownRemaining = Saved_SlideCooldownRemaining;
	Move->SlideBufferRemaining = Saved_SlideBufferRemaining;
	Move->WallRunTimeRemaining = Saved_WallRunTimeRemaining;
	Move->WallRunCooldownRemaining = Saved_WallRunCooldownRemaining;
	Move->SameWallCooldownRemaining = Saved_SameWallCooldownRemaining;
	Move->WallRunActor = Saved_WallRunActor;
	Move->LastWallRunActor = Saved_LastWallRunActor;
}

/* ---------------------------------------------------------------------------
 * FNetworkPredictionData_Client_Shooter
 * ------------------------------------------------------------------------- */

FNetworkPredictionData_Client_Shooter::FNetworkPredictionData_Client_Shooter(const UCharacterMovementComponent& ClientMovement)
	: Super(ClientMovement)
{
}

FSavedMovePtr FNetworkPredictionData_Client_Shooter::AllocateNewMove()
{
	return FSavedMovePtr(new FSavedMove_Shooter());
}

/* ---------------------------------------------------------------------------
 * UShooterMovementComponent
 * ------------------------------------------------------------------------- */

UShooterMovementComponent::UShooterMovementComponent()
{
	bWantsToSprint = 0;
	bWantsToSlide = 0;
	bSprinting = 0;
	bSliding = 0;

	WallRunSide = EWallRunSide::None;
	WallRunNormal = FVector::ZeroVector;

	SlideTimeRemaining = 0.f;
	SlideCooldownRemaining = 0.f;
	SlideBufferRemaining = 0.f;
	WallRunTimeRemaining = 0.f;
	WallRunCooldownRemaining = 0.f;
	SameWallCooldownRemaining = 0.f;

	DefaultGroundFriction = 8.f;
}

void UShooterMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	// Captured before anything can modify it, so slide friction can be applied and removed
	// purely as a function of predicted state.
	DefaultGroundFriction = GroundFriction;
}

AShooterCharacter* UShooterMovementComponent::GetShooterCharacter() const
{
	if (!CachedShooterCharacter.IsValid())
	{
		CachedShooterCharacter = Cast<AShooterCharacter>(CharacterOwner);
	}
	return CachedShooterCharacter.Get();
}

FNetworkPredictionData_Client* UShooterMovementComponent::GetPredictionData_Client() const
{
	check(PawnOwner != nullptr);

	if (ClientPredictionData == nullptr)
	{
		UShooterMovementComponent* MutableThis = const_cast<UShooterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_Shooter(*this);
	}

	return ClientPredictionData;
}

void UShooterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);

	// Server side: adopt the client's intent for this move, then reach its own conclusions.
	bWantsToSprint = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
	bWantsToSlide = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
}

/* --- Input entry points --- */

void UShooterMovementComponent::SetWantsToSprint(bool bNewWantsToSprint)
{
	bWantsToSprint = bNewWantsToSprint;

	// Cleared immediately rather than waiting for the next movement update, because the combat
	// component checks IsSprinting() on the very same frame it cancels sprint to fire. A one
	// frame lag here would drop the first shot - the exact bug the fire gate exists to avoid.
	if (!bNewWantsToSprint)
	{
		bSprinting = 0;
	}
}

void UShooterMovementComponent::RequestSlide()
{
	const AShooterCharacter* Char = GetShooterCharacter();

	// Latched intent rather than an immediate start. The movement update decides whether it is
	// legal, which is what lets client and server agree, and it doubles as the landing buffer.
	bWantsToSlide = 1;

	// Only an airborne press buffers, matching the pre-refactor rule. A grounded press that
	// fails its conditions gets exactly one attempt and is then dropped.
	SlideBufferRemaining = (IsValid(Char) && IsFalling()) ? Char->SlideInputBufferTime : 0.f;
}

void UShooterMovementComponent::RequestCancelSlide()
{
	// The slide holds bWantsToCrouch, so "player wants to stand" IS the cancel. Routing it
	// through the base crouch flag means the cancel is predicted without spending a custom flag.
	bWantsToCrouch = false;
}

/* --- State --- */

bool UShooterMovementComponent::IsWallRunning() const
{
	return MovementMode == MOVE_Custom && CustomMovementMode == static_cast<uint8>(EShooterCustomMovementMode::WallRun);
}

void UShooterMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	AdvanceCooldowns(DeltaSeconds);
	UpdateSprintState();
	UpdateSlideState(DeltaSeconds);
	UpdateWallRunState(DeltaSeconds);

	// Derived from predicted state every move rather than mutated on enter/exit, so the client
	// and server can never drift apart on it and nothing needs replicating.
	if (const AShooterCharacter* Char = GetShooterCharacter())
	{
		GroundFriction = bSliding ? Char->SlideGroundFriction : DefaultGroundFriction;
	}

	// Runs last so the crouch state it applies reflects a slide entered this frame.
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
}

void UShooterMovementComponent::AdvanceCooldowns(float DeltaSeconds)
{
	SlideCooldownRemaining = FMath::Max(0.f, SlideCooldownRemaining - DeltaSeconds);
	WallRunCooldownRemaining = FMath::Max(0.f, WallRunCooldownRemaining - DeltaSeconds);
	SameWallCooldownRemaining = FMath::Max(0.f, SameWallCooldownRemaining - DeltaSeconds);
}

void UShooterMovementComponent::UpdateSprintState()
{
	const AShooterCharacter* Char = GetShooterCharacter();
	if (!IsValid(Char))
	{
		return;
	}

	if (bSliding || IsWallRunning())
	{
		// Both abilities consume the sprint on entry. Clearing intent too stops a queued toggle
		// reinstating it underneath them, which would re-block firing mid-ability.
		bWantsToSprint = 0;
	}
	else if (bWantsToSprint && IsMovingOnGround())
	{
		// Don't leave the character looping a sprint animation while standing still.
		if (GetCurrentAcceleration().IsNearlyZero() && Velocity.Size2D() <= Char->SprintStopSpeed)
		{
			bWantsToSprint = 0;
		}
	}

	bSprinting = bWantsToSprint && !bSliding && !IsWallRunning();
}

void UShooterMovementComponent::UpdateSlideState(float DeltaSeconds)
{
	const AShooterCharacter* Char = GetShooterCharacter();
	if (!IsValid(Char))
	{
		return;
	}

	if (bSliding)
	{
		SlideTimeRemaining -= DeltaSeconds;

		const bool bCancelled = !bWantsToCrouch;
		const bool bExpired = SlideTimeRemaining <= 0.f;
		const bool bTooSlow = Velocity.Size2D() < Char->SlideEndSpeed;

		if (bCancelled || bExpired || bTooSlow)
		{
			ExitSlide();
		}
		return;
	}

	if (!bWantsToSlide)
	{
		return;
	}

	if (CanStartSlide())
	{
		EnterSlide();
		return;
	}

	// Airborne press stays queued for SlideInputBufferTime so landing into a slide doesn't need
	// frame-perfect timing. Decremented after the attempt, so a zero-length buffer still gets
	// exactly one try and the feature can be switched off by setting it to 0.
	SlideBufferRemaining -= DeltaSeconds;
	if (SlideBufferRemaining <= 0.f)
	{
		bWantsToSlide = 0;
	}
}

bool UShooterMovementComponent::CanStartSlide() const
{
	const AShooterCharacter* Char = GetShooterCharacter();
	if (!IsValid(Char))
	{
		return false;
	}

	if (bSliding) return false;

	// A slide can only ever come out of a sprint.
	if (!bSprinting) return false;

	if (!IsMovingOnGround()) return false;

	// Sprint can be toggled on while standing still, so the speed check is what actually
	// prevents sliding from a standstill.
	if (Velocity.Size2D() < Char->SlideMinStartSpeed) return false;

	if (SlideCooldownRemaining > 0.f) return false;

	return true;
}

void UShooterMovementComponent::EnterSlide()
{
	const AShooterCharacter* Char = GetShooterCharacter();
	if (!IsValid(Char))
	{
		return;
	}

	bSliding = 1;
	bWantsToSlide = 0;
	SlideBufferRemaining = 0.f;
	SlideTimeRemaining = Char->SlideDuration;

	// Entering a slide ends the sprint, which is what unblocks firing and aiming during it.
	bWantsToSprint = 0;
	bSprinting = 0;

	// Equivalent to the old Crouch() call. Super::UpdateCharacterStateBeforeMovement applies it.
	bWantsToCrouch = true;

	FVector SlideDirection = Velocity;
	SlideDirection.Z = 0.f;
	if (SlideDirection.IsNearlyZero() && IsValid(UpdatedComponent))
	{
		SlideDirection = UpdatedComponent->GetForwardVector();
	}

	// GetMaxSpeed returns SlideLaunchSpeed while sliding, so this is not clamped back down.
	Velocity = SlideDirection.GetSafeNormal() * Char->SlideLaunchSpeed;
}

void UShooterMovementComponent::ExitSlide()
{
	const AShooterCharacter* Char = GetShooterCharacter();

	bSliding = 0;
	SlideTimeRemaining = 0.f;
	SlideCooldownRemaining = IsValid(Char) ? Char->SlideCooldown : 0.f;

	// Exit state is walking: the sprint was consumed by the slide, so re-sprinting is a
	// deliberate second press. Combined with SlideCooldown this is the anti-spam guard.
	bWantsToCrouch = false;
}

/* --- Wall run --- */

void UShooterMovementComponent::UpdateWallRunState(float DeltaSeconds)
{
	if (IsWallRunning())
	{
		WallRunTimeRemaining -= DeltaSeconds;
		if (WallRunTimeRemaining <= 0.f)
		{
			EndWallRun();
		}
		return;
	}

	TryStartWallRun();
}

bool UShooterMovementComponent::FindRunnableWall(EWallRunSide Side, FHitResult& OutHit) const
{
	const AShooterCharacter* Char = GetShooterCharacter();
	if (!IsValid(Char) || Side == EWallRunSide::None) return false;
	if (!IsValid(UpdatedComponent) || !IsValid(GetWorld())) return false;

	const FVector RightVector = UpdatedComponent->GetRightVector();
	const FVector TraceDirection = (Side == EWallRunSide::Right) ? RightVector : -RightVector;
	const FVector Start = UpdatedComponent->GetComponentLocation();
	const FVector End = Start + TraceDirection * Char->WallRunTraceDistance;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(WallRunTrace), false, CharacterOwner);
	if (!GetWorld()->LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, Params)) return false;

	// Reject floors, ceilings and steep ramps - only near-vertical surfaces are runnable.
	if (FMath::Abs(OutHit.ImpactNormal.Z) > Char->WallRunMaxWallNormalZ) return false;

	return true;
}

bool UShooterMovementComponent::TryStartWallRun()
{
	const AShooterCharacter* Char = GetShooterCharacter();
	if (!IsValid(Char) || !IsValid(UpdatedComponent) || !IsValid(GetWorld())) return false;

	if (bSliding || IsWallRunning()) return false;
	if (!IsFalling()) return false;

	// Re-attach guard, part one: no wall at all for a moment after any wall run ends.
	if (WallRunCooldownRemaining > 0.f) return false;

	// Don't snap onto a wall while still rising hard out of a jump.
	if (Velocity.Z > Char->WallRunMaxStartVerticalSpeed) return false;

	if (Velocity.Size2D() < Char->WallRunMinSpeed) return false;

	// Must actually be driving forward, not just drifting past a wall.
	const FVector InputDirection = GetCurrentAcceleration().GetSafeNormal();
	if (InputDirection.IsNearlyZero()) return false;
	if ((InputDirection | UpdatedComponent->GetForwardVector()) < Char->WallRunMinForwardInputDot) return false;

	// Need clearance below, otherwise you can attach while skimming along the floor next to a wall.
	FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(WallRunGroundClearance), false, CharacterOwner);
	FHitResult GroundHit;
	const FVector Origin = UpdatedComponent->GetComponentLocation();
	if (GetWorld()->LineTraceSingleByChannel(GroundHit, Origin, Origin - FVector(0.f, 0.f, Char->WallRunMinGroundClearance), ECC_Visibility, GroundParams))
	{
		return false;
	}

	FHitResult WallHit;
	EWallRunSide FoundSide = EWallRunSide::None;
	if (FindRunnableWall(EWallRunSide::Right, WallHit))
	{
		FoundSide = EWallRunSide::Right;
	}
	else if (FindRunnableWall(EWallRunSide::Left, WallHit))
	{
		FoundSide = EWallRunSide::Left;
	}

	if (FoundSide == EWallRunSide::None) return false;

	// Re-attach guard, part two: a longer lockout on the specific wall just left, so a player
	// can't jump off a wall and immediately climb the same one again.
	if (SameWallCooldownRemaining > 0.f && LastWallRunActor.IsValid() && LastWallRunActor.Get() == WallHit.GetActor())
	{
		return false;
	}

	// Attaching consumes the sprint state, exactly like a slide does. That is what unblocks
	// firing and aiming while on the wall - the gate is "is sprinting", never "is in an ability".
	bWantsToSprint = 0;
	bSprinting = 0;

	WallRunSide = FoundSide;
	WallRunNormal = WallHit.ImpactNormal;
	WallRunActor = WallHit.GetActor();
	WallRunTimeRemaining = Char->WallRunMaxDuration;

	// Fresh air jump off every wall, per the double jump reset rule.
	if (IsValid(CharacterOwner))
	{
		CharacterOwner->ResetJumpState();
	}

	SetMovementMode(MOVE_Custom, static_cast<uint8>(EShooterCustomMovementMode::WallRun));
	return true;
}

void UShooterMovementComponent::ClearWallRunState()
{
	const AShooterCharacter* Char = GetShooterCharacter();

	WallRunCooldownRemaining = IsValid(Char) ? Char->WallRunCooldown : 0.f;
	SameWallCooldownRemaining = IsValid(Char) ? Char->WallRunSameWallCooldown : 0.f;

	// Remembered so the same-wall lockout can reject an immediate re-attach.
	LastWallRunActor = WallRunActor;
	WallRunActor = nullptr;
	WallRunSide = EWallRunSide::None;
	WallRunNormal = FVector::ZeroVector;
	WallRunTimeRemaining = 0.f;
}

void UShooterMovementComponent::EndWallRun(EMovementMode NewMode, uint8 NewCustomMode)
{
	// State first, mode second: OnMovementModeChanged uses WallRunSide to detect a mode change
	// it did not drive, so clearing it here keeps that path from double-firing.
	ClearWallRunState();
	SetMovementMode(NewMode, NewCustomMode);
}

void UShooterMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);

	const bool bWasWallRunning =
		PreviousMovementMode == MOVE_Custom &&
		PreviousCustomMode == static_cast<uint8>(EShooterCustomMovementMode::WallRun);

	// Safety net for a mode change this component did not drive - death, teleport, an external
	// SetMovementMode. EndWallRun already cleared WallRunSide, so this won't double-fire.
	if (bWasWallRunning && !IsWallRunning() && WallRunSide != EWallRunSide::None)
	{
		ClearWallRunState();
	}
}

void UShooterMovementComponent::PhysCustom(float deltaTime, int32 Iterations)
{
	if (CustomMovementMode == static_cast<uint8>(EShooterCustomMovementMode::WallRun))
	{
		PhysWallRun(deltaTime, Iterations);
		return;
	}

	Super::PhysCustom(deltaTime, Iterations);
}

void UShooterMovementComponent::PhysWallRun(float deltaTime, int32 Iterations)
{
	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	const AShooterCharacter* Char = GetShooterCharacter();
	if (!IsValid(Char) || !IsValid(UpdatedComponent))
	{
		EndWallRun();
		return;
	}

	Iterations++;
	bJustTeleported = false;

	// --- Exit condition: lost the wall. ---
	FHitResult WallHit;
	if (!FindRunnableWall(WallRunSide, WallHit))
	{
		EndWallRun();
		StartNewPhysics(deltaTime, Iterations);
		return;
	}
	WallRunNormal = WallHit.ImpactNormal;

	// Direction along the wall surface, flipped to whichever way the character is facing.
	FVector AlongWall = FVector::CrossProduct(WallRunNormal, FVector::UpVector).GetSafeNormal();
	if ((AlongWall | UpdatedComponent->GetForwardVector()) < 0.f)
	{
		AlongWall *= -1.f;
	}

	// Holding forward drives the run; releasing it bleeds speed until the floor is hit.
	float Speed = Velocity.Size2D();
	const FVector InputDirection = GetCurrentAcceleration().GetSafeNormal();
	const bool bDrivingForward = !InputDirection.IsNearlyZero() && (InputDirection | AlongWall) > 0.f;

	if (bDrivingForward)
	{
		Speed = FMath::FInterpTo(Speed, Char->WallRunSpeed, deltaTime, Char->WallRunAccelInterpSpeed);
	}
	else
	{
		Speed = FMath::Max(0.f, Speed - Char->WallRunDeceleration * deltaTime);
	}

	// --- Exit condition: dropped below the speed floor. ---
	if (Speed < Char->WallRunMinSpeed)
	{
		EndWallRun();
		StartNewPhysics(deltaTime, Iterations);
		return;
	}

	// Reduced gravity with a clamped sink rate, applied here instead of via GravityScale so no
	// component property has to be mutated (and therefore kept in sync) to represent the state.
	float NewZ = Velocity.Z + GetGravityZ() * Char->WallRunGravityScale * deltaTime;
	NewZ = FMath::Max(NewZ, -Char->WallRunMaxFallSpeed);

	Velocity = AlongWall * Speed - WallRunNormal * Char->WallRunStickSpeed;
	Velocity.Z = NewZ;

	const FVector OldLocation = UpdatedComponent->GetComponentLocation();
	const FVector Adjusted = Velocity * deltaTime;

	FHitResult Hit(1.f);
	SafeMoveUpdatedComponent(Adjusted, UpdatedComponent->GetComponentQuat(), true, Hit);

	if (Hit.bBlockingHit)
	{
		// --- Exit condition: landed. ---
		if (IsValidLandingSpot(UpdatedComponent->GetComponentLocation(), Hit))
		{
			EndWallRun(MOVE_Walking);
			StartNewPhysics(deltaTime * (1.f - Hit.Time), Iterations);
			return;
		}

		HandleImpact(Hit, deltaTime, Adjusted);
		SlideAlongSurface(Adjusted, 1.f - Hit.Time, Hit.Normal, Hit, true);
	}

	// Derive velocity from the move actually achieved, so the wall blocking the stick component
	// doesn't accumulate phantom inward speed frame over frame.
	if (!bJustTeleported && deltaTime > 0.f)
	{
		Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / deltaTime;
	}
}

/* --- Jumping --- */

bool UShooterMovementComponent::CanAttemptJump() const
{
	// Base refuses anything that isn't walking or falling, which would make a wall run a dead end.
	// A plain crouch still blocks the jump, but a slide must not: the slide holds bWantsToCrouch
	// for its whole duration, and jumping out of a slide is intended.
	const bool bCrouchBlocksJump = bWantsToCrouch && !bSliding;

	return IsJumpAllowed() && !bCrouchBlocksJump && (IsMovingOnGround() || IsFalling() || IsWallRunning());
}

bool UShooterMovementComponent::TryWallJump()
{
	if (!IsWallRunning())
	{
		return false;
	}

	const AShooterCharacter* Char = GetShooterCharacter();
	if (!IsValid(Char))
	{
		return false;
	}

	// Carry the player's momentum. While attached, velocity is already along the wall, so the
	// horizontal velocity direction IS the direction of travel.
	FVector ForwardDirection = Velocity;
	ForwardDirection.Z = 0.f;
	if (ForwardDirection.IsNearlyZero() && IsValid(UpdatedComponent))
	{
		ForwardDirection = UpdatedComponent->GetForwardVector();
	}
	ForwardDirection = ForwardDirection.GetSafeNormal();

	// Computed before EndWallRun clears WallRunNormal.
	const FVector LaunchVelocity =
		ForwardDirection * Char->WallJumpForwardSpeed
		+ WallRunNormal * Char->WallJumpOutwardSpeed
		+ FVector::UpVector * Char->WallJumpUpwardSpeed;

	EndWallRun(MOVE_Falling);
	Velocity = LaunchVelocity;

	// Returning true makes CheckJumpInput count this as a jump, so JumpCurrentCount lands on 1
	// and exactly one air jump remains - matching the pre-refactor behaviour.
	return true;
}

bool UShooterMovementComponent::DoJump(bool bReplayingMoves, float DeltaTime)
{
	if (TryWallJump())
	{
		return true;
	}

	// Jumping out of a slide ends it. Order is deliberate: Super::DoJump re-checks CanJump()
	// internally, and AShooterCharacter::CanJumpInternal only lifts the crouch block *while*
	// bSliding is set - clearing the slide first would flip that carve-out off while the capsule
	// is still crouched and the jump would be refused again.
	//
	// Runs inside CheckJumpInput, so it is part of the same predicted move as the jump itself.
	// Super::DoJump only writes Velocity.Z, so the slide's horizontal momentum carries into the
	// jump. ExitSlide starts the normal slide cooldown, so this can't be chained into a faster
	// slide-jump-slide loop.
	const bool bWasSliding = bSliding;

	const bool bJumped = Super::DoJump(bReplayingMoves, DeltaTime);

	if (bJumped && bWasSliding)
	{
		ExitSlide();
	}

	return bJumped;
}

/* --- Speed / braking --- */

float UShooterMovementComponent::GetMaxSpeed() const
{
	const AShooterCharacter* Char = GetShooterCharacter();
	if (!IsValid(Char))
	{
		return Super::GetMaxSpeed();
	}

	// Overridden rather than writing MaxWalkSpeed, so speed is a pure function of predicted
	// state and there is nothing to get out of sync between client and server.
	if (bSliding)
	{
		return Char->SlideLaunchSpeed;
	}
	if (IsWallRunning())
	{
		return Char->WallRunSpeed;
	}
	if (bSprinting && IsMovingOnGround())
	{
		return Char->SprintSpeed;
	}

	return Super::GetMaxSpeed();
}

float UShooterMovementComponent::GetMaxBrakingDeceleration() const
{
	const AShooterCharacter* Char = GetShooterCharacter();
	if (IsValid(Char) && bSliding && IsMovingOnGround())
	{
		return Char->SlideBrakingDeceleration;
	}

	return Super::GetMaxBrakingDeceleration();
}
