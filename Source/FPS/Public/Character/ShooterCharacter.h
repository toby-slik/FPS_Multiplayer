// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Interfaces/PlayerInterface.h"
#include "ShooterTypes/ShooterTypes.h"
#include "ShooterCharacter.generated.h"

class UCombatComponent;
class UCameraComponent;
class USpringArmComponent;
class UInputAction;
class AWeapon;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWeaponFirstReplicated, AWeapon*, Weapon);

UCLASS()
class FPS_API AShooterCharacter : public ACharacter, public IPlayerInterface
{
	GENERATED_BODY()

public:
	AShooterCharacter();
	
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;

	/** PlayerInterface */
	virtual FName GetWeaponAttachPoint_Implementation(const FGameplayTag& WeaponType) const override;
	virtual USkeletalMeshComponent* GetMesh1P_Implementation() const override;
	virtual USkeletalMeshComponent* GetMesh3P_Implementation() const override;
	virtual void WeaponReplicated_Implementation() override;
	virtual AWeapon* GetCurrentWeapon_Implementation() override;
	virtual bool IsSprinting_Implementation() const override;
	virtual void CancelSprint_Implementation() override;
	virtual bool IsSliding_Implementation() const override;
	virtual void CancelSlide_Implementation() override;
	virtual int32 GetReserveAmmo_Implementation() const override;
	/** ~PlayerInterface */
	
	virtual void BeginPlay() override;
	virtual void BeginDestroy() override;
	
	UFUNCTION(BlueprintCallable)
	FRotator GetFixedRotation() const;
	
	UPROPERTY(BlueprintReadOnly, Category = "FPS|FABRIK")
	FTransform FABRIK_SocketTransform;
	
	UFUNCTION(BlueprintCallable)
	bool HasCurrentWeapon() const;
	
	UPROPERTY(BlueprintAssignable)
	FWeaponFirstReplicated OnWeaponFirstReplicated;
	
	bool HasWeaponFirstReplicated() const { return bWeaponFirstReplicated; }
protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FPS|Combat")
	TObjectPtr<UCombatComponent> Combat;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FPS|Camera")
	TObjectPtr<UCameraComponent> FirstPersonCamera;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Aiming")
	float DefaultFOV;
	
	UFUNCTION(BlueprintImplementableEvent)
	void OnAim(bool bIsAiming);
	
	UPROPERTY(BlueprintReadOnly, Category = "FPS|TurnInPlace")
	float AO_Yaw;
	
	UPROPERTY(BlueprintReadOnly, Category = "FPS|Strafing")
	float MovementOffsetYaw;
	
	UPROPERTY(BlueprintReadOnly, Category = "FPS|TurnInPlace")
	ETurnInPlace TurningStatus;

	/** Drives the sprint state in ABP_FirstPerson / ABP_ThirdPerson. */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "FPS|Movement")
	bool bSprinting;

	/** Drives the slide state in ABP_FirstPerson / ABP_ThirdPerson. Independent of bSprinting on purpose. */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "FPS|Movement")
	bool bSliding;

	// --- Sprint tuning ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float WalkSpeed;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float SprintSpeed;

	/** Sprint auto-cancels when the player has no movement input and has slowed below this. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float SprintStopSpeed;

	// --- Slide tuning ---

	/** Minimum ground speed required to enter a slide. Stops slide-on-the-spot the instant sprint is toggled. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float SlideMinStartSpeed;

	/** Speed the slide launches at. Should sit above SprintSpeed to feel like a burst. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float SlideLaunchSpeed;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float SlideDuration;

	/** Slide ends early once it decays below this speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float SlideEndSpeed;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float SlideGroundFriction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float SlideBrakingDeceleration;

	/** Minimum time between slides. Second half of the anti-spam guard. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Movement")
	float SlideCooldown;

private:

	void Input_CycleWeapon();
	void Input_ReloadWeapon();
	void Input_FireWeapon_Pressed();
	void Input_FireWeapon_Released();
	void Input_Aim_Pressed();
	void Input_Aim_Released();
	void Input_Sprint_Toggle();
	void Input_Slide_Pressed();

	void Local_SetSprinting(bool bNewSprinting);

	UFUNCTION(Server, Reliable)
	void Server_SetSprinting(bool bNewSprinting);

	void Local_StartSlide();

	UFUNCTION(Server, Reliable)
	void Server_StartSlide();

	UFUNCTION(Server, Reliable)
	void Server_StopSlide();

	void StopSlide();
	bool CanStartSlide() const;
	void UpdateMovementState();

	FTimerHandle SlideTimer;
	float LastSlideEndTime;
	float CachedGroundFriction;
	float CachedBrakingDeceleration;
	float CachedMaxWalkSpeedCrouched;

	void CalculateFABRIKSocketTransform();
	void CalculateTurnInPlaceParameters(float DeltaTime);
	void TurnInPlace(float DeltaTime);
	
	bool bWeaponFirstReplicated;
	FRotator StartingAimRotation;
	float InterpAO_Yaw;
	
	// 1st person view (arms)
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkeletalMeshComponent> Mesh1P;
	
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USpringArmComponent> SpringArm;
	
	UPROPERTY(EditAnywhere, Category = "FPS|Input")
	TObjectPtr<UInputAction> CycleWeaponAction;
	
	UPROPERTY(EditAnywhere, Category = "FPS|Input")
	TObjectPtr<UInputAction> FireWeaponAction;
	
	UPROPERTY(EditAnywhere, Category = "FPS|Input")
	TObjectPtr<UInputAction> ReloadWeaponAction;
	
	UPROPERTY(EditAnywhere, Category = "FPS|Input")
	TObjectPtr<UInputAction> AimWeaponAction;

	UPROPERTY(EditAnywhere, Category = "FPS|Input")
	TObjectPtr<UInputAction> SprintAction;

	UPROPERTY(EditAnywhere, Category = "FPS|Input")
	TObjectPtr<UInputAction> SlideAction;
};
