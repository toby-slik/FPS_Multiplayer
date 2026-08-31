// Copyright Druid Mechanics

#include "Campaign/CampaignDoor.h"

#include "Campaign/CampaignGameMode.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"

ACampaignDoor::ACampaignDoor()
{
	PrimaryActorTick.bCanEverTick = false;

	DoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMesh"));
	SetRootComponent(DoorMesh);
	DoorMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

void ACampaignDoor::BeginPlay()
{
	Super::BeginPlay();

	ACampaignGameMode* GM = Cast<ACampaignGameMode>(UGameplayStatics::GetGameMode(this));
	if (!IsValid(GM))
	{
		// Not a campaign level. Leaving the door shut in a map that cannot ever clear it would wall the
		// player in, so it opens rather than trapping them.
		UE_LOG(LogTemp, Warning, TEXT("%s is in a level with no ACampaignGameMode; opening on start"), *GetName());
		OpenDoor();
		return;
	}

	if (GM->IsArenaCleared())
	{
		OpenDoor();
		return;
	}

	GM->OnArenaCleared.AddDynamic(this, &ThisClass::HandleArenaCleared);
}

void ACampaignDoor::HandleArenaCleared()
{
	OpenDoor();
}

void ACampaignDoor::OpenDoor()
{
	if (bOpen) return;
	bOpen = true;

	if (bHideOnOpen && IsValid(DoorMesh))
	{
		DoorMesh->SetVisibility(false, true);
		DoorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	OnDoorOpened();
}
