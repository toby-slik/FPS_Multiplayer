// Copyright Druid Mechanics

#include "UI/MainMenuGameMode.h"

#include "Blueprint/UserWidget.h"
#include "Campaign/CampaignLevelSet.h"
#include "GameFramework/PlayerController.h"
#include "UI/MainMenuWidget.h"

AMainMenuGameMode::AMainMenuGameMode()
{
	// No pawn on the menu. Spawning the shooter character behind the UI would give it a weapon, a health
	// component and a tick, all for a body nobody controls.
	DefaultPawnClass = nullptr;

	MenuWidgetClass = UMainMenuWidget::StaticClass();
	LevelSet = TSoftObjectPtr<UCampaignLevelSet>(FSoftObjectPath(TEXT("/Game/FPS/Campaign/DA_Campaign.DA_Campaign")));
}

void AMainMenuGameMode::BeginPlay()
{
	Super::BeginPlay();

	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!IsValid(PC) || !IsValid(MenuWidgetClass)) return;

	MenuWidget = CreateWidget<UMainMenuWidget>(PC, MenuWidgetClass);
	if (!IsValid(MenuWidget)) return;

	// Synchronous load: this is the front-end, there is nothing else competing for the frame, and an async
	// load would leave the buttons briefly unable to say where they lead.
	if (UCampaignLevelSet* LoadedLevelSet = LevelSet.LoadSynchronous())
	{
		MenuWidget->SetLevelSet(LoadedLevelSet);
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("Main menu could not load its campaign level set at %s. Create the data asset there, or set "
				 "LevelSet on the menu game mode; the campaign buttons will do nothing until then."),
			*LevelSet.ToString());
	}

	MenuWidget->AddToViewport();

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(MenuWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(InputMode);
	PC->SetShowMouseCursor(true);
}
