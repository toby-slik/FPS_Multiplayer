// Copyright Druid Mechanics

#include "Campaign/CampaignTriggerDoor.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Math/UnrealMathUtility.h"

ACampaignTriggerDoor::ACampaignTriggerDoor()
{
	PrimaryActorTick.bCanEverTick = true;

	DoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMesh"));
	SetRootComponent(DoorMesh);
	DoorMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(RootComponent);
	Trigger->SetBoxExtent(FVector(150.f, 150.f, 150.f));
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
}

void ACampaignTriggerDoor::BeginPlay()
{
	Super::BeginPlay();

	ClosedRelativeLocation = DoorMesh->GetRelativeLocation();
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::HandleTriggerOverlap);

	// No work to do until triggered - avoid ticking every trigger door in a level for nothing.
	SetActorTickEnabled(false);
}

void ACampaignTriggerDoor::HandleTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (bTriggered) return;

	const APawn* Pawn = Cast<APawn>(OtherActor);
	if (!IsValid(Pawn) || !Pawn->IsPlayerControlled()) return;

	bTriggered = true;
	ElapsedTime = 0.f;

	if (bDropCollisionOnTriggered && IsValid(DoorMesh))
	{
		DoorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	SetActorTickEnabled(true);
	OnDoorTriggered();
}

void ACampaignTriggerDoor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bTriggered || !IsValid(DoorMesh)) return;

	ElapsedTime += DeltaTime;
	const float Alpha = FMath::Clamp(ElapsedTime / SlideDuration, 0.f, 1.f);
	const float Eased = FMath::InterpEaseInOut(0.f, 1.f, Alpha, 2.f);

	DoorMesh->SetRelativeLocation(ClosedRelativeLocation + FVector(0.f, 0.f, SlideDistance * Eased));

	if (Alpha >= 1.f)
	{
		SetActorTickEnabled(false);
	}
}
