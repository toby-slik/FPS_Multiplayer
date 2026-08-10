// Copyright Druid Mechanics


#include "AI/ShooterAIBlackboard.h"

#include "AI/ShooterAIController.h"
#include "Character/ShooterCharacter.h"
#include "Combat/CombatComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Health/HealthComponent.h"
#include "Interfaces/PlayerInterface.h"
#include "Weapon/Weapon.h"

UShooterAIBlackboard::UShooterAIBlackboard()
{
	// Ticked by hand from AShooterAIController::Tick so the within-frame order is guaranteed: knowledge
	// first, then decisions, then locomotion, then aim.
	PrimaryComponentTick.bCanEverTick = false;

	SightRange = 8000.f;
	SightHalfAngleDegrees = 80.f;
	HearingRange = 4000.f;
	TargetRefreshInterval = 0.5f;

	bRawLineOfSight = false;
	bLineOfSightHeld = false;
	bTargetAcquired = false;

	LineOfSightHeldTime = 0.f;
	LineOfSightLostTime = 0.f;
	TimeSinceLineOfSight = TNumericLimits<float>::Max();
	TargetRefreshTimer = 0.f;

	LastKnownTargetLocation = FVector::ZeroVector;
	LastKnownTargetEyeLocation = FVector::ZeroVector;
	LastKnownTargetVelocity = FVector::ZeroVector;

	InstantStillness = 0.f;
	SmoothedStillness = 0.f;

	bWasTargetLive = false;
	bWasTargetAcquired = false;
	bWasMagEmpty = false;
	bLastDamageUnseen = false;
	DamageInterruptCooldownRemaining = 0.f;

	PendingInterrupt = EShooterAIInterrupt::None;
	LastConsumedInterrupt = EShooterAIInterrupt::None;
}

AShooterAIController* UShooterAIBlackboard::GetAIController() const
{
	return Cast<AShooterAIController>(GetOwner());
}

void UShooterAIBlackboard::ResetKnowledge()
{
	TargetPawn = nullptr;

	bRawLineOfSight = false;
	bLineOfSightHeld = false;
	bTargetAcquired = false;

	LineOfSightHeldTime = 0.f;
	LineOfSightLostTime = 0.f;
	TimeSinceLineOfSight = TNumericLimits<float>::Max();
	TargetRefreshTimer = 0.f;

	LastKnownTargetLocation = FVector::ZeroVector;
	LastKnownTargetEyeLocation = FVector::ZeroVector;
	LastKnownTargetVelocity = FVector::ZeroVector;

	InstantStillness = 0.f;
	SmoothedStillness = 0.f;

	bWasTargetLive = false;
	bWasTargetAcquired = false;
	bWasMagEmpty = false;
	bLastDamageUnseen = false;
	DamageInterruptCooldownRemaining = 0.f;

	PendingInterrupt = EShooterAIInterrupt::None;
	LastConsumedInterrupt = EShooterAIInterrupt::None;
}

void UShooterAIBlackboard::TickBlackboard(float DeltaTime)
{
	DamageInterruptCooldownRemaining = FMath::Max(0.f, DamageInterruptCooldownRemaining - DeltaTime);

	TargetRefreshTimer -= DeltaTime;
	if (TargetRefreshTimer <= 0.f)
	{
		TargetRefreshTimer = TargetRefreshInterval;
		RefreshTarget();
	}

	UpdateLineOfSight(DeltaTime);
	UpdateStillness(DeltaTime);
	UpdateInterruptTriggers();
}

/* --- Target selection --- */

