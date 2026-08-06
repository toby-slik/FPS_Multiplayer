// Copyright Druid Mechanics


#include "Character/ShooterCharacter.h"

#include "Character/ShooterMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "TimerManager.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraShakeBase.h"
#include "Camera/PlayerCameraManager.h"
#include "Combat/CombatComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/WeaponData.h"
#include "CollisionQueryParams.h"
#include "ShooterGameModeBase.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Health/HealthComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Net/UnrealNetwork.h"
#include "Weapon/Weapon.h"
#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "FPS/FPS.h"
#include "Kismet/GameplayStatics.h"
#include "Player/ShooterPlayerController.h"

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
	
	Health = CreateDefaultSubobject<UHealthComponent>("Health");
	Health->SetIsReplicated(true);
	
	DefaultFOV = 90.0f;
	TurningStatus = ETurnInPlace::NotTurning;
	bWeaponFirstReplicated = false;
	RespawnTime = 3.f;

	// PA_Mannequin body names, verified against SKM_Manny's physics asset - these are the bodies a
	// weapon trace can actually report in Hit.BoneName. A name with no body would silently never hit.
	HeadshotBones = { FName("head"), FName("neck_02"), FName("neck_01") };

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

	CameraShakeAmplitude = 0.f;
	CameraShakeFrequency = 0.f;
	CameraShakeTotalDuration = 0.f;
	CameraShakeTimeRemaining = 0.f;
	CameraShakePhasePitch = 0.f;
	CameraShakePhaseYaw = 0.f;
	CameraShakePhaseRoll = 0.f;
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
	
	Health->OnDeathStarted.AddDynamic(this, &ThisClass::OnDeathStarted);
	
	FirstPersonCamera->SetFieldOfView(DefaultFOV);

	StartingAimRotation = FRotator(0.f, GetBaseAimRotation().Yaw, 0.f);
	
	if (AShooterPlayerController* PC = Cast<AShooterPlayerController>(GetController()); IsValid(PC))
	{
		PC->bPawnAlive = true;
	}

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

void AShooterCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// CombatComponent owns the inventory and tears it down from its own EndPlay. Actor teardown belongs
	// here rather than BeginDestroy, which runs after replicated references may already be invalid.
	Super::EndPlay(EndPlayReason);
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
	UpdateCameraOffsets(DeltaTime);
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
	if (!IsValid(Combat) || !IsValid(Combat->WeaponData))
	{
		UE_LOG(LogTemp, Error, TEXT("%s has no weapon data asset"), *GetNameSafe(this));
		return NAME_None;
	}

	if (const FName* AttachPoint = Combat->WeaponData->GripPoints.Find(WeaponType))
	{
		return *AttachPoint;
	}

	UE_LOG(LogTemp, Error, TEXT("No grip point configured for weapon tag %s on %s"),
		*WeaponType.ToString(), *GetNameSafe(this));
	return NAME_None;
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

void AShooterCharacter::OnDeathStarted()
{
	if (HasAuthority())
	{
		Combat->DestroyInventory();
		GetWorld()->GetTimerManager().SetTimer(DeathTimer, this, &ThisClass::DeathTimerFinished, RespawnTime);
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		DeathEffects();
		if (AShooterPlayerController* PC = Cast<AShooterPlayerController>(GetController()); IsValid(PC))
		{
			DisableInput(PC);
			if (PC->IsLocalController())
			{
				PC->bPawnAlive = false;

				// Outstanding recoil has to go with the pawn. Input is disabled from here, so nothing
				// would consume the queued kick, and it would otherwise be waiting to fire on the
				// respawned pawn's first frame - the controller survives the death, the pawn does not.
				PC->ResetViewRecoil();
			}
		}
		ClearCameraShake();
	}
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	GetCapsuleComponent()->SetCollisionResponseToChannel(FPSTraceChannels::ECC_Weapon, ECR_Ignore);
	GetMesh()->SetCollisionResponseToChannel(FPSTraceChannels::ECC_Weapon, ECR_Ignore);
}

void AShooterCharacter::DeathTimerFinished()
{
	AShooterGameModeBase* GM = Cast<AShooterGameModeBase>(UGameplayStatics::GetGameMode(this));
	if (IsValid(GM))
	{
		GM->RequestRespawn(this, GetController());
	}
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
	if (!IsValid(Health)) return false;

	const bool bLethal = Health->ChangeHealthByAmount(-DamageAmount, DamageInstigator);

	if (!HitReacts.IsEmpty())
	{
		const int32 MontageSelection = FMath::RandRange(0, HitReacts.Num() - 1);
		Multicast_HitReact(MontageSelection);
	}

	return bLethal;
}

bool AShooterCharacter::IsAlive_Implementation() const
{
	if (!IsValid(Health)) return false;

	return Health->DeathState == EDeathState::NotDead;
}

TArray<FName> AShooterCharacter::GetHeadshotBones_Implementation() const
{
	return HeadshotBones;
}

