// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/ShooterReticle.h"

#include "Character/ShooterCharacter.h"
#include "Combat/CombatComponent.h"
#include "Components/Image.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/DrawElements.h"

namespace HitMarkerPaint
{
	/** Below this accumulated intensity the marker is invisible, so we skip the draw entirely. */
	constexpr float VisibilityThreshold = 0.01f;
}

namespace  Ammo
{
	const FName Rounds_Current = FName("Rounds_Current");
	const FName Rounds_Max = FName("Rounds_Max");
}

namespace Reticle
{
	const FName RoundedCornerScale = FName("RoundedCornerScale");
	const FName ShapeCutThickness = FName("ShapeCutThickness");
	const FName Inner_RGBA = FName("Inner_RGBA");
}

namespace HitMarker
{
	const FName Intensity = FName("Intensity");
	const FName LethalBlend = FName("LethalBlend");
	const FName HeadshotBlend = FName("HeadshotBlend");
	const FName Scale = FName("Scale");
}


void UShooterReticle::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	Image_Reticle->SetRenderOpacity(0.f);
	Image_AmmoCounter->SetRenderOpacity(0.f);
	_BaseCornerScaleFactor_RoundFired = 0.0f;
	_BaseShapeCutFactor_RoundFired = 0.f;
	_BaseCornerScaleFactor_Aiming = 0.f;
	_BaseShapeCutFactor_Aiming = 0.f;
	_BaseCornerScaleFactor_Spread = 0.f;
	_BaseCornerScaleFactor_TargetingPlayer = 0.f;
	_HitMarkerIntensity = 0.f;
	_HitMarkerLethal = 0.f;
	_HitMarkerHeadshot = 0.f;
	_bHitMarkerLethalLatched = false;
	_bHitMarkerHeadshotLatched = false;
	bAiming = false;
	bTargetingPlayer = false;

	SetupHitMarker();

	GetOwningPlayer()->OnPossessedPawnChanged.AddDynamic(this, &ThisClass::OnPossesedPawnChanged);
	
	AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(GetOwningPlayer()->GetPawn());
	if (!IsValid(ShooterCharacter)) return;
	
	OnPossesedPawnChanged(nullptr, ShooterCharacter);
	
	if ( ShooterCharacter->HasWeaponFirstReplicated())
	{
		AWeapon* Weapon = IPlayerInterface::Execute_GetCurrentWeapon(ShooterCharacter);
		if (IsValid(Weapon))
		{
			OnReticleChanged(Weapon->GetReticleDynamicMaterialInstance(), Weapon->ReticleParams, false);
			OnAmmoCounterChanged(Weapon->GetAmmoCounterDynamicMaterialInstance(), Weapon->Ammo, Weapon->GetEffectiveMagCapacity());
		}
	}
	else
	{
		ShooterCharacter->OnWeaponFirstReplicated.AddDynamic(this, &ThisClass::OnWeaponFirstReplicated);
	}
	if (ShooterCharacter->HasAuthority())
	{
		AWeapon* Weapon = IPlayerInterface::Execute_GetCurrentWeapon(ShooterCharacter);
		if (!IsValid(Weapon)) return;
		OnReticleChanged(Weapon->GetReticleDynamicMaterialInstance(), Weapon->ReticleParams, false);
		OnAmmoCounterChanged(Weapon->GetAmmoCounterDynamicMaterialInstance(), Weapon->Ammo, Weapon->GetEffectiveMagCapacity());
	}
}

void UShooterReticle::SetupHitMarker()
{
	if (!IsValid(Image_HitMarker)) return;
	if (!IsValid(HitMarkerMaterial))
	{
		// No art assigned yet - keep the image dark rather than showing an unset white brush.
		Image_HitMarker->SetRenderOpacity(0.f);
		return;
	}

	HitMarker_DynMatInst = UMaterialInstanceDynamic::Create(HitMarkerMaterial, this);

	FSlateBrush Brush;
	Brush.SetResourceObject(HitMarker_DynMatInst);
	Image_HitMarker->SetBrush(Brush);

	// The marker is always drawn; visibility is entirely the Intensity param's job. Toggling Slate
	// visibility per shot would rebuild the layout on every round under auto-fire.
	Image_HitMarker->SetRenderOpacity(1.f);
}

