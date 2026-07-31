// Fill out your copyright notice in the Description page of Project Settings.


#include "Player/ShooterPlayerController.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interfaces/PlayerInterface.h"

AShooterPlayerController::AShooterPlayerController()
{
	bReplicates = true;
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
	const FVector2D InputAxisVector = InputActionValue.Get<FVector2D>();
	AddYawInput(InputAxisVector.X);
	AddPitchInput(InputAxisVector.Y);
}