void AShooterCharacter::Multicast_HitReact_Implementation(int32 MontageIndex)
{
	if (GetNetMode() != NM_DedicatedServer && !IsLocallyControlled())
	{
		if (HitReacts.IsValidIndex(MontageIndex))
		{
			UAnimInstance* AnimInstance = IsValid(GetMesh()) ? GetMesh()->GetAnimInstance() : nullptr;
			if (IsValid(AnimInstance) && IsValid(HitReacts[MontageIndex]))
			{
				AnimInstance->Montage_Play(HitReacts[MontageIndex]);
			}
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

void AShooterCharacter::UpdateCameraOffsets(float DeltaTime)
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

	const FRotator ShakeOffset = AdvanceCameraShake(DeltaTime);

	// Relative to the SpringArm, which is the component driven by control rotation - so this
	// adds roll without fighting the player's look input.
	//
	// Set outright rather than read-modify-write: both terms are recomputed in full every frame, so
	// composing them onto whatever was written last frame would integrate the shake instead of replacing
	// it and the camera would walk away from level.
	FRotator CameraRotation;
	CameraRotation.Pitch = ShakeOffset.Pitch;
	CameraRotation.Yaw = ShakeOffset.Yaw;
	CameraRotation.Roll = CurrentCameraRoll + ShakeOffset.Roll;
	FirstPersonCamera->SetRelativeRotation(CameraRotation);
}

bool AShooterCharacter::IsMovingFasterThan_Implementation(float Speed) const
{
	// Horizontal only. Falling speed is already covered by IsAirborne, and counting it here would apply
	// the movement penalty on top of the airborne one for the same jump.
	FVector Velocity = GetVelocity();
	Velocity.Z = 0.f;
	return Velocity.SizeSquared() > FMath::Square(Speed);
}

bool AShooterCharacter::IsAirborne_Implementation() const
{
	// Wall-running counts as airborne here, and should: it is a custom movement mode with no floor, and a
	// player who is holding a wall has no more business landing precise shots than one mid-jump. Sliding
	// stays on the ground, so it keeps the lighter movement penalty instead.
	const UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	return IsValid(MoveComp) && !MoveComp->IsMovingOnGround();
}

bool AShooterCharacter::IsFirstPersonViewer_Implementation() const
{
	// IsPlayerControlled() is the whole point of this override - see the note on the interface declaration.
	// An AI controller is a local controller on the authority, so IsLocallyControlled() alone would claim a
	// bot is looking through its own eyes and hand it the first-person asset path.
	return IsLocallyControlled() && IsPlayerControlled();
}

void AShooterCharacter::AddCameraShake_Implementation(float Amplitude, float Frequency, float Duration,
	TSubclassOf<UCameraShakeBase> ShakeClass)
{
	// The shake is a local cosmetic on the shooter's own view, so it has no business running anywhere else.
	// Player-viewed rather than merely locally controlled: a bot has no view to shake, and letting it run
	// would have it writing FirstPersonCamera's relative rotation for nobody.
	if (!IsFirstPersonViewer_Implementation()) return;

	if (IsValid(ShakeClass))
	{
		if (const APlayerController* PC = Cast<APlayerController>(GetController());
			IsValid(PC) && IsValid(PC->PlayerCameraManager))
		{
			PC->PlayerCameraManager->StartCameraShake(ShakeClass);
		}
	}

	if (Amplitude <= 0.f || Duration <= 0.f || Frequency <= 0.f) return;

	// Amplitude takes the max of the outstanding shake rather than summing. At automatic fire rates a new
	// shot lands long before the previous shake has finished, and summing drives the view into a blur that
	// costs the player target tracking - which is unacceptable in a 1v1 where tracking is the whole game.
	CameraShakeAmplitude = FMath::Max(Amplitude, CameraShakeAmplitude * (CameraShakeTotalDuration > 0.f
		? FMath::Clamp(CameraShakeTimeRemaining / CameraShakeTotalDuration, 0.f, 1.f)
		: 0.f));
	CameraShakeFrequency = Frequency;
	CameraShakeTotalDuration = Duration;
	CameraShakeTimeRemaining = Duration;

	CameraShakePhasePitch = FMath::FRandRange(0.f, 2.f * PI);
	CameraShakePhaseYaw = FMath::FRandRange(0.f, 2.f * PI);
	CameraShakePhaseRoll = FMath::FRandRange(0.f, 2.f * PI);
}

void AShooterCharacter::ClearCameraShake()
{
	CameraShakeAmplitude = 0.f;
	CameraShakeTimeRemaining = 0.f;
	CameraShakeTotalDuration = 0.f;
}

FRotator AShooterCharacter::AdvanceCameraShake(float DeltaTime)
{
	if (CameraShakeTimeRemaining <= 0.f || CameraShakeTotalDuration <= 0.f || CameraShakeAmplitude <= 0.f)
	{
		return FRotator::ZeroRotator;
	}

	CameraShakeTimeRemaining = FMath::Max(0.f, CameraShakeTimeRemaining - DeltaTime);

	// Squared so the shake dies away quickly rather than trailing off linearly, which reads as a rattle
	// that outlasts the shot.
	const float Alpha = CameraShakeTimeRemaining / CameraShakeTotalDuration;
	const float Envelope = Alpha * Alpha;
	const float Elapsed = CameraShakeTotalDuration - CameraShakeTimeRemaining;
	const float Amplitude = CameraShakeAmplitude * Envelope;

	// Deliberately incommensurate multipliers on the three axes. At a shared frequency the axes stay in
	// step and the view traces a clean ellipse, which reads as a wobble rather than as an impact. Roll is
	// trimmed because roll is the axis the eye is least willing to forgive.
	const float Base = 2.f * PI * CameraShakeFrequency * Elapsed;

	return FRotator(
		Amplitude * FMath::Sin(Base + CameraShakePhasePitch),
		Amplitude * FMath::Sin(Base * 0.83f + CameraShakePhaseYaw),
		Amplitude * 0.6f * FMath::Sin(Base * 1.17f + CameraShakePhaseRoll));
}
