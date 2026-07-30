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
};