const FHitMarkerParams& UShooterReticle::GetHitMarkerParams() const
{
	// Function-local static, so the fallback outlives the reference we hand back. It is only ever reached
	// when there is nothing to read from, and the marker is sitting at zero intensity in that window anyway.
	static const FHitMarkerParams Fallback;

	if (!CurrentCombat.IsValid()) return Fallback;

	const AWeapon* Weapon = CurrentCombat->CurrentWeapon;
	if (!IsValid(Weapon)) return Fallback;

	return Weapon->HitMarkerParams;
}

float UShooterReticle::GetCurrentSpreadAlpha() const
{
	if (!CurrentCombat.IsValid()) return 0.f;

	const AWeapon* Weapon = CurrentCombat->CurrentWeapon;
	if (!IsValid(Weapon)) return 0.f;

	// Read, never advanced. UCombatComponent::TickComponent owns settling the decay; if the widget advanced
	// it too, the cone would cool at twice the authored rate on the client and disagree with the server's.
	return FMath::Clamp(Weapon->GetHeat(), 0.f, 1.f);
}

void UShooterReticle::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	
	_BaseCornerScaleFactor_RoundFired = FMath::FInterpTo(_BaseCornerScaleFactor_RoundFired, 0.f, InDeltaTime, CurrentReticleParams.RoundFiredInterpSpeed);
	_BaseShapeCutFactor_RoundFired = FMath::FInterpTo(_BaseShapeCutFactor_RoundFired, 0.f, InDeltaTime, CurrentReticleParams.RoundFiredInterpSpeed);
	
	_BaseCornerScaleFactor_Aiming = FMath::FInterpTo(_BaseCornerScaleFactor_Aiming, bAiming? CurrentReticleParams.ScaleFactor_Aiming : CurrentReticleParams.ScaleFactor_NotAiming, InDeltaTime, CurrentReticleParams.AimingInterpSpeed);
	_BaseShapeCutFactor_Aiming = FMath::FInterpTo(_BaseShapeCutFactor_Aiming, bAiming ? CurrentReticleParams.ShapeCutFactor_Aiming : CurrentReticleParams.ShapeCutFactor_NotAiming, InDeltaTime, CurrentReticleParams.AimingInterpSpeed);
	
	_BaseCornerScaleFactor_TargetingPlayer = FMath::FInterpTo(_BaseCornerScaleFactor_TargetingPlayer, bTargetingPlayer ? CurrentReticleParams.ScaleFactor_Targeting : CurrentReticleParams.ScaleFactor_NotTargeting, InDeltaTime, CurrentReticleParams.TargetingPlayerInterpSpeed);
	
	// Driven by the weapon's live recoil heat rather than by the fire event, which makes this the one
	// reticle term that reports the *actual* bullet cone. _BaseCornerScaleFactor_RoundFired is a per-shot
	// cosmetic punch that decays on its own timer and can disagree with the real spread; this cannot. A
	// crosshair that under-reports its cone is worse than no feedback at all, because the player calibrates
	// their engagement range against it.
	// Expressed as a multiple of the already-authored per-shot term so it inherits that term's sign - the
	// reticle material's convention for "open" is not knowable here, and getting it backwards would tighten
	// the crosshair as the cone widens.
	const float TargetSpreadScale = CurrentReticleParams.ScaleFactor_RoundFired
		* CurrentReticleParams.SpreadScaleMultiplier
		* GetCurrentSpreadAlpha();
	_BaseCornerScaleFactor_Spread = FMath::FInterpTo(_BaseCornerScaleFactor_Spread, TargetSpreadScale, InDeltaTime, CurrentReticleParams.SpreadInterpSpeed);

	BaseCornerScaleFactor = _BaseCornerScaleFactor_RoundFired + _BaseCornerScaleFactor_Aiming + _BaseCornerScaleFactor_TargetingPlayer + _BaseCornerScaleFactor_Spread;
	BaseShapeCutFactor = _BaseShapeCutFactor_RoundFired + _BaseShapeCutFactor_Aiming;
	
	if (CurrentReticle_DynMatInst.IsValid())
	{
		CurrentReticle_DynMatInst->SetScalarParameterValue(Reticle::RoundedCornerScale, BaseCornerScaleFactor);
		CurrentReticle_DynMatInst->SetScalarParameterValue(Reticle::ShapeCutThickness, BaseShapeCutFactor);
	}

	// Read live off the weapon rather than from a cached copy, so a Blueprint setting HitMarkerParams at
	// runtime takes effect on the next frame instead of waiting for the next OnReticleChanged broadcast.
	const FHitMarkerParams& HitMarkerParams = GetHitMarkerParams();

	// Same accumulate-then-decay shape as _BaseCornerScaleFactor_RoundFired: a fresh hit adds to the
	// intensity instead of restarting an animation, which is what keeps it readable under auto-fire.
	_HitMarkerIntensity = FMath::FInterpTo(_HitMarkerIntensity, 0.f, InDeltaTime, HitMarkerParams.HitMarkerInterpSpeed);

	// Lethal is a LATCHED state, not an independent decay. Decaying it on its own faster speed meant a
	// kill was only red for the first ~0.1s of a marker that stays visible for ~0.9s - a few frames out
	// of the whole life, which reads as "the kill marker is teal". So it ramps in and then holds until
	// the marker itself has faded out. A non-lethal hit can never land during that hold anyway: the
	// target is dead, and Server_FireWeapon's IsAlive gate drops rounds fired into a corpse.
	if (_bHitMarkerLethalLatched && _HitMarkerIntensity <= HitMarkerPaint::VisibilityThreshold)
	{
		_HitMarkerLethal = 0.f;
		_bHitMarkerLethalLatched = false;
	}

	// Headshot is latched on exactly the same terms as lethal, released on exactly the same condition.
	// One consequence worth knowing: because the release waits for the whole marker to fade, a burst whose
	// first round is a headshot and whose rest are body shots reads gold for the whole burst. That is the
	// same trade the lethal latch makes and it is the right one - the alternative flickers gold to teal
	// mid-burst, which is far harder to read than a marker that over-reports the best hit in the burst.
	if (_bHitMarkerHeadshotLatched && _HitMarkerIntensity <= HitMarkerPaint::VisibilityThreshold)
	{
		_HitMarkerHeadshot = 0.f;
		_bHitMarkerHeadshotLatched = false;
	}

	if (IsValid(HitMarker_DynMatInst))
	{
		HitMarker_DynMatInst->SetScalarParameterValue(HitMarker::Intensity, _HitMarkerIntensity);
		HitMarker_DynMatInst->SetScalarParameterValue(HitMarker::LethalBlend, _HitMarkerLethal);
		HitMarker_DynMatInst->SetScalarParameterValue(HitMarker::HeadshotBlend, _HitMarkerHeadshot);
		HitMarker_DynMatInst->SetScalarParameterValue(HitMarker::Scale, 1.f + _HitMarkerIntensity * HitMarkerParams.HitMarkerScaleFactor);
	}
}

