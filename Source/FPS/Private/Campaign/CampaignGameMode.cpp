// Copyright Druid Mechanics

#include "Campaign/CampaignGameMode.h"

#include "AIController.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "AI/ShooterAIController.h"
#include "Camera/PlayerCameraManager.h"
#include "Campaign/CampaignLevelSet.h"
#include "Campaign/CampaignSaveGame.h"
#include "Character/ShooterCharacter.h"
#include "Combat/CombatComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Health/HealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Weapon/Weapon.h"

ACampaignGameMode::ACampaignGameMode()
{
}

void ACampaignGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	SaveData = UCampaignSaveGame::LoadOrCreate();
	CurrentLevelIndex = ResolveLevelIndex();
}

bool ACampaignGameMode::ShouldOverrideStartingLoadout(const AController* Controller) const
{
	return IsValid(Controller) && Controller->IsA<APlayerController>();
}

void ACampaignGameMode::BeginPlay()
{
	Super::BeginPlay();

	// The pawn and controller classes live on BP_ShooterGameMode, and a C++ subclass cannot inherit
	// Blueprint defaults - so using this class directly as a map's GameMode override silently hands the
	// player an engine ADefaultPawn with no Enhanced Input context, which reads in game as "I cannot move".
	if (!DefaultPawnClass || !DefaultPawnClass->IsChildOf(AShooterCharacter::StaticClass()))
	{
		UE_LOG(LogTemp, Error,
			TEXT("ACampaignGameMode's DefaultPawnClass is %s, which is not a ShooterCharacter. The player "
				 "will spawn unable to move or look. Use a Blueprint subclass of CampaignGameMode with "
				 "DefaultPawnClass, PlayerControllerClass and BotPawnClass set as they are on "
				 "BP_ShooterGameMode, rather than this class directly."),
			*GetNameSafe(DefaultPawnClass));
	}

	if (!IsValid(LevelSet))
	{
		UE_LOG(LogTemp, Error,
			TEXT("ACampaignGameMode has no LevelSet. The level will still play, but the bot keeps its "
				 "Blueprint difficulty and the player keeps the pawn's DefaultWeaponClass."));
	}

	// One tick late on purpose: a pawn placed in the level has not been given its AutoPossessAI controller
	// while the game mode is beginning play, so gathering now would find no enemies and clear the arena
	// before the fight started.
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &ThisClass::GatherPlacedEnemies));

	OnCampaignLevelStarted.Broadcast(CurrentLevelIndex);

	// Starts held at black rather than faded: the previous level's AdvanceToNextLevel already faded to
	// black and held it there through the OpenLevel call, so this is the other half of the same transition,
	// not a fade the player sees from a level that was already visible.
	FadeLocalPlayerCameras(1.f, 0.f, LevelFadeInDuration, false);
}

int32 ACampaignGameMode::ResolveLevelIndex() const
{
	if (LevelIndexOverride >= 0)
	{
		return LevelIndexOverride;
	}

	// The map being played is the truth when it is in the level set: it makes hitting Play in any campaign
	// map do the right thing, which is most of how these levels get iterated on.
	if (IsValid(LevelSet))
	{
		const FString CurrentMap = UGameplayStatics::GetCurrentLevelName(this, true);
		const int32 Found = LevelSet->FindLevelIndexByPackageName(CurrentMap);
		if (Found != INDEX_NONE)
		{
			return Found;
		}
	}

	return IsValid(SaveData) ? SaveData->CurrentLevelIndex : 0;
}

const FCampaignLevel* ACampaignGameMode::GetCurrentLevel() const
{
	return IsValid(LevelSet) && LevelSet->IsValidLevelIndex(CurrentLevelIndex)
		? &LevelSet->Levels[CurrentLevelIndex]
		: nullptr;
}

void ACampaignGameMode::GatherPlacedEnemies()
{
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		APawn* Pawn = *It;

		// Anything with a health component that is not being driven by a human is an arena enemy. Testing
		// for "not a player controller" rather than "is an AI controller" also catches an unpossessed pawn
		// waiting on a controller, which would otherwise be missed and leave the door shut forever.
		if (!IsValid(Pawn) || Pawn->IsPlayerControlled()) continue;
		if (!IsValid(UHealthComponent::FindHealthComponent(Pawn))) continue;

		RegisterEnemy(Pawn);
	}

	if (LivingEnemyCount == 0 && !bArenaCleared)
	{
		// A traversal-only level, or a level whose bot was never placed. Either way there is nothing to
		// fight, so opening the door immediately beats locking the player in.
		UE_LOG(LogTemp, Warning, TEXT("Campaign level %d has no enemies; clearing the arena immediately"), CurrentLevelIndex);
		bArenaCleared = true;
		OnArenaCleared.Broadcast();
	}
}