void UShooterAIBlackboard::RefreshTarget()
{
	// A 1v1 game has exactly one opponent, so the player controller list *is* the candidate list. Iterating
	// it rather than running a perception component's stimuli queries is both cheaper and simpler, and it
	// cannot pick up a stray pawn that happens to implement the interface.
	const AShooterAIController* AI = GetAIController();
	if (!IsValid(AI) || !IsValid(GetWorld())) return;

	const APawn* Bot = AI->GetPawn();
	if (!IsValid(Bot)) return;

	APawn* Best = nullptr;
	float BestDistanceSquared = TNumericLimits<float>::Max();

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

bool UShooterAIBlackboard::HasTarget() const
{
	APawn* Target = TargetPawn.Get();
	if (!IsValid(Target)) return false;
	if (!Target->Implements<UPlayerInterface>()) return false;

	return IPlayerInterface::Execute_IsAlive(Target);
}

bool UShooterAIBlackboard::IsTargetLive() const
{
	if (!HasTarget()) return false;
	if (bLineOfSightHeld) return true;

	const AShooterAIController* AI = GetAIController();
	if (!IsValid(AI)) return false;

	return TimeSinceLineOfSight <= AI->GetDifficulty().TargetMemoryTime;
}

/* --- Line of sight --- */

bool UShooterAIBlackboard::TraceLineOfSight(const APawn* Target) const
{
	const AShooterAIController* AI = GetAIController();
	if (!IsValid(AI) || !IsValid(Target) || !IsValid(GetWorld())) return false;

	const APawn* Bot = AI->GetPawn();
	if (!IsValid(Bot)) return false;

	const FVector Start = AI->GetEyeLocation();
	const FVector End = Target->GetPawnViewLocation();

	if (FVector::DistSquared(Start, End) > FMath::Square(SightRange)) return false;

	// Vision cone. Taken from the control rotation rather than from the capsule so it matches where the bot
	// is actually looking, which is what makes flanking it work.
	const FVector ToTarget = (End - Start).GetSafeNormal();
	const FVector Facing = AI->GetControlRotation().Vector();
	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(SightHalfAngleDegrees));
	if ((ToTarget | Facing) < CosHalfAngle) return false;

	// Visibility channel on purpose: both the Pawn capsule profile and the CharacterMesh profile ignore it,
	// so a clear shot returns *no blocking hit* and only world geometry can occlude. Tracing on a channel
	// pawns block would report the target's own capsule as the obstruction.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BotLineOfSight), false, Bot);
	Params.AddIgnoredActor(Target);

	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
}

