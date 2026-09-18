// Copyright Druid Mechanics

#include "Campaign/CampaignWeaponPickup.h"

#include "Combat/CombatComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "Weapon/Weapon.h"

ACampaignWeaponPickup::ACampaignWeaponPickup()
{
	PrimaryActorTick.bCanEverTick = true;

	// Only bCollected crosses the network. What was granted, and to whom, is decided entirely on the
	// authority - a client is told the pickup is gone, not what came out of it.
	bReplicates = true;
	SetReplicateMovement(false);

	// A plain scene root rather than the trigger box, so the box and the display can both sit above the
	// actor's origin. That makes the origin the point that touches the floor, which is what a level designer
	// expects to be dragging around.
	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);

	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->SetupAttachment(SceneRoot);
	Box->SetRelativeLocation(FVector(0.f, 0.f, 100.f));
	Box->SetBoxExtent(FVector(80.f, 80.f, 100.f));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	PedestalMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PedestalMesh"));
	PedestalMesh->SetupAttachment(SceneRoot);
	PedestalMesh->SetRelativeLocation(FVector(0.f, 0.f, 5.f));
	PedestalMesh->SetRelativeScale3D(FVector(0.9f, 0.9f, 0.1f));
	PedestalMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PedestalMesh->bReceivesDecals = false;

	// Blockout art. Swap the mesh and its material on the placed actor once the real pedestal exists.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PedestalMeshAsset(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (PedestalMeshAsset.Succeeded())
	{
		PedestalMesh->SetStaticMesh(PedestalMeshAsset.Object);
	}

	WeaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetupAttachment(SceneRoot);
	WeaponMesh->SetRelativeLocation(FVector(0.f, 0.f, 90.f));
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	WeaponMesh->bReceivesDecals = false;
}

void ACampaignWeaponPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACampaignWeaponPickup, bCollected);
}

void ACampaignWeaponPickup::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ApplyDisplayMesh();
}

void ACampaignWeaponPickup::BeginPlay()
{
	Super::BeginPlay();

	ApplyDisplayMesh();

	if (IsValid(WeaponMesh))
	{
		WeaponMeshRestLocation = WeaponMesh->GetRelativeLocation();
	}

	if (FMath::IsNearlyZero(SpinRate) && FMath::IsNearlyZero(BobHeight))
	{
		SetActorTickEnabled(false);
	}

	if (!IsValid(WeaponClass.Get()))
	{
		UE_LOG(LogTemp, Warning, TEXT("%s has no WeaponClass set; it will grant nothing"), *GetName());
	}

	Box->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::HandleOverlap);
}

void ACampaignWeaponPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bCollected || !IsValid(WeaponMesh)) return;

	if (!FMath::IsNearlyZero(SpinRate))
	{
		// World rotation, not local: the display mesh inherits whatever orientation the weapon's third-person
		// mesh was authored with, so spinning around its own axis would roll some weapons rather than turn them.
		WeaponMesh->AddWorldRotation(FRotator(0.f, SpinRate * DeltaSeconds, 0.f));
	}

	if (BobHeight > 0.f && BobRate > 0.f)
	{
		BobAccumulator += DeltaSeconds * BobRate * 2.f * PI;

		FVector BobbedLocation = WeaponMeshRestLocation;
		BobbedLocation.Z += FMath::Sin(BobAccumulator) * BobHeight;
		WeaponMesh->SetRelativeLocation(BobbedLocation);
	}
}

void ACampaignWeaponPickup::HandleOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// The latch is tested before the authority check so the ordering reads the same on every machine, but it
	// is HasAuthority that does the real work here: overlaps fire locally on clients too, and a client must
	// never be the one that decides a weapon was granted.
	if (bCollected) return;
	if (!HasAuthority()) return;

	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!IsValid(Pawn) || !Pawn->IsPlayerControlled()) return;
	if (!IsValid(WeaponClass.Get())) return;

	UCombatComponent* Combat = UCombatComponent::FindCombatComponent(Pawn);
	if (!IsValid(Combat)) return;

	const EWeaponGrantResult Result = Combat->Auth_GrantWeapon(WeaponClass, bEquipIfUnarmed);

	// A failed grant leaves the pickup standing, so a transient problem does not silently eat the only copy
	// of a level's new weapon.
	if (Result == EWeaponGrantResult::Failed) return;
	if (Result == EWeaponGrantResult::AlreadyOwned && !bCollectIfAlreadyOwned) return;

	Auth_Collect(Pawn);
}

void ACampaignWeaponPickup::Auth_Collect(APawn* Collector)
{
	if (bCollected) return;

	bCollected = true;
	ForceNetUpdate();

	// The authority never receives its own rep notify, so the listen-server host's own copy is hidden here.
	Local_ApplyCollected(Collector);

	if (DestroyDelay > 0.f)
	{
		SetLifeSpan(DestroyDelay);
	}
}

void ACampaignWeaponPickup::OnRep_Collected()
{
	if (!bCollected) return;

	// Null collector: which pawn walked into it is authority-only knowledge and is deliberately not replicated.
	Local_ApplyCollected(nullptr);
}

void ACampaignWeaponPickup::Local_ApplyCollected(APawn* Collector)
{
	SetActorTickEnabled(false);

	if (IsValid(Box))
	{
		Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (IsValid(PedestalMesh))
	{
		PedestalMesh->SetVisibility(false, true);
	}
	if (IsValid(WeaponMesh))
	{
		WeaponMesh->SetVisibility(false, true);
	}

	if (IsValid(PickupSound))
	{
		UGameplayStatics::PlaySoundAtLocation(this, PickupSound, GetActorLocation());
	}

	OnPickedUp(Collector);
}

void ACampaignWeaponPickup::ApplyDisplayMesh()
{
	if (!IsValid(WeaponMesh)) return;

	if (IsValid(DisplayMeshOverride))
	{
		WeaponMesh->SetSkeletalMeshAsset(DisplayMeshOverride);
		return;
	}

	if (!IsValid(WeaponClass.Get())) return;

	// Read off the weapon's own class defaults rather than duplicating the asset reference here. One property
	// - WeaponClass - decides both what the pickup looks like and what it hands over, so the two cannot drift.
	const AWeapon* WeaponCDO = WeaponClass->GetDefaultObject<AWeapon>();
	if (!IsValid(WeaponCDO)) return;

	USkeletalMeshComponent* CDOMesh3P = WeaponCDO->GetMesh3P();
	if (!IsValid(CDOMesh3P)) return;

	WeaponMesh->SetSkeletalMeshAsset(CDOMesh3P->GetSkeletalMeshAsset());
}
