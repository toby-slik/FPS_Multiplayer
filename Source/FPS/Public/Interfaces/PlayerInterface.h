// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "PlayerInterface.generated.h"

// This class does not need to be modified.
UINTERFACE()
class UPlayerInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class FPS_API IPlayerInterface
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
	
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	FName GetWeaponAttachPoint(const FGameplayTag& WeaponType) const;
	
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	USkeletalMeshComponent* GetMesh1P() const;
	
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	USkeletalMeshComponent* GetMesh3P() const;
	
	
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void WeaponReplicated();
	
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	AWeapon* GetCurrentWeapon();

	/** True only while the sprint state is active. Sliding, jumping and (later) wall-running are NOT sprinting. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	bool IsSprinting() const;

	/** Ends sprinting only. Must never interrupt a slide, a jump or any other movement state. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void CancelSprint();

	/** True while a slide is in progress. The slide owns the crouched state for its duration. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	bool IsSliding() const;

	/** Ends an in-progress slide early. No effect when not sliding. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void CancelSlide();

	/**
	 * True while the character is attached to and running along a wall.
	 * Read-only on purpose - there is no CancelWallRun(). Firing and aiming are gated on
	 * IsSprinting() alone, so nothing outside the character should be ending a wall run.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	bool IsWallRunning() const;
	
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	int32 GetReserveAmmo() const;
	
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void Notify_CycleWeapon();
	
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void Notify_ReloadWeapon();
	
	/** Applies damage. Returns true only when this damage was the killing blow. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	bool DoDamage(float DamageAmount, AActor* DamageInstigator);

	/**
	 * False from the moment death starts, not when the ragdoll settles. Lets the combat component
	 * skip corpses without reaching into UHealthComponent itself.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	bool IsAlive() const;

	/**
	 * Bones on this target that count as a headshot. The target owns its own anatomy - the weapon only
	 * owns the multiplier - so a future character with a different skeleton needs no combat-code change.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	TArray<FName> GetHeadshotBones() const;

};