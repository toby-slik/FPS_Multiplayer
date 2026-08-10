// Fill out your copyright notice in the Description page of Project Settings.


#include "ShooterGameModeBase.h"


#include "AIController.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"

void AShooterGameModeBase::RequestRespawn(ACharacter* Character, AController* Controller)
{
	if (!IsValid(Character) || !IsValid(Controller)) return;

	// Weak, because destroying the pawn can take the controller with it: AController::PawnPendingDestroy
	// destroys any controller holding no PlayerState. AShooterAIController opts into one for exactly this
	// reason, but a controller from elsewhere may not, and a raw pointer would go stale silently.
	TWeakObjectPtr<AController> WeakController = Controller;

	Character->Reset();
	Character->Destroy();

	if (!WeakController.IsValid())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("RequestRespawn: %s was destroyed along with its pawn, so it cannot be respawned. "
				 "Controllers that respawn need bWantsPlayerState set."), *GetNameSafe(Controller));
		return;
	}

	TArray<AActor*> PlayerStarts;
	UGameplayStatics::GetAllActorsOfClass(this, APlayerStart::StaticClass(), PlayerStarts);
	if (PlayerStarts.IsEmpty())
	{
		// No PlayerStart in the level at all. RestartPlayer falls back to the spot this controller last used,
		// which still gets the pawn back rather than leaving the player stuck watching a corpse.
		UE_LOG(LogTemp, Warning, TEXT("RequestRespawn: no PlayerStart in the level, falling back to the last start spot"));
		RestartPlayer(WeakController.Get());
		return;
	}

	const int32 Selection = FMath::RandRange(0, PlayerStarts.Num() - 1);

	RestartPlayerAtPlayerStart(WeakController.Get(), PlayerStarts[Selection]);
}

UClass* AShooterGameModeBase::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	if (IsValid(BotPawnClass) && InController && InController->IsA<AAIController>())
	{
		return BotPawnClass;
	}

	return Super::GetDefaultPawnClassForController_Implementation(InController);
}