void UShooterAIBlackboard::UpdateLineOfSight(float DeltaTime)
{
	const AShooterAIController* AI = GetAIController();
	if (!IsValid(AI)) return;

	const FShooterBotDifficulty& D = AI->GetDifficulty();

	APawn* Target = TargetPawn.Get();
	if (!HasTarget())
	{
		bRawLineOfSight = false;
		bLineOfSightHeld = false;
		bTargetAcquired = false;
		LineOfSightHeldTime = 0.f;
		LineOfSightLostTime = 0.f;
		TimeSinceLineOfSight += DeltaTime;
		return;
	}

	bRawLineOfSight = TraceLineOfSight(Target);

	if (bRawLineOfSight)
	{
		LineOfSightLostTime = 0.f;
		LineOfSightHeldTime += DeltaTime;
		TimeSinceLineOfSight = 0.f;
		bLineOfSightHeld = true;

		LastKnownTargetLocation = Target->GetActorLocation();
		LastKnownTargetEyeLocation = Target->GetPawnViewLocation();
		LastKnownTargetVelocity = Target->GetVelocity();
	}
	else
	{
		TimeSinceLineOfSight += DeltaTime;

		// The debounce. Sight is only *reported* lost once it has been continuously lost for the grace
		// window; a single frame of a railing crossing the trace no longer drops the trigger, the ADS and the
		// sprint state all at once. LineOfSightHeldTime keeps accumulating through the grace so a target that
		// briefly clips behind cover does not cost the bot its whole reaction time.
		LineOfSightLostTime += DeltaTime;
		if (LineOfSightLostTime >= D.LineOfSightGraceTime)
		{
			bLineOfSightHeld = false;
			LineOfSightHeldTime = 0.f;
		}
		else
		{
			LineOfSightHeldTime += DeltaTime;
		}

		// Being shot at from outside the vision cone. A fired weapon is the loudest tell in the game, and a
		// bot that can be freely shot in the back reads as broken rather than as easy - so this hands it the
		// shooter's position without granting sight. It still has to turn, acquire and settle before it can
		// shoot back, which is where its difficulty is actually priced.
		if (HearingRange > 0.f && !bLineOfSightHeld)
		{
			const APawn* Bot = AI->GetPawn();
			if (IsValid(Bot) && Target->Implements<UPlayerInterface>())
			{
				if (const AWeapon* TargetWeapon = IPlayerInterface::Execute_GetCurrentWeapon(Target))
				{
					const bool bInEarshot =
						FVector::DistSquared(Target->GetActorLocation(), Bot->GetActorLocation())
						<= FMath::Square(HearingRange);

					if (bInEarshot && TargetWeapon->WeaponStatus == EWeaponStatus::Firing)
					{
						LastKnownTargetLocation = Target->GetActorLocation();
						LastKnownTargetEyeLocation = Target->GetPawnViewLocation();
						LastKnownTargetVelocity = Target->GetVelocity();
						TimeSinceLineOfSight = 0.f;
					}
				}
			}
		}
	}

	// The reaction delay, scaled by how still the target is. A player who has stopped moving is acquired in a
	// fraction of the time it takes to react to one who is sliding past - which is the whole point of the
	// stillness mechanic applied to the very first moment of a duel.
	const float EffectiveReaction = FMath::Max(0.f, D.ReactionTime * D.ReactionStillnessResponse.Evaluate(SmoothedStillness));
	bTargetAcquired = bLineOfSightHeld && LineOfSightHeldTime >= EffectiveReaction;
}

/* --- Stillness --- */

void UShooterAIBlackboard::UpdateStillness(float DeltaTime)
{
	const AShooterAIController* AI = GetAIController();
	if (!IsValid(AI)) return;

	const FShooterBotDifficulty& D = AI->GetDifficulty();
	APawn* Target = TargetPawn.Get();

	if (bRawLineOfSight && IsValid(Target))
	{
		// Speed term. A deadband at the bottom so ordinary sub-walk drift still reads as "standing still",
		// and a ceiling near sprint speed so everything above it is equally hard to track.
		const float Speed = Target->GetVelocity().Size2D();
		const float Span = FMath::Max(D.StillnessFullSpeed - D.StillnessStillSpeed, 1.f);
		const float MobilityAlpha = FMath::Clamp((Speed - D.StillnessStillSpeed) / Span, 0.f, 1.f);

		float Stillness = 1.f - MobilityAlpha;

		// Movement-tech modifiers, applied multiplicatively against what stillness is left. Multiplicative
		// rather than subtractive on purpose: a player who is *already* fast gains little extra protection
		// from also being airborne, but a player who slides from a standstill immediately loses most of the
		// lock the bot had built up. That is the read we want - tech is a reset, not a bonus.
		if (Target->Implements<UPlayerInterface>())
		{
			if (IPlayerInterface::Execute_IsWallRunning(Target))
			{
				Stillness *= (1.f - FMath::Clamp(D.StillnessWallRunPenalty, 0.f, 1.f));
			}
			if (IPlayerInterface::Execute_IsSliding(Target))
			{
				Stillness *= (1.f - FMath::Clamp(D.StillnessSlidePenalty, 0.f, 1.f));
			}
			if (IPlayerInterface::Execute_IsAirborne(Target))
			{
				Stillness *= (1.f - FMath::Clamp(D.StillnessAirbornePenalty, 0.f, 1.f));
			}
		}

		InstantStillness = FMath::Clamp(Stillness, 0.f, 1.f);
	}
	else
	{
		// Unseen. The bot is not allowed to know what the player is doing behind a wall, so it assumes they
		// are repositioning - which means a re-peek never hands it a settled, fully locked-on aim.
		InstantStillness = FMath::Clamp(D.StillnessWhenUnseen, 0.f, 1.f);
	}

	// Asymmetric exponential moving average. Rising toward "still" is slow (the player has to genuinely
	// commit to standing still before it counts); falling is fast (moving again must pay off inside a beat,
	// or the player cannot feel the cause and effect at all). Framerate-independent by construction.
	const float TimeConstant = (InstantStillness > SmoothedStillness) ? D.StillnessRiseTime : D.StillnessFallTime;
	const float Alpha = 1.f - FMath::Exp(-DeltaTime / FMath::Max(TimeConstant, KINDA_SMALL_NUMBER));

	SmoothedStillness = FMath::Clamp(SmoothedStillness + (InstantStillness - SmoothedStillness) * Alpha, 0.f, 1.f);
}

