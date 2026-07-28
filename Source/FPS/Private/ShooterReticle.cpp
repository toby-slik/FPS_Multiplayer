// Fill out your copyright notice in the Description page of Project Settings.


#include "ShooterReticle.h"

void UShooterReticle::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	
	GetOwningPlayer()->OnPossessedPawnChanged.AddDynamic(this, &ThisClass::OnPossesedPawnChanged);
}

void UShooterReticle::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
}

void UShooterReticle::OnPossesedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	// bind to delegates on the combat component
}
