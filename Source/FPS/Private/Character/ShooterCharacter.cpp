// Copyright Druid Mechanics


#include "Character/ShooterCharacter.h"

#include "Character/ShooterMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "TimerManager.h"
#include "Camera/CameraComponent.h"
#include "Combat/CombatComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/WeaponData.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Net/UnrealNetwork.h"
#include "Weapon/Weapon.h"
#include "Animation/AnimInstance.h"

AShooterCharacter::AShooterCharacter(const FObjectInitializer& ObjectInitializer)
	// Swaps the stock CharacterMovementComponent for the predicted one. Same subobject name, so
	// BP_ShooterCharacter picks it up with no re-parenting or rewiring needed.
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UShooterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;

	ShooterMovement = Cast<UShooterMovementComponent>(GetCharacterMovement());

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
	bWallRunning = false;
	WallRunSide = EWallRunSide::None;

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
	SlideInputBufferTime = 0.35f;

	MaxJumpCount = 2;
	DoubleJumpZVelocity = 600.f;
	DoubleJumpRedirectAlpha = 0.8f;
	DoubleJumpDirectionalBoost = 250.f;
	DoubleJumpMinRedirectSpeed = 400.f;
	DefaultAirControl = 0.35f;

	WallRunMinSpeed = 400.f;
	WallRunSpeed = 900.f;
	WallRunAccelInterpSpeed = 3.f;
	WallRunDeceleration = 900.f;
	WallRunGravityScale = 0.15f;
	WallRunMaxFallSpeed = 200.f;
	WallRunAirControl = 0.2f;
	WallRunMaxDuration = 2.f;
	WallRunTraceDistance = 75.f;
	WallRunMaxWallNormalZ = 0.25f;
	WallRunMinGroundClearance = 100.f;
	WallRunMaxStartVerticalSpeed = 300.f;
	WallRunMinForwardInputDot = 0.5f;
	WallRunStickSpeed = 60.f;
	WallJumpForwardSpeed = 750.f;
	WallJumpOutwardSpeed = 250.f;
	WallJumpUpwardSpeed = 550.f;
	WallRunCooldown = 0.35f;
	WallRunSameWallCooldown = 1.f;
	WallRunCameraRoll = 15.f;
	WallRunCameraRollInterpSpeed = 8.f;

	// All slide / wall run timers and cooldowns now live on UShooterMovementComponent as
	// countdowns advanced by DeltaTime, because world time differs between client and server
	// and so can never be replayed correctly.
	CurrentCameraRoll = 0.f;
}

void AShooterCharacter::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Simulated proxies need these to drive their anim states. The owning client predicts
	// its own, so skipping the owner avoids stomping local prediction.
	DOREPLIFETIME_CONDITION(AShooterCharacter, bSprinting, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AShooterCharacter, bSliding, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AShooterCharacter, bWallRunning, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AShooterCharacter, WallRunSide, COND_SkipOwner);
}

void AShooterCharacter::BeginPlay()
{
	Super::BeginPlay();
	
	FirstPersonCamera->SetFieldOfView(DefaultFOV);

	StartingAimRotation = FRotator(0.f, GetBaseAimRotation().Yaw, 0.f);

	// Cached again here because a Blueprint-constructed instance builds its components after the
	// C++ constructor has run.
	ShooterMovement = Cast<UShooterMovementComponent>(GetCharacterMovement());

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		// Applied here rather than in the constructor so overrides on BP_ShooterCharacter take effect.
		// Sprint and slide speeds are no longer written onto MaxWalkSpeed - they come out of
		// UShooterMovementComponent::GetMaxSpeed, so speed is a pure function of predicted state.
		MoveComp->MaxWalkSpeed = WalkSpeed;
		MoveComp->AirControl = DefaultAirControl;
	}

	// ACharacter already tracks JumpCurrentCount against JumpMaxCount inside FSavedMove_Character,
	// so the extra air jump comes out properly client-predicted for free. Set here rather than in
	// the constructor so MaxJumpCount can be overridden on BP_ShooterCharacter.
	JumpMaxCount = FMath::Max(1, MaxJumpCount);
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
	UpdateCameraRoll(DeltaTime);
	CalculateTurnInPlaceParameters(DeltaTime);
	CalculateFABRIKSocketTransform();
}