/* --- Interrupts --- */

void UShooterAIBlackboard::UpdateInterruptTriggers()
{
	const bool bLive = IsTargetLive();

	// Edge-triggered, all of them. A level-triggered interrupt would fire every frame the condition holds and
	// would therefore invalidate the current action every frame - i.e. it would reintroduce exactly the
	// per-frame re-decision that the commitment model exists to remove.
	if (bWasTargetLive && !bLive)
	{
		RaiseInterrupt(EShooterAIInterrupt::TargetLost);
	}
	if (!bWasTargetAcquired && bTargetAcquired)
	{
		RaiseInterrupt(EShooterAIInterrupt::TargetAcquired);
	}

	const bool bMagEmpty = GetMagAmmo() <= 0 && CanReload();
	if (!bWasMagEmpty && bMagEmpty)
	{
		RaiseInterrupt(EShooterAIInterrupt::WeaponNeedsReload);
	}

	bool bHealthCritical = false;
	if (const AShooterAIController* AI = GetAIController())
	{
		bHealthCritical = GetSelfHealthFraction() <= AI->GetDifficulty().RetreatHealthFraction;
	}
	if (!bWasHealthCritical && bHealthCritical)
	{
		RaiseInterrupt(EShooterAIInterrupt::HealthCritical);
	}

	bWasTargetLive = bLive;
	bWasTargetAcquired = bTargetAcquired;
	bWasMagEmpty = bMagEmpty;
	bWasHealthCritical = bHealthCritical;
}

void UShooterAIBlackboard::RaiseInterrupt(EShooterAIInterrupt Interrupt)
{
	if (Interrupt == EShooterAIInterrupt::None) return;

	// Declaration order is priority order, so the lower value wins. Several interrupts firing on one frame is
	// ordinary - being shot while your mag runs dry - and the chapter resolves it with a fixed priority list
	// rather than a queue, because only one of them can change what you do next anyway.
	if (PendingInterrupt == EShooterAIInterrupt::None || Interrupt < PendingInterrupt)
	{
		PendingInterrupt = Interrupt;
	}
}

void UShooterAIBlackboard::RegisterDamageFrom(AActor* DamageInstigator)
{
	// Rate limited so sustained automatic fire cannot re-select the action on every round. Without this the
	// bot never reaches an action's max duration while being shot at, which is the worst possible moment for
	// it to stop committing to anything.
	if (DamageInterruptCooldownRemaining > 0.f) return;

	const AShooterAIController* AI = GetAIController();
	if (!IsValid(AI)) return;

	DamageInterruptCooldownRemaining = AI->GetDifficulty().DamageInterruptCooldown;
	bLastDamageUnseen = !bLineOfSightHeld;

	// A round landing on you is a real observation of where the shooter is. Treating it as one is what lets
	// the bot turn around when it is shot in the back: the target becomes live again, so the aim layer leaves
	// Search and swings onto the remembered position, and a Hunt now walks at a real point.
	APawn* Target = TargetPawn.Get();
	if (IsValid(Target) && DamageInstigator == Target)
	{
		LastKnownTargetLocation = Target->GetActorLocation();
		LastKnownTargetEyeLocation = Target->GetPawnViewLocation();
		LastKnownTargetVelocity = Target->GetVelocity();
		TimeSinceLineOfSight = 0.f;
	}

	RaiseInterrupt(EShooterAIInterrupt::TookDamage);
}

