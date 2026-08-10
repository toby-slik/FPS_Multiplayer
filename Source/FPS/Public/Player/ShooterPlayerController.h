// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ShooterPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/**
 * 
 */
UCLASS()
class FPS_API AShooterPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	AShooterPlayerController();

	bool bPawnAlive;

	/**
	 * Queues one round's worth of view kick, in degrees. Positive PitchUp raises the view.
	 *
	 * Only ever called on the machine that owns this controller. The kick reaches the authority the same
	 * way ordinary mouse movement does - as part of the replicated control rotation - so the server's
	 * damage trace already fires down the recoiled view with no extra plumbing and nothing to reconcile.
	 */
	void AddViewRecoil(float PitchUp, float Yaw);

	/** Drops all outstanding kick and recovery. Used on death and on anything that reslots the weapon. */
	void ResetViewRecoil();

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	/**
	 * Where bPawnAlive is raised again after a respawn.
	 *
	 * It cannot be done from the pawn's BeginPlay: a respawned pawn is spawned unpossessed and only handed to
	 * the controller afterwards, so GetController() is still null there and the flag stayed false - which left
	 * the new pawn unable to move or even look, because every Input_ handler gates on it. SetPawn is the one
	 * hook that runs on the server at Possess and on the owning client when the pawn replicates in.
	 */
	virtual void SetPawn(APawn* InPawn) override;

	/**
	 * Recoil is injected here, into RotationInput, before Super consumes it - not by writing control
	 * rotation afterwards. That ordering matters: it leaves pitch clamping, ProcessViewRotation and the
	 * pawn's FaceRotation entirely to the engine, so recoil cannot push the view past the camera
	 * manager's limits or desync the mesh yaw from the camera by a frame.
	 */
	virtual void UpdateRotation(float DeltaTime) override;

private:

	/**
	 * Advances the recoil state by DeltaTime and returns the rotation delta to add this frame.
	 * PlayerLookThisFrame is the player's own input for the frame, already accumulated into RotationInput
	 * but not yet applied, and is used to let manual correction retire recovery credit - see the note in
	 * the implementation.
	 */
	FRotator ConsumeViewRecoil(float DeltaTime, const FRotator& PlayerLookThisFrame);

	/** Kick queued by AddViewRecoil but not yet fed onto the view. */
	float RecoilPendingPitch;
	float RecoilPendingYaw;

	/**
	 * How far recoil currently has the view off the player's aim point, in degrees of pitch. This is what
	 * ViewPunchMaxAccumulatedPitch caps, so the ceiling is on the standing offset rather than on lifetime
	 * kick - it falls again as recovery returns pitch or as the player corrects by hand.
	 */
	float RecoilStandingPitch;

	/** The portion of applied kick still eligible to return on its own. */
	float RecoilRecoverablePitch;
	float RecoilRecoverableYaw;

	/** Seconds since the last queued kick, for the recovery delay. */
	float TimeSinceLastViewPunch;

	UPROPERTY(EditAnywhere, Category= "FPS|Input")
	TObjectPtr<UInputMappingContext> ShooterIMC;
	
	UPROPERTY(EditAnywhere, Category= "FPS|Input")
	TObjectPtr<UInputAction> MoveAction;
	
	UPROPERTY(EditAnywhere, Category= "FPS|Input")
	TObjectPtr<UInputAction> LookAction;
	
	UPROPERTY(EditAnywhere, Category= "FPS|Input")
	TObjectPtr<UInputAction> CrouchAction;
	
	UPROPERTY(EditAnywhere, Category= "FPS|Input")
	TObjectPtr<UInputAction> JumpAction;
	
	void Input_Crouch();
	void Input_Jump();
	void Input_Move(const FInputActionValue& InputActionValue);
	void Input_Look(const FInputActionValue& InputActionValue);
};
