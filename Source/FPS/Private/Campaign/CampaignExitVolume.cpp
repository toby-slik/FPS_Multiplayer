// Copyright Druid Mechanics

#include "Campaign/CampaignExitVolume.h"

#include "Campaign/CampaignGameMode.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

ACampaignExitVolume::ACampaignExitVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	SetRootComponent(Box);
	Box->SetBoxExtent(FVector(200.f, 200.f, 200.f));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
}

void ACampaignExitVolume::BeginPlay()
{
	Super::BeginPlay();

	Box->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::HandleOverlap);
}

void ACampaignExitVolume::HandleOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	const APawn* Pawn = Cast<APawn>(OtherActor);
	if (!IsValid(Pawn) || !Pawn->IsPlayerControlled()) return;

	ACampaignGameMode* GM = Cast<ACampaignGameMode>(UGameplayStatics::GetGameMode(this));
	if (!IsValid(GM)) return;

	if (bRequireArenaCleared && !GM->IsArenaCleared()) return;

	GM->AdvanceToNextLevel();
}