EShooterAIInterrupt UShooterAIBlackboard::ConsumeInterrupt()
{
	// An interrupt lasts exactly one update. Whoever consumes it is the one reacting to it.
	const EShooterAIInterrupt Consumed = PendingInterrupt;
	PendingInterrupt = EShooterAIInterrupt::None;

	if (Consumed != EShooterAIInterrupt::None)
	{
		LastConsumedInterrupt = Consumed;
	}

	return Consumed;
}

/* --- Function-based queries --- */

float UShooterAIBlackboard::GetDistanceToTarget() const
{
	const AShooterAIController* AI = GetAIController();
	const APawn* Target = TargetPawn.Get();
	if (!IsValid(AI) || !IsValid(Target)) return TNumericLimits<float>::Max();

	return FVector::Dist(AI->GetEyeLocation(), Target->GetPawnViewLocation());
}

float UShooterAIBlackboard::GetAngleToTargetDegrees() const
{
	const AShooterAIController* AI = GetAIController();
	const APawn* Target = TargetPawn.Get();
	if (!IsValid(AI) || !IsValid(Target)) return 180.f;

	const FVector ToTarget = (Target->GetPawnViewLocation() - AI->GetEyeLocation()).GetSafeNormal();
	if (ToTarget.IsNearlyZero()) return 180.f;

	const FVector Facing = AI->GetControlRotation().Vector();
	return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(ToTarget | Facing, -1.f, 1.f)));
}

float UShooterAIBlackboard::GetAngleToLastKnownDegrees() const
{
	const AShooterAIController* AI = GetAIController();
	if (!IsValid(AI)) return 180.f;

	const FVector ToPoint = (LastKnownTargetEyeLocation - AI->GetEyeLocation()).GetSafeNormal();
	if (ToPoint.IsNearlyZero()) return 180.f;

	const FVector Facing = AI->GetControlRotation().Vector();
	return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(ToPoint | Facing, -1.f, 1.f)));
}

FVector UShooterAIBlackboard::GetPredictedTargetEyeLocation(float LeadSeconds) const
{
	const APawn* Target = TargetPawn.Get();
	if (IsValid(Target) && bRawLineOfSight)
	{
		return Target->GetPawnViewLocation() + Target->GetVelocity() * LeadSeconds;
	}

	return LastKnownTargetEyeLocation;
}

float UShooterAIBlackboard::GetSelfHealthFraction() const
{
	const AShooterAIController* AI = GetAIController();
	if (!IsValid(AI)) return 1.f;

	if (const UHealthComponent* BotHealth = UHealthComponent::FindHealthComponent(AI->GetPawn()))
	{
		return BotHealth->GetHealthNormalized();
	}
	return 1.f;
}

int32 UShooterAIBlackboard::GetMagAmmo() const
{
	const AShooterAIController* AI = GetAIController();
	if (!IsValid(AI)) return 0;

	const UCombatComponent* Combat = AI->GetCombat();
	if (!IsValid(Combat) || !IsValid(Combat->CurrentWeapon)) return 0;

	return Combat->CurrentWeapon->Ammo;
}

bool UShooterAIBlackboard::CanReload() const
{
	const AShooterAIController* AI = GetAIController();
	if (!IsValid(AI)) return false;

	const UCombatComponent* Combat = AI->GetCombat();
	if (!IsValid(Combat) || !IsValid(Combat->CurrentWeapon)) return false;

	return Combat->CurrentReserveAmmo > 0;
}