bool UShooterReticle::ShouldPaintHitMarker() const
{
	// HitMarker_DynMatInst is only ever created when both the image and the material exist, so its
	// validity is the single authoritative "the material path is live" test.
	return !(IsValid(Image_HitMarker) && IsValid(HitMarker_DynMatInst));
}

int32 UShooterReticle::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Super first so the reticle and ammo counter images draw, then the marker goes on the layer above
	// whatever they ended up on - otherwise the reticle can occlude it.
	const int32 MaxLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	if (!ShouldPaintHitMarker()) return MaxLayer;
	if (_HitMarkerIntensity <= HitMarkerPaint::VisibilityThreshold) return MaxLayer;

	return PaintHitMarker(AllottedGeometry, OutDrawElements, MaxLayer + 1);
}

int32 UShooterReticle::PaintHitMarker(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	// Centre is derived from the geometry rather than hard-coded pixels so the marker stays centred at
	// any resolution, and the local-space units below are pre-DPI, so it scales with the rest of the HUD.
	// FVector2f rather than FVector2D throughout: FSlateDrawElement::MakeLines only stores floats, so the
	// FVector2d overload converts into a second, internal TArray<FVector2f> before handing it to the draw
	// element. Building the float array directly means one allocation per tick mark instead of two, and the
	// one that remains is unavoidable - MakeLines moves it straight into the element's payload.
	const FVector2f LocalSize = FVector2f(AllottedGeometry.GetLocalSize());
	const FVector2f Centre = LocalSize * 0.5f;

	const FHitMarkerParams& HitMarkerParams = GetHitMarkerParams();

	const float Alpha = FMath::Clamp(_HitMarkerIntensity, 0.f, 1.f);
	const float Lethal = FMath::Clamp(_HitMarkerLethal, 0.f, 1.f);
	const float Headshot = FMath::Clamp(_HitMarkerHeadshot, 0.f, 1.f);

	// Both blends are latched for the marker's whole visible life, so a kill or a headshot holds its colour
	// from the marker's first frame to its last and never crossfades back to teal while still on screen.
	//
	// Precedence is lethal > headshot > normal, and it is expressed by ORDER: headshot blends off the base
	// teal, then lethal blends off whatever that produced. Because the lethal lerp is applied last it wins
	// outright at full blend, so a headshot that kills reads red, not gold. That is deliberate - the kill is
	// the more important signal of the two and must never be ambiguous, and a player who just got a
	// headshot kill does not need to be told it was a headshot, they need to be told the target is down.
	FLinearColor Colour = FMath::Lerp(HitMarkerParams.HitMarkerColor, HitMarkerParams.HitMarkerColor_Headshot, Headshot);
	Colour = FMath::Lerp(Colour, HitMarkerParams.HitMarkerColor_Lethal, Lethal);
	Colour.A *= Alpha;

	const float Thickness = HitMarkerParams.HitMarkerTickThickness * FMath::Lerp(1.f, HitMarkerParams.HitMarkerLethalThicknessMultiplier, Lethal);
	const float Length = HitMarkerParams.HitMarkerTickLength * FMath::Lerp(1.f, HitMarkerParams.HitMarkerLethalLengthMultiplier, Lethal);
	const float InnerRadius = HitMarkerParams.HitMarkerGap + HitMarkerParams.HitMarkerKick * Alpha;

	// Diagonals only. The existing reticle uses cardinal-facing shapes, so the marker sits in the gaps
	// between them instead of overlapping them, and the centre stays clear for the crosshair.
	// Pre-normalised, so the four GetSafeNormal calls that used to run every frame are gone. 1/sqrt(2).
	constexpr float Diag = 0.70710678f;
	static const FVector2f Diagonals[4] =
	{
		FVector2f(-Diag, -Diag),
		FVector2f( Diag, -Diag),
		FVector2f(-Diag,  Diag),
		FVector2f( Diag,  Diag)
	};

	const FPaintGeometry PaintGeometry = AllottedGeometry.ToPaintGeometry();

	for (const FVector2f& Direction : Diagonals)
	{
		TArray<FVector2f> Points;
		Points.Reserve(2);
		Points.Add(Centre + Direction * InnerRadius);
		Points.Add(Centre + Direction * (InnerRadius + Length));

		// One MakeLines call per tick - a single call would join all four points into one polyline.
		// NoPixelSnapping keeps the outward kick smooth instead of stepping a pixel at a time.
		// MoveTemp so the array we just built becomes the draw element's storage rather than being copied.
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, PaintGeometry, MoveTemp(Points), ESlateDrawEffect::NoPixelSnapping, Colour, true, Thickness);
	}

	return LayerId;
}