void AShooterCharacter::UpdateMovementState()
{
	// All the actual decisions now happen inside UShooterMovementComponent, where they are
	// predicted. This only copies the result onto the replicated anim mirrors.
	if (!IsValid(ShooterMovement)) return;

	// Simulated proxies never run the prediction, so they keep whatever replication gave them.
	if (!IsLocallyControlled() && !HasAuthority()) return;

	bSprinting = ShooterMovement->IsSprinting();
	bSliding = ShooterMovement->IsSliding();
	bWallRunning = ShooterMovement->IsWallRunning();
	WallRunSide = ShooterMovement->GetWallRunSide();
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

int32 AShooterCharacter::GetReserveAmmo_Implementation() const
{
	return Combat->CurrentReserveAmmo;
}

void AShooterCharacter::Notify_CycleWeapon_Implementation()
{
	Combat->Notify_CycleWeapon();
}

void AShooterCharacter::Input_CycleWeapon()
{
	Combat->Initiate_CycleWeapon();
}

void AShooterCharacter::Notify_ReloadWeapon_Implementation()
{
	Combat->Notify_ReloadWeapon();
}

bool AShooterCharacter::DoDamage_Implementation(float DamageAmount, AActor* DamageInstigator)
{
	// Change health by damage amount
	
	const int32 MontageSelection = FMath::RandRange(0, HitReacts.Num() - 1);
	Multicast_HitReact(MontageSelection);
	
	return false;
}

void AShooterCharacter::Multicast_HitReact_Implementation(int32 MontageIndex)
{
	if (GetNetMode() != NM_DedicatedServer && !IsLocallyControlled())
	{
		if (HitReacts.IsValidIndex(MontageIndex))
		{
			GetMesh()->GetAnimInstance()->Montage_Play(HitReacts[MontageIndex]);
		}
	}
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
	if (!IsValid(ShooterMovement)) return;

	// Toggle, not hold. The movement component refuses to sprint while sliding or wall running,
	// so this only flips intent - it no longer touches any movement parameter directly.
	ShooterMovement->SetWantsToSprint(!ShooterMovement->WantsToSprint());
}

void AShooterCharacter::Input_Slide_Pressed()
{
	if (!IsValid(ShooterMovement)) return;

	// Latches intent. The movement component validates it inside the predicted update, which is
	// also where the airborne landing buffer is honoured.
	ShooterMovement->RequestSlide();
}

bool AShooterCharacter::IsSprinting_Implementation() const
{
	// Read live from the movement component, not from the replicated mirror, so a cancel is
	// visible on the same frame. UCombatComponent cancels sprint and then fires within a single
	// input event - a one frame lag here would drop the first shot.
	if (IsValid(ShooterMovement) && (IsLocallyControlled() || HasAuthority()))
	{
		return ShooterMovement->IsSprinting();
	}

	// Simulated proxies never run the prediction, so they fall back to the replicated mirror.
	return bSprinting;
}

bool AShooterCharacter::IsSliding_Implementation() const
{
	if (IsValid(ShooterMovement) && (IsLocallyControlled() || HasAuthority()))
	{
		return ShooterMovement->IsSliding();
	}
	return bSliding;
}

bool AShooterCharacter::IsWallRunning_Implementation() const
{
	if (IsValid(ShooterMovement) && (IsLocallyControlled() || HasAuthority()))
	{
		return ShooterMovement->IsWallRunning();
	}
	return bWallRunning;
}

void AShooterCharacter::CancelSprint_Implementation()
{
	if (!IsValid(ShooterMovement)) return;

	// Deliberately only touches the sprint intent - never the slide, the wall run or the jump.
	ShooterMovement->SetWantsToSprint(false);
}

void AShooterCharacter::CancelSlide_Implementation()
{
	if (!IsValid(ShooterMovement)) return;

	// Ends an in-progress slide early by withdrawing the crouch the slide holds. Predicted,
	// because it rides the base crouch flag rather than an RPC.
	ShooterMovement->RequestCancelSlide();
}

bool AShooterCharacter::CanJumpInternal_Implementation() const
{
	// ACharacter::CanJumpInternal_Implementation opens with `!bIsCrouched`, and a slide holds a
	// genuinely crouched capsule for its whole duration - so jumping out of a slide is refused
	// before it ever reaches DoJump. Lift only the crouch condition here; the movement
	// component's CanAttemptJump (which has its own slide carve-out) still has the final say,
	// and the jump count rule is preserved so this can't hand out extra air jumps.
	// bSliding is predicted state restored by FSavedMove_Shooter, so this replays correctly.
	if (IsValid(ShooterMovement) && ShooterMovement->IsSliding())
	{
		return ShooterMovement->CanAttemptJump() && JumpCurrentCount < JumpMaxCount;
	}

	return Super::CanJumpInternal_Implementation();
}

void AShooterCharacter::OnJumped_Implementation()
{
	Super::OnJumped_Implementation();

	// Runs inside CheckJumpInput on both the owning client and the server (including move replay),
	// so overriding the launch here stays inside CharacterMovement's prediction. GetCurrentAcceleration
	// is part of the saved move, so the redirect below replays identically on the server.
	// The wall jump is handled in UShooterMovementComponent::DoJump and lands on JumpCurrentCount
	// == 1, so it deliberately never reaches this redirect.
	if (JumpCurrentCount > 1)
	{
		if (UCharacterMovementComponent* MoveComp = GetCharacterMovement(); IsValid(MoveComp))
		{
			MoveComp->Velocity.Z = DoubleJumpZVelocity;

			// Directional air jump: rotate existing horizontal momentum onto the movement input
			// direction. Without this the air jump only ever changes Z, so reversing direction
			// mid-air has to fight the old momentum through air control alone - which is what
			// reads as "floaty" / no control.
			const FVector InputDirection = MoveComp->GetCurrentAcceleration().GetSafeNormal();
			if (!InputDirection.IsNearlyZero())
			{
				FVector HorizontalVelocity = MoveComp->Velocity;
				HorizontalVelocity.Z = 0.f;

				// Speed is preserved through the turn (with a floor), so a mid-air reversal keeps
				// pace instead of dumping the player's momentum.
				const float RedirectSpeed = FMath::Max(HorizontalVelocity.Size(), DoubleJumpMinRedirectSpeed);

				FVector Redirected = FMath::Lerp(
					HorizontalVelocity,
					InputDirection * RedirectSpeed,
					FMath::Clamp(DoubleJumpRedirectAlpha, 0.f, 1.f));

				Redirected += InputDirection * DoubleJumpDirectionalBoost;

				MoveComp->Velocity.X = Redirected.X;
				MoveComp->Velocity.Y = Redirected.Y;
			}
		}
	}
}

void AShooterCharacter::UpdateCameraRoll(float DeltaTime)
{
	// Purely local feedback - the camera only exists on the owning client.
	if (!IsLocallyControlled() || !IsValid(FirstPersonCamera)) return;

	float TargetRoll = 0.f;
	if (bWallRunning)
	{
		// Roll AWAY from the wall, so the view opens out from the surface rather than into it.
		// Wall on the right -> negative roll. The sign lives here so WallRunCameraRoll stays a
		// plain positive "degrees of tilt" value in the editor.
		TargetRoll = (WallRunSide == EWallRunSide::Right) ? -WallRunCameraRoll : WallRunCameraRoll;
	}

	// Interped both ways, so the roll eases back to level when the run ends.
	CurrentCameraRoll = FMath::FInterpTo(CurrentCameraRoll, TargetRoll, DeltaTime, WallRunCameraRollInterpSpeed);

	// Relative to the SpringArm, which is the component driven by control rotation - so this
	// adds roll without fighting the player's look input.
	FRotator CameraRotation = FirstPersonCamera->GetRelativeRotation();
	CameraRotation.Roll = CurrentCameraRoll;
	FirstPersonCamera->SetRelativeRotation(CameraRotation);
}
