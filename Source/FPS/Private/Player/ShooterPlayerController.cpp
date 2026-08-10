// Fill out your copyright notice in the Description page of Project Settings.


#include "Player/ShooterPlayerController.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedInputSubsystems.h"
#include "Combat/CombatComponent.h"
#include "Engine/LocalPlayer.h"
#include "Weapon/Weapon.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interfaces/PlayerInterface.h"

namespace
{
	/** Parked value for TimeSinceLastViewPunch while nothing is outstanding - any value past every
	 *  plausible ViewRecoveryDelay, so the first shot of a burst is never met by a stale expired delay. */
	constexpr float RecoilIdleTime = 1.0e6f;
}

AShooterPlayerController::AShooterPlayerController()
{
	bReplicates = true;
	bPawnAlive = true;

	RecoilPendingPitch = 0.f;
	RecoilPendingYaw = 0.f;
	RecoilStandingPitch = 0.f;
	RecoilRecoverablePitch = 0.f;
	RecoilRecoverableYaw = 0.f;
	TimeSinceLastViewPunch = RecoilIdleTime;
}

void AShooterPlayerController::AddViewRecoil(float PitchUp, float Yaw)
{
	const AWeapon* Weapon = nullptr;
	if (const UCombatComponent* Combat = UCombatComponent::FindCombatComponent(GetPawn()); IsValid(Combat))
	{
		Weapon = Combat->CurrentWeapon;
	}
	if (!IsValid(Weapon)) return;

	const FRecoilParams& Params = Weapon->RecoilParams;

	// Clamped against what recoil has already got standing, so a long burst tapers off at the ceiling
	// instead of walking the crosshair into the sky.
	const float Ceiling = FMath::Max(Params.ViewPunchMaxAccumulatedPitch, 0.f);
	const float Headroom = FMath::Max(0.f, Ceiling - (RecoilStandingPitch + RecoilPendingPitch));
	const float PitchToAdd = FMath::Clamp(PitchUp, 0.f, Headroom);

	RecoilPendingPitch += PitchToAdd;
	RecoilPendingYaw += Yaw;

	// Only the fraction the designer marked as automatic is banked for return. The remainder is left for
	// the player to pull down by hand, which is what makes recoil control a skill rather than a formality.
	const float RecoveryFraction = FMath::Clamp(Params.ViewRecoveryFraction, 0.f, 1.f);
	RecoilRecoverablePitch += PitchToAdd * RecoveryFraction;
	RecoilRecoverableYaw += Yaw * RecoveryFraction;

	TimeSinceLastViewPunch = 0.f;
}

void AShooterPlayerController::ResetViewRecoil()
{
	RecoilPendingPitch = 0.f;
	RecoilPendingYaw = 0.f;
	RecoilStandingPitch = 0.f;
	RecoilRecoverablePitch = 0.f;
	RecoilRecoverableYaw = 0.f;
	TimeSinceLastViewPunch = RecoilIdleTime;
}

void AShooterPlayerController::UpdateRotation(float DeltaTime)
{
	// Sampled before Super, which is the only point in the frame where the player's own look for this
	// frame is fully accumulated and not yet spent.
	const FRotator PlayerLookThisFrame = RotationInput;

	RotationInput += ConsumeViewRecoil(DeltaTime, PlayerLookThisFrame);

	Super::UpdateRotation(DeltaTime);
}

