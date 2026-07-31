// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ShooterTypes/ShooterTypes.h"
#include "ShooterMovementComponent.generated.h"

class AShooterCharacter;

/**
 * Saved move for AShooterCharacter.
 *
 * Two jobs:
 *  1. Carry sprint/slide *intent* to the server in the compressed flags, so the server reaches
 *     the same decisions from the same input.
 *  2. Snapshot the predicted movement state so a client can replay unacknowledged moves
 *     correctly after a server correction.
 *
 * Note the timers here are all *remaining* counters advanced by DeltaTime, never world time.
 * World time differs between client and server, so anything derived from it cannot be replayed.
 */
class FPS_API FSavedMove_Shooter : public FSavedMove_Character
{
public:
	typedef FSavedMove_Character Super;

	// --- Intent. Sent to the server via GetCompressedFlags. ---
	uint8 Saved_bWantsToSprint : 1;
	uint8 Saved_bWantsToSlide : 1;

	// --- Predicted state. Local replay only; the server recomputes its own. ---
	uint8 Saved_bSprinting : 1;
	uint8 Saved_bSliding : 1;
	EWallRunSide Saved_WallRunSide;
	FVector Saved_WallRunNormal;
	float Saved_SlideTimeRemaining;
	float Saved_SlideCooldownRemaining;
	float Saved_SlideBufferRemaining;
	float Saved_WallRunTimeRemaining;
	float Saved_WallRunCooldownRemaining;
	float Saved_SameWallCooldownRemaining;
	TWeakObjectPtr<AActor> Saved_WallRunActor;
	TWeakObjectPtr<AActor> Saved_LastWallRunActor;

	virtual void Clear() override;
	virtual uint8 GetCompressedFlags() const override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override;
	virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, class FNetworkPredictionData_Client_Character& ClientData) override;
	virtual void PrepMoveFor(ACharacter* C) override;
};

/** Hands out FSavedMove_Shooter instead of the base saved move. */
class FPS_API FNetworkPredictionData_Client_Shooter : public FNetworkPredictionData_Client_Character
{
public:
	typedef FNetworkPredictionData_Client_Character Super;

	FNetworkPredictionData_Client_Shooter(const UCharacterMovementComponent& ClientMovement);

	virtual FSavedMovePtr AllocateNewMove() override;
};

/**
 * Client-predicted sprint, slide and wall run.
 *
 * All tuning values deliberately stay on AShooterCharacter (this class is a friend of it) so the
 * numbers already dialled in on BP_ShooterCharacter keep working untouched.
 */
UCLASS()
class FPS_API UShooterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

	friend class FSavedMove_Shooter;

public:
	UShooterMovementComponent();

	virtual void BeginPlay() override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;

	virtual float GetMaxSpeed() const override;
	virtual float GetMaxBrakingDeceleration() const override;
	virtual bool CanAttemptJump() const override;
	virtual bool DoJump(bool bReplayingMoves, float DeltaTime) override;
	virtual void PhysCustom(float deltaTime, int32 Iterations) override;
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;

	/** ~State queries. Back IPlayerInterface on AShooterCharacter. */
	bool IsSprinting() const { return bSprinting; }
	bool IsSliding() const { return bSliding; }
	bool IsWallRunning() const;
	EWallRunSide GetWallRunSide() const { return WallRunSide; }
	bool WantsToSprint() const { return bWantsToSprint; }

	/**
	 * ~Input entry points. These only ever set intent - never movement parameters - which is what
	 * lets the server arrive at the same result from the replicated flags.
	 */
	void SetWantsToSprint(bool bNewWantsToSprint);
	void RequestSlide();
	void RequestCancelSlide();

protected:
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;

private:
	AShooterCharacter* GetShooterCharacter() const;

	void AdvanceCooldowns(float DeltaSeconds);
	void UpdateSprintState();
	void UpdateSlideState(float DeltaSeconds);
	void UpdateWallRunState(float DeltaSeconds);

	bool CanStartSlide() const;
	void EnterSlide();
	void ExitSlide();

	bool FindRunnableWall(EWallRunSide Side, FHitResult& OutHit) const;
	bool TryStartWallRun();
	void ClearWallRunState();
	void EndWallRun(EMovementMode NewMode = MOVE_Falling, uint8 NewCustomMode = 0);
	void PhysWallRun(float deltaTime, int32 Iterations);
	bool TryWallJump();

	// --- Intent, replicated as compressed flags ---
	uint8 bWantsToSprint : 1;
	uint8 bWantsToSlide : 1;

	// --- Predicted state ---
	uint8 bSprinting : 1;
	uint8 bSliding : 1;
	EWallRunSide WallRunSide;
	FVector WallRunNormal;
	TWeakObjectPtr<AActor> WallRunActor;
	TWeakObjectPtr<AActor> LastWallRunActor;

	float SlideTimeRemaining;
	float SlideCooldownRemaining;
	float SlideBufferRemaining;
	float WallRunTimeRemaining;
	float WallRunCooldownRemaining;
	float SameWallCooldownRemaining;

	/** Captured once so slide friction can be applied and removed without replicating it. */
	float DefaultGroundFriction;

	mutable TWeakObjectPtr<AShooterCharacter> CachedShooterCharacter;
};
