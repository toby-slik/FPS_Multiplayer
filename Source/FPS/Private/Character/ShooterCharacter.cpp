// Copyright Druid Mechanics


#include "Character/ShooterCharacter.h"

#include "EnhancedInputComponent.h"
#include "TimerManager.h"
#include "Camera/CameraComponent.h"
#include "Combat/CombatComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/WeaponData.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Net/UnrealNetwork.h"
#include "Weapon/Weapon.h"

AShooterCharacter::AShooterCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	
	GetCharacterMovement()->MovementState.bCanCrouch = true;

	SpringArm = CreateDefaultSubobject<USpringArmComponent>("SpringArm");
	SpringArm->SetupAttachment(GetRootComponent());
	SpringArm->TargetArmLength = 0.f;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 15.f;
	SpringArm->bUsePawnControlRotation = true;
	
	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>("FirstPersonCamera");
	FirstPersonCamera->SetupAttachment(SpringArm);
	FirstPersonCamera->bUsePawnControlRotation = false;
	
	Mesh1P = CreateDefaultSubobject<USkeletalMeshComponent>("Mesh1P");
	Mesh1P->SetupAttachment(FirstPersonCamera);
	Mesh1P->bOnlyOwnerSee = true;
	Mesh1P->bOwnerNoSee = false;
	Mesh1P->bCastDynamicShadow = false;
	Mesh1P->bReceivesDecals = false;
	Mesh1P->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	Mesh1P->PrimaryComponentTick.TickGroup = TG_PrePhysics;
	
	GetMesh()->bOnlyOwnerSee = false;
	GetMesh()->bOwnerNoSee = true;
	GetMesh()->bReceivesDecals = false;
	
	Combat = CreateDefaultSubobject<UCombatComponent>("Combat");
	Combat->SetIsReplicated(true);
	
	DefaultFOV = 90.0f;
	TurningStatus = ETurnInPlace::NotTurning;
	bWeaponFirstReplicated = false;

	bSprinting = false;
	bSliding = false;

	WalkSpeed = 600.f;
	SprintSpeed = 900.f;
	SprintStopSpeed = 10.f;

	SlideMinStartSpeed = 500.f;
	SlideLaunchSpeed = 1250.f;
	SlideDuration = 0.9f;
	SlideEndSpeed = 350.f;
	SlideGroundFriction = 0.2f;
	SlideBrakingDeceleration = 500.f;
	SlideCooldown = 0.75f;

	// Far enough in the past that the cooldown never blocks the first slide of a life.
	LastSlideEndTime = -10000.f;
	CachedGroundFriction = 8.f;
	CachedBrakingDeceleration = 2048.f;
	CachedMaxWalkSpeedCrouched = 300.f;
}

void AShooterCharacter::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Simulated proxies need these to drive their anim states. The owning client predicts
	// its own, so skipping the owner avoids stomping local prediction.
	DOREPLIFETIME_CONDITION(AShooterCharacter, bSprinting, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AShooterCharacter, bSliding, COND_SkipOwner);
}

void AShooterCharacter::BeginPlay()
{
	Super::BeginPlay();
	
	FirstPersonCamera->SetFieldOfView(DefaultFOV);

	StartingAimRotation = FRotator(0.f, GetBaseAimRotation().Yaw, 0.f);

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		// Applied here rather than in the constructor so overrides on BP_ShooterCharacter take effect.
		MoveComp->MaxWalkSpeed = WalkSpeed;
		CachedGroundFriction = MoveComp->GroundFriction;
		CachedBrakingDeceleration = MoveComp->BrakingDecelerationWalking;
		CachedMaxWalkSpeedCrouched = MoveComp->MaxWalkSpeedCrouched;
	}
}

void AShooterCharacter::BeginDestroy()
{
	Super::BeginDestroy();
	
	if (IsValid(Combat))
	{
		Combat->DestroyInventory();
	}
}

FRotator AShooterCharacter::GetFixedRotation() const
{
	FRotator AimRotation = GetBaseAimRotation();
	if (AimRotation.Pitch > 90.f && !IsLocallyControlled())
	{
		// map pitch from [270, 360) to [-90, 0]
		const FVector2D InRange(270.f, 360.f);
		const FVector2D OutRange(-90.f, 0.f);
		AimRotation.Pitch = FMath::GetMappedRangeValueClamped(InRange, OutRange, AimRotation.Pitch);
	}
	
	return AimRotation;
}