FRotator AShooterPlayerController::ConsumeViewRecoil(float DeltaTime, const FRotator& PlayerLookThisFrame)
{
	if (DeltaTime <= 0.f) return FRotator::ZeroRotator;

	const AWeapon* Weapon = nullptr;
	if (const UCombatComponent* Combat = UCombatComponent::FindCombatComponent(GetPawn()); IsValid(Combat))
	{
		Weapon = Combat->CurrentWeapon;
	}

	const bool bIdle = FMath::IsNearlyZero(RecoilPendingPitch) && FMath::IsNearlyZero(RecoilPendingYaw)
		&& FMath::IsNearlyZero(RecoilRecoverablePitch) && FMath::IsNearlyZero(RecoilRecoverableYaw)
		&& FMath::IsNearlyZero(RecoilStandingPitch);

	if (!IsValid(Weapon) || bIdle)
	{
		// Nothing outstanding, so there is nothing to time either. Left ticking it would let a stale
		// delay expire while the weapon is stowed and fire recovery at the first shot of the next burst.
		if (bIdle) TimeSinceLastViewPunch = RecoilIdleTime;
		return FRotator::ZeroRotator;
	}

	const FRecoilParams& Params = Weapon->RecoilParams;
	TimeSinceLastViewPunch += DeltaTime;

	FRotator Delta = FRotator::ZeroRotator;

	// --- Feed the queued kick onto the view -------------------------------------------------------
	// Interped rather than snapped so a shot reads as a shove with weight. FInterpTo returns what is left
	// after the step, so the difference is exactly this frame's share.
	{
		const float InterpSpeed = FMath::Max(Params.ViewPunchInterpSpeed, 0.1f);

		const float NextPendingPitch = FMath::FInterpTo(RecoilPendingPitch, 0.f, DeltaTime, InterpSpeed);
		const float NextPendingYaw = FMath::FInterpTo(RecoilPendingYaw, 0.f, DeltaTime, InterpSpeed);

		const float PitchStep = RecoilPendingPitch - NextPendingPitch;
		const float YawStep = RecoilPendingYaw - NextPendingYaw;

		RecoilPendingPitch = NextPendingPitch;
		RecoilPendingYaw = NextPendingYaw;

		Delta.Pitch += PitchStep;
		Delta.Yaw += YawStep;
		RecoilStandingPitch += PitchStep;
	}

	// --- Let manual correction retire recovery credit ---------------------------------------------
	// The single most important rule in the whole system: if the player has already pulled the view down
	// themselves, automatic recovery must not then pull it down again. Without this the mouse and the
	// recoil system fight each other and aiming feels like it is being taken away from the player.
	{
		const float ManualDownPitch = FMath::Max(0.f, -PlayerLookThisFrame.Pitch);
		if (ManualDownPitch > 0.f)
		{
			const float Consumed = FMath::Min(ManualDownPitch, RecoilRecoverablePitch);
			RecoilRecoverablePitch -= Consumed;
			RecoilStandingPitch = FMath::Max(0.f, RecoilStandingPitch - ManualDownPitch);
		}

		// Yaw has no privileged direction, so credit is retired by any manual turn that opposes the
		// outstanding drift rather than by one specific sign.
		if (!FMath::IsNearlyZero(RecoilRecoverableYaw) &&
			FMath::Sign(PlayerLookThisFrame.Yaw) == -FMath::Sign(RecoilRecoverableYaw))
		{
			const float ManualYaw = FMath::Abs(PlayerLookThisFrame.Yaw);
			const float OutstandingYaw = FMath::Abs(RecoilRecoverableYaw);
			const float RemainingYaw = FMath::Max(0.f, OutstandingYaw - ManualYaw);
			RecoilRecoverableYaw = FMath::Sign(RecoilRecoverableYaw) * RemainingYaw;
		}
	}

	// --- Automatic recovery ------------------------------------------------------------------------
	// Held off until the burst has actually stopped, so recovery never drags against rounds still going
	// out. This is why ViewRecoveryDelay must stay below the weapon's FireTime for a slow weapon to
	// recover between its own shots, and above it for an automatic to hold its climb through a burst.
	if (TimeSinceLastViewPunch >= FMath::Max(Params.ViewRecoveryDelay, 0.f))
	{
		const float RecoverySpeed = FMath::Max(Params.ViewRecoverySpeed, 0.1f);

		const float NextRecoverablePitch = FMath::FInterpTo(RecoilRecoverablePitch, 0.f, DeltaTime, RecoverySpeed);
		const float NextRecoverableYaw = FMath::FInterpTo(RecoilRecoverableYaw, 0.f, DeltaTime, RecoverySpeed);

		const float PitchReturn = RecoilRecoverablePitch - NextRecoverablePitch;
		const float YawReturn = RecoilRecoverableYaw - NextRecoverableYaw;

		RecoilRecoverablePitch = NextRecoverablePitch;
		RecoilRecoverableYaw = NextRecoverableYaw;

		Delta.Pitch -= PitchReturn;
		Delta.Yaw -= YawReturn;
		RecoilStandingPitch = FMath::Max(0.f, RecoilStandingPitch - PitchReturn);

		if (FMath::IsNearlyZero(RecoilRecoverablePitch, 0.001f)) RecoilRecoverablePitch = 0.f;
		if (FMath::IsNearlyZero(RecoilRecoverableYaw, 0.001f)) RecoilRecoverableYaw = 0.f;
	}

	if (FMath::IsNearlyZero(RecoilPendingPitch, 0.001f)) RecoilPendingPitch = 0.f;
	if (FMath::IsNearlyZero(RecoilPendingYaw, 0.001f)) RecoilPendingYaw = 0.f;
	if (FMath::IsNearlyZero(RecoilStandingPitch, 0.001f)) RecoilStandingPitch = 0.f;

	return Delta;
}

void AShooterPlayerController::BeginPlay()
{
	Super::BeginPlay();
	
	UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
	if (IsValid(Subsystem))
	{
		Subsystem->AddMappingContext(ShooterIMC, 0);
	}
}

void AShooterPlayerController::SetPawn(APawn* InPawn)
{
	Super::SetPawn(InPawn);

	// A pawn arriving is the only thing that clears the death gate. Losing one re-arms it, which covers the
	// window between UnPossess and the next spawn as well as the spectating case.
	const bool bHasPawn = IsValid(InPawn);
	bPawnAlive = bHasPawn;

	if (bHasPawn)
	{
		// Recoil state belongs to the body that generated it - see the note in AShooterCharacter::OnDeathStarted.
		ResetViewRecoil();
	}
}

void AShooterPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	
	UEnhancedInputComponent* ShooterInputComponent = CastChecked<UEnhancedInputComponent>(InputComponent);
	ShooterInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ThisClass::Input_Move);
	ShooterInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &ThisClass::Input_Look);
	// Started, not Triggered. IA_Jump is a Boolean action with an empty Triggers array, so
	// Triggered fires every frame Space is held - which retriggered the jump each frame and
	// burned the air jump immediately after take-off. Started fires once per press.
	ShooterInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ThisClass::Input_Jump);
	// Started, not Triggered: IA_Crouch is a Boolean action with no explicit trigger, so
	// Triggered fires every frame the key is held. Against the toggle in Input_Crouch that
	// flipped bWantsToCrouch once per frame, which read as crouch/stand flickering.
	ShooterInputComponent->BindAction(CrouchAction, ETriggerEvent::Started, this, &ThisClass::Input_Crouch);
	
	
	
}
 
void AShooterPlayerController::Input_Crouch()
{
	ACharacter* ControlledCharacter = GetCharacter();
	if (!IsValid(ControlledCharacter)) return;
	if (!bPawnAlive) return;
	
	// Slide shares this key with crouch. A second press during a slide cancels it rather than
	// toggling crouch underneath it. The cancel withdraws the crouch the slide holds, so the
	// player ends up standing - and because it rides the base crouch flag, it stays predicted.
	if (ControlledCharacter->Implements<UPlayerInterface>() && IPlayerInterface::Execute_IsSliding(ControlledCharacter))
	{
		IPlayerInterface::Execute_CancelSlide(ControlledCharacter);
		return;
	}

	if (UCharacterMovementComponent* CMC = ControlledCharacter->GetCharacterMovement(); IsValid(CMC))
	{
		CMC->bWantsToCrouch = !CMC->bWantsToCrouch;
	}

}

void AShooterPlayerController::Input_Jump()
{
	ACharacter* ControlledCharacter = GetCharacter();
	if (!IsValid(ControlledCharacter)) return;
	if (!bPawnAlive) return;
	UCharacterMovementComponent* CMC = ControlledCharacter->GetCharacterMovement();
	if (!IsValid(CMC)) return;

	const bool bSliding =
		ControlledCharacter->Implements<UPlayerInterface>() &&
		IPlayerInterface::Execute_IsSliding(ControlledCharacter);

	// Stand up out of a plain crouch instead of jumping.
	// Deliberately does NOT apply to a slide - a slide holds bWantsToCrouch for its whole
	// duration, so this branch used to swallow the entire jump press. It also no longer applies
	// while airborne, where a lingering crouch intent would silently block the air jump.
	if (CMC->bWantsToCrouch && !bSliding && CMC->IsMovingOnGround())
	{
		CMC->bWantsToCrouch = false;
		return;
	}

	// Release a lingering airborne crouch intent so it can't block the air jump. Deliberately
	// skipped while sliding: the slide owns bWantsToCrouch and reads its withdrawal as a cancel,
	// so clearing it here would cancel the slide on a jump press that then failed for some other
	// reason. The slide already has its own carve-out in CanAttemptJump, so it doesn't need this.
	if (!bSliding)
	{
		CMC->bWantsToCrouch = false;
	}

	// bWantsToCrouch is a base compressed flag and Jump() sets bPressedJump, so both survive
	// into the saved move and replay correctly.
	ControlledCharacter->Jump();
}

void AShooterPlayerController::Input_Move(const FInputActionValue& InputActionValue)
{
	if (!bPawnAlive) return;
	
	const FVector2D InputAxisVector = InputActionValue.Get<FVector2D>();
	const FRotator Rotation = GetControlRotation();
	const FRotator YawRotaton(0.f, Rotation.Yaw, 0.f);
	
	const FVector ForwardDirection = FRotationMatrix(Rotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(Rotation).GetUnitAxis(EAxis::Y);
	
	if (APawn* ControlledPawn = GetPawn())
	{
		ControlledPawn->AddMovementInput(ForwardDirection, InputAxisVector.Y);
		ControlledPawn->AddMovementInput(RightDirection, InputAxisVector.X);
	}
	
}

void AShooterPlayerController::Input_Look(const FInputActionValue& InputActionValue)
{
	if (!bPawnAlive) return;
	
	const FVector2D InputAxisVector = InputActionValue.Get<FVector2D>();

	// On top of the FOV Scaling modifier on IA_Look, which already slows turning by however far the
	// weapon pulls the FOV in. This is the per-weapon trim on that.
	float LookScale = 1.f;
	if (const UCombatComponent* Combat = UCombatComponent::FindCombatComponent(GetPawn()); IsValid(Combat) && Combat->bAiming && IsValid(Combat->CurrentWeapon))
	{
		LookScale = Combat->CurrentWeapon->GetEffectiveAimLookSensitivityScale();
	}

	AddYawInput(InputAxisVector.X * LookScale);
	AddPitchInput(InputAxisVector.Y * LookScale);
}