void UShooterReticle::OnPossesedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	UCombatComponent* OldPawnCombat = UCombatComponent::FindCombatComponent(OldPawn);
	if (IsValid(OldPawnCombat))
	{
		OldPawnCombat->OnReticleChanged.RemoveDynamic(this, &ThisClass::OnReticleChanged);
		OldPawnCombat->OnAmmoCounterChanged.RemoveDynamic(this, &ThisClass::OnAmmoCounterChanged);
		OldPawnCombat->OnRoundFired.RemoveDynamic(this, &ThisClass::OnRoundFired);
		OldPawnCombat->OnAimingStatusChanged.RemoveDynamic(this, &ThisClass::OnAimingStatusChanged);
		OldPawnCombat->OnTargetingPlayerStatusChanged.RemoveDynamic(this, &ThisClass::OnTargetingPlayerStatusChanged);
		OldPawnCombat->OnHitConfirmed.RemoveDynamic(this, &ThisClass::OnHitConfirmed);
	}
	UCombatComponent* NewPawnCombat = UCombatComponent::FindCombatComponent(NewPawn);

	// The one and only place the combat component is resolved. Assigned unconditionally so a possession of
	// nullptr (death, before the respawn pawn arrives) clears it and GetHitMarkerParams falls back instead
	// of reading through the dead pawn's component.
	CurrentCombat = NewPawnCombat;

	if (IsValid(NewPawnCombat))
	{
		Image_Reticle->SetRenderOpacity(1.f);
		Image_AmmoCounter->SetRenderOpacity(1.f);
		NewPawnCombat->OnReticleChanged.AddDynamic(this, &ThisClass::OnReticleChanged);
		NewPawnCombat->OnAmmoCounterChanged.AddDynamic(this, &ThisClass::OnAmmoCounterChanged);
		NewPawnCombat->OnRoundFired.AddDynamic(this, &ThisClass::OnRoundFired);
		NewPawnCombat->OnAimingStatusChanged.AddDynamic(this, &ThisClass::OnAimingStatusChanged);
		NewPawnCombat->OnTargetingPlayerStatusChanged.AddDynamic(this, &ThisClass::OnTargetingPlayerStatusChanged);
		NewPawnCombat->OnHitConfirmed.AddDynamic(this, &ThisClass::OnHitConfirmed);

		// The old pawn's marker state must not carry across a respawn - including the latches, which would
		// otherwise leave the fresh pawn's first body shot rendering in the previous life's colour.
		_HitMarkerIntensity = 0.f;
		_HitMarkerLethal = 0.f;
		_HitMarkerHeadshot = 0.f;
		_bHitMarkerLethalLatched = false;
		_bHitMarkerHeadshotLatched = false;
	}
}