bool AShooterCharacter::HasCurrentWeapon() const
{
	return IsValid(Combat) && Combat->CurrentWeapon != nullptr;
}

void AShooterCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	UpdateMovementState();
	CalculateTurnInPlaceParameters(DeltaTime);
	CalculateFABRIKSocketTransform();
}

void AShooterCharacter::UpdateMovementState()
{
	// Only the mover decides when its own states end; proxies just receive the replicated flags.
	if (!IsLocallyControlled() && !HasAuthority()) return;

	const UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!IsValid(MoveComp)) return;

	FVector GroundVelocity = GetVelocity();
	GroundVelocity.Z = 0.f;
	const float GroundSpeed = GroundVelocity.Size();

	// A slide decays to a stop rather than snapping out at a fixed time.
	if (bSliding && GroundSpeed < SlideEndSpeed)
	{
		StopSlide();
	}

	// Don't leave the character looping a sprint animation while standing still.
	// Owner-only: the server must not race the owning client's own sprint decisions.
	if (IsLocallyControlled() && bSprinting && !bSliding && !MoveComp->IsFalling())
	{
		const bool bNoMovementInput = MoveComp->GetCurrentAcceleration().IsNearlyZero();
		if (bNoMovementInput && GroundSpeed <= SprintStopSpeed)
		{
			Local_SetSprinting(false);
			Server_SetSprinting(false);
		}
	}
}

void AShooterCharacter::CalculateTurnInPlaceParameters(float DeltaTime)
{
	FVector Velocity = GetVelocity();
	Velocity.Z = 0.f;
	float Speed = Velocity.Size();
	bool bIsInAir = GetCharacterMovement()->IsFalling();
	
	if (Speed == 0.f && !bIsInAir) // standing still, not jumping
	{
		FRotator CurrentAimRotation(0.f, GetBaseAimRotation().Yaw, 0.f);
		// StartingAimRotation initially set in BeginPlay
		FRotator DeltaAimRotation = UKismetMathLibrary::NormalizedDeltaRotator(CurrentAimRotation, StartingAimRotation);
		AO_Yaw = DeltaAimRotation.Yaw;
		
		if (TurningStatus == ETurnInPlace::NotTurning)
		{
			InterpAO_Yaw = AO_Yaw;
		}
		
		TurnInPlace(DeltaTime); // interpolates the InterpAO_Yaw value to zero.
	}

	if (Speed > 0.f || bIsInAir)
	{
		StartingAimRotation = FRotator(0.f, GetBaseAimRotation().Yaw, 0.f);
		AO_Yaw = 0.f;
		
		FRotator AimRotation = GetBaseAimRotation();
		FRotator MovementRotation = UKismetMathLibrary::MakeRotFromX(GetVelocity());
		MovementOffsetYaw = UKismetMathLibrary::NormalizedDeltaRotator(MovementRotation, AimRotation).Yaw;
		TurningStatus = ETurnInPlace::NotTurning;
	}

	AO_Yaw *= -1.f;
}

void AShooterCharacter::TurnInPlace(float DeltaTime)
{
	if (AO_Yaw > 90.f)
	{
		TurningStatus = ETurnInPlace::Right;
	}
	else if (AO_Yaw < -90.f)
	{
		TurningStatus = ETurnInPlace::Left;
	}
	if (TurningStatus != ETurnInPlace::NotTurning) // we are turning
	{
		InterpAO_Yaw = FMath::FInterpTo(InterpAO_Yaw, 0.f, DeltaTime, 4.0f);
		AO_Yaw = InterpAO_Yaw;
		if (FMath::Abs(AO_Yaw) < 5.f)
		{
			TurningStatus = ETurnInPlace::NotTurning;
			StartingAimRotation = FRotator(0.f, GetBaseAimRotation().Yaw, 0.f);
		}
	}
}

void AShooterCharacter::CalculateFABRIKSocketTransform()
{
	if (IsValid(Combat) && IsValid(Combat->CurrentWeapon) && IsValid(Combat->CurrentWeapon->GetMesh3P()))
	{
		FABRIK_SocketTransform = Combat->CurrentWeapon->GetMesh3P()->GetSocketTransform("FABRIK_Socket", RTS_World);
		
		FVector OutLocation;
		FRotator OutRotation;
		GetMesh()->TransformToBoneSpace(
			"hand_r", 
			FABRIK_SocketTransform.GetLocation(), 
			FABRIK_SocketTransform.GetRotation().Rotator(), 
			OutLocation, 
			OutRotation);
		FABRIK_SocketTransform.SetLocation(OutLocation);
		FABRIK_SocketTransform.SetRotation(OutRotation.Quaternion());
	}
}

void AShooterCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* ShooterInputComponent = CastChecked<UEnhancedInputComponent>(PlayerInputComponent);
	
	ShooterInputComponent->BindAction(CycleWeaponAction, ETriggerEvent::Started, this, &ThisClass::Input_CycleWeapon);
	ShooterInputComponent->BindAction(FireWeaponAction, ETriggerEvent::Started, this, &ThisClass::Input_FireWeapon_Pressed);
	ShooterInputComponent->BindAction(FireWeaponAction, ETriggerEvent::Completed, this, &ThisClass::Input_FireWeapon_Released);
	ShooterInputComponent->BindAction(AimWeaponAction, ETriggerEvent::Started, this, &ThisClass::Input_Aim_Pressed);
	ShooterInputComponent->BindAction(AimWeaponAction, ETriggerEvent::Completed, this, &ThisClass::Input_Aim_Released);
	ShooterInputComponent->BindAction(ReloadWeaponAction, ETriggerEvent::Started, this, &ThisClass::Input_ReloadWeapon);

	// Sprint is a toggle, so it only binds Started - there is deliberately no Completed binding.
	ShooterInputComponent->BindAction(SprintAction, ETriggerEvent::Started, this, &ThisClass::Input_Sprint_Toggle);
	ShooterInputComponent->BindAction(SlideAction, ETriggerEvent::Started, this, &ThisClass::Input_Slide_Pressed);
}

void AShooterCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	if (IsValid(Combat))
	{
		Combat->SpawnInventory();
	}
}

void AShooterCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	
	if (IsValid(Combat))
	{
		Combat->InitializeWeaponWidgets();
	}
}

FName AShooterCharacter::GetWeaponAttachPoint_Implementation(const FGameplayTag& WeaponType) const
{
	checkf(Combat->WeaponData, TEXT("No Weapon Data Asset - Please fill out BP_ShooterCharacter"));
	return Combat->WeaponData->GripPoints.FindChecked(WeaponType);
}

USkeletalMeshComponent* AShooterCharacter::GetMesh1P_Implementation() const
{
	return Mesh1P;
}

USkeletalMeshComponent* AShooterCharacter::GetMesh3P_Implementation() const
{
	return GetMesh();
}

void AShooterCharacter::WeaponReplicated_Implementation()
{
	if (!bWeaponFirstReplicated)
	{
		bWeaponFirstReplicated = true;
		OnWeaponFirstReplicated.Broadcast(Combat->CurrentWeapon);
	}
}

AWeapon* AShooterCharacter::GetCurrentWeapon_Implementation()
{
	return Combat->CurrentWeapon;
}

void AShooterCharacter::Input_CycleWeapon()
{
	Combat->Initiate_CycleWeapon();
}

void AShooterCharacter::Input_ReloadWeapon()
{
	Combat->Initiate_ReloadWeapon();
}

void AShooterCharacter::Input_FireWeapon_Pressed()
{
	Combat->Initiate_FireWeapon_Pressed();
}

void AShooterCharacter::Input_FireWeapon_Released()
{
	Combat->Initiate_FireWeapon_Released();
}

void AShooterCharacter::Input_Aim_Pressed()
{
	// The sprint cancel lives in UCombatComponent::Initiate_Aim_Pressed so firing and aiming
	// share one gate. Aiming itself is never blocked - airborne, sliding and later wall-running
	// must all still be able to aim.
	Combat->Initiate_Aim_Pressed();
	OnAim(true);
}

void AShooterCharacter::Input_Aim_Released()
{
	Combat->Initiate_Aim_Released();
	OnAim(false);
}

void AShooterCharacter::Input_Sprint_Toggle()
{
	// Toggle, not hold.
	const bool bNewSprinting = !bSprinting;

	// Sliding owns the movement params until it finishes; queueing a sprint mid-slide would fight it.
	if (bNewSprinting && bSliding) return;

	Local_SetSprinting(bNewSprinting);
	Server_SetSprinting(bNewSprinting);
}

void AShooterCharacter::Local_SetSprinting(bool bNewSprinting)
{
	bSprinting = bNewSprinting;

	// While sliding, the slide owns MaxWalkSpeed. Cancelling sprint here (from aim or fire)
	// must change nothing about the slide in progress.
	if (bSliding) return;

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!IsValid(MoveComp)) return;

	MoveComp->MaxWalkSpeed = bSprinting ? SprintSpeed : WalkSpeed;
}