void ACampaignGameMode::RegisterEnemy(APawn* EnemyPawn)
{
	UHealthComponent* Health = UHealthComponent::FindHealthComponent(EnemyPawn);
	if (!IsValid(Health) || TrackedEnemies.Contains(Health)) return;

	TrackedEnemies.Add(Health);
	++LivingEnemyCount;
	Health->OnDeathStarted.AddDynamic(this, &ThisClass::HandleEnemyDeath);

	ApplyLevelSettingsToEnemy(EnemyPawn);
}

void ACampaignGameMode::ApplyLevelSettingsToEnemy(APawn* EnemyPawn)
{
	const FCampaignLevel* Level = GetCurrentLevel();
	if (!Level || !IsValid(EnemyPawn)) return;

	if (AShooterAIController* AI = Cast<AShooterAIController>(EnemyPawn->GetController()))
	{
		AI->SetSkill(Level->BotSkill);
	}
}

void ACampaignGameMode::HandleEnemyDeath()
{
	if (bArenaCleared) return;

	LivingEnemyCount = FMath::Max(LivingEnemyCount - 1, 0);
	if (LivingEnemyCount > 0) return;

	bArenaCleared = true;
	OnArenaCleared.Broadcast();
}

void ACampaignGameMode::RequestRespawn(ACharacter* Character, AController* Controller)
{
	if (!IsValid(Character)) return;

	if (!IsValid(Controller) || !Controller->IsA<APlayerController>())
	{
		// A campaign bot dies once. Respawning it would re-lock a door the player has already earned.
		Character->Reset();
		Character->Destroy();
		if (IsValid(Controller))
		{
			Controller->Destroy();
		}
		return;
	}

	// The player is not respawned into the arena mid-run: the level restarts, so the fight is fought from
	// the top rather than resumed against a bot that is still half dead.
	Character->Reset();
	Character->Destroy();

	GetWorldTimerManager().SetTimer(RestartTimer, this, &ThisClass::RestartCurrentLevel, FMath::Max(PlayerDeathRestartDelay, 0.01f), false);
}

void ACampaignGameMode::RestartCurrentLevel()
{
	UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this, true)));
}

void ACampaignGameMode::AdvanceToNextLevel()
{
	if (bTransitionStarted) return;
	bTransitionStarted = true;

	// Held at black (bHoldWhenFinished) rather than snapping back once the fade completes, so the screen is
	// still black - not just faded and un-faded - by the time OpenLevel actually swaps the map underneath
	// it. This is the "airlock" half of the transition: ACampaignExitVolume seals the door behind the
	// player at the same moment, so retreat and load hitch are both hidden together.
	FadeLocalPlayerCameras(0.f, 1.f, LevelTransitionDelay, true);

	const int32 NextIndex = CurrentLevelIndex + 1;

	if (IsValid(SaveData))
	{
		SaveData->CurrentLevelIndex = NextIndex;
		SaveData->HighestLevelReached = FMath::Max(SaveData->HighestLevelReached, NextIndex);
		SaveData->LevelsCleared = FMath::Max(SaveData->LevelsCleared, NextIndex);
		SaveData->Save();
	}

	TSoftObjectPtr<UWorld> Destination;
	if (IsValid(LevelSet) && LevelSet->IsValidLevelIndex(NextIndex))
	{
		Destination = LevelSet->Levels[NextIndex].Level;
	}
	else
	{
		Destination = CampaignCompleteLevel;
	}

	if (Destination.IsNull())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Campaign level %d has no destination to advance to (no next level and no CampaignCompleteLevel)"),
			CurrentLevelIndex);
		bTransitionStarted = false;
		return;
	}

	GetWorldTimerManager().SetTimer(TransitionTimer,
		FTimerDelegate::CreateWeakLambda(this, [this, Destination]()
		{
			UGameplayStatics::OpenLevelBySoftObjectPtr(this, Destination);
		}),
		FMath::Max(LevelTransitionDelay, 0.01f), false);
}

void ACampaignGameMode::GetStartingLoadout(const AController* Controller, TArray<TSubclassOf<AWeapon>>& OutWeapons) const
{
	OutWeapons.Reset();

	const FCampaignLevel* Level = GetCurrentLevel();
	if (!Level || !IsValid(LevelSet)) return;

	if (IsValid(Controller) && Controller->IsA<APlayerController>())
	{
		// Up to but not including this level: whatever this level itself unlocks is a pickup placed in the
		// level, not a spawn-in weapon. See the comment on the header declaration.
		LevelSet->BuildPlayerLoadout(CurrentLevelIndex - 1, OutWeapons);
	}
	else
	{
		// Empty stays empty on purpose: an unconfigured bot keeps whatever its own Blueprint carries.
		OutWeapons = Level->BotWeapons;
	}
}

void ACampaignGameMode::FadeLocalPlayerCameras(float FromAlpha, float ToAlpha, float Duration, bool bHoldWhenFinished) const
{
	const UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!IsValid(PC) || !IsValid(PC->PlayerCameraManager)) continue;

		PC->PlayerCameraManager->StartCameraFade(FromAlpha, ToAlpha, FMath::Max(Duration, 0.01f),
			FLinearColor::Black, false, bHoldWhenFinished);
	}
}