void UShooterReticle::OnWeaponFirstReplicated(AWeapon* Weapon)
{
		OnReticleChanged(Weapon->GetReticleDynamicMaterialInstance(), Weapon->ReticleParams, false);
		OnAmmoCounterChanged(Weapon->GetAmmoCounterDynamicMaterialInstance(), Weapon->Ammo, Weapon->GetEffectiveMagCapacity());
}

void UShooterReticle::OnReticleChanged(UMaterialInstanceDynamic* ReticleDynMatInst, const FReticleParams& ReticleParams, bool bCurrentlyTargetingPlayer)
{
	CurrentReticleParams = ReticleParams;
	CurrentReticle_DynMatInst = ReticleDynMatInst;
	
	FSlateBrush Brush;
	Brush.SetResourceObject(ReticleDynMatInst);
	if (IsValid(Image_Reticle))
	{
		Image_Reticle->SetBrush(Brush);
	}
	
	OnTargetingPlayerStatusChanged(bCurrentlyTargetingPlayer);
}

void UShooterReticle::OnAmmoCounterChanged(UMaterialInstanceDynamic* AmmoCounterDynMatInst, int32 RoundsCurrent,
	int32 RoundsMax)
{
	CurrentAmmoCounter_DynMatInst = AmmoCounterDynMatInst;
	CurrentAmmoCounter_DynMatInst->SetScalarParameterValue(Ammo::Rounds_Current, RoundsCurrent);
	CurrentAmmoCounter_DynMatInst->SetScalarParameterValue(Ammo::Rounds_Max, RoundsMax);
	
	FSlateBrush Brush;
	Brush.SetResourceObject(AmmoCounterDynMatInst);
	if (IsValid(Image_AmmoCounter))
	{
		Image_AmmoCounter->SetBrush(Brush);
	}
}