void AShooterCharacter::Server_SetSprinting_Implementation(bool bNewSprinting)
{
	Local_SetSprinting(bNewSprinting);
}

void AShooterCharacter::Input_Slide_Pressed()
{
	if (!CanStartSlide()) return;

	Local_StartSlide();
	Server_StartSlide();
}

bool AShooterCharacter::CanStartSlide() const
{
	if (bSliding) return false;

	// A slide can only ever come out of a sprint.
	if (!bSprinting) return false;

	const UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!IsValid(MoveComp) || MoveComp->IsFalling()) return false;

	// Sprint can be toggled on while standing still, so the speed check is what actually
	// prevents sliding from a standstill.
	FVector GroundVelocity = GetVelocity();
	GroundVelocity.Z = 0.f;
	if (GroundVelocity.Size() < SlideMinStartSpeed) return false;

	if (!IsValid(GetWorld())) return false;
	if (GetWorld()->GetTimeSeconds() - LastSlideEndTime < SlideCooldown) return false;

	return true;
}

void AShooterCharacter::Local_StartSlide()
{
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!IsValid(MoveComp)) return;

	// Entering a slide ends the sprint state, which is what unblocks firing and aiming
	// during the slide. Order matters: set bSliding first so Local_SetSprinting leaves
	// the movement params for the slide to own.
	bSliding = true;
	Local_SetSprinting(false);

	CachedGroundFriction = MoveComp->GroundFriction;
	CachedBrakingDeceleration = MoveComp->BrakingDecelerationWalking;
	CachedMaxWalkSpeedCrouched = MoveComp->MaxWalkSpeedCrouched;

	MoveComp->GroundFriction = SlideGroundFriction;
	MoveComp->BrakingDecelerationWalking = SlideBrakingDeceleration;

	// Raise the caps or the launch velocity is clamped straight back down.
	MoveComp->MaxWalkSpeed = SlideLaunchSpeed;
	MoveComp->MaxWalkSpeedCrouched = SlideLaunchSpeed;

	FVector SlideDirection = GetVelocity();
	SlideDirection.Z = 0.f;
	if (SlideDirection.IsNearlyZero())
	{
		SlideDirection = GetActorForwardVector();
	}
	MoveComp->Velocity = SlideDirection.GetSafeNormal() * SlideLaunchSpeed;

	Crouch();

	if (IsValid(GetWorld()))
	{
		GetWorld()->GetTimerManager().SetTimer(SlideTimer, this, &ThisClass::StopSlide, SlideDuration, false);
	}
}

void AShooterCharacter::Server_StartSlide_Implementation()
{
	// Re-validated on the server so a client cannot slide from a standstill or ignore the cooldown.
	if (!CanStartSlide()) return;

	Local_StartSlide();
}

void AShooterCharacter::StopSlide()
{
	if (!bSliding) return;

	bSliding = false;

	if (IsValid(GetWorld()))
	{
		GetWorld()->GetTimerManager().ClearTimer(SlideTimer);
		LastSlideEndTime = GetWorld()->GetTimeSeconds();
	}

	UnCrouch();

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!IsValid(MoveComp)) return;

	MoveComp->GroundFriction = CachedGroundFriction;
	MoveComp->BrakingDecelerationWalking = CachedBrakingDeceleration;
	MoveComp->MaxWalkSpeedCrouched = CachedMaxWalkSpeedCrouched;

	// Exit state is walking: sprint was consumed by the slide, so re-sprinting is a
	// deliberate second press. Combined with SlideCooldown this is the anti-spam guard.
	MoveComp->MaxWalkSpeed = WalkSpeed;
}

bool AShooterCharacter::IsSprinting_Implementation() const
{
	return bSprinting;
}

bool AShooterCharacter::IsSliding_Implementation() const
{
	return bSliding;
}

void AShooterCharacter::CancelSlide_Implementation()
{
	if (!bSliding) return;

	// Ends the slide early. StopSlide restores the movement params, uncrouches and starts
	// the cooldown, so a cancelled slide cannot be chained straight into another one.
	StopSlide();
	Server_StopSlide();
}

void AShooterCharacter::Server_StopSlide_Implementation()
{
	StopSlide();
}

void AShooterCharacter::CancelSprint_Implementation()
{
	if (!bSprinting) return;

	// Deliberately only touches the sprint state - never bSliding, never the jump.
	Local_SetSprinting(false);
	Server_SetSprinting(false);
}