void UShooterReticle::OnRoundFired(int32 RoundsCurrent, int32 RoundsMax, int32 RoundsInReserve)
{
	_BaseCornerScaleFactor_RoundFired += CurrentReticleParams.ScaleFactor_RoundFired;
	_BaseShapeCutFactor_RoundFired += CurrentReticleParams.ShapeCutFactor_RoundFired;
	
	
	if (CurrentAmmoCounter_DynMatInst.IsValid())
	{
		CurrentAmmoCounter_DynMatInst->SetScalarParameterValue(Ammo::Rounds_Current, RoundsCurrent);
		CurrentAmmoCounter_DynMatInst->SetScalarParameterValue(Ammo::Rounds_Max, RoundsMax);
	}
}

void UShooterReticle::OnAimingStatusChanged(bool bIsAiming)
{
	bAiming =bIsAiming;
}

void UShooterReticle::OnHitConfirmed(bool bLethal, bool bHeadshot, float DamageDealt)
{
	const FHitMarkerParams& HitMarkerParams = GetHitMarkerParams();

	_HitMarkerIntensity += HitMarkerParams.HitMarkerIntensity_Hit;

	if (bHeadshot)
	{
		// Snapped, not ramped, for the same reason lethal is - and colour only. A headshot deliberately does
		// NOT get its own intensity, thickness or length: the lethal multipliers already own the "marker
		// changes shape" axis, and giving headshots a second shape change would make the two states compete
		// rather than read as a hierarchy.
		_HitMarkerHeadshot = 1.f;
		_bHitMarkerHeadshotLatched = true;
	}

	if (bLethal)
	{
		_HitMarkerIntensity += HitMarkerParams.HitMarkerIntensity_Lethal;

		// Snapped straight to full rather than crossfaded in - a kill confirmation wants to be
		// unmistakable on the first frame it appears. NativeTick holds it here until the fade completes.
		_HitMarkerLethal = 1.f;
		_bHitMarkerLethalLatched = true;
	}

	// Held auto-fire on a target would otherwise drive the intensity up without bound, so the marker
	// would still be blooming seconds after the last round landed.
	_HitMarkerIntensity = FMath::Min(_HitMarkerIntensity, HitMarkerParams.HitMarkerIntensityMax);
}

void UShooterReticle::OnTargetingPlayerStatusChanged(bool bTargeting)
{
	bTargetingPlayer = bTargeting;
	if (CurrentReticle_DynMatInst.IsValid())
	{
		FLinearColor ReticleColor = bTargetingPlayer ? FLinearColor::Red : FLinearColor::White;
		CurrentReticle_DynMatInst->SetVectorParameterValue(Reticle::Inner_RGBA, ReticleColor);
	}
}
