#pragma once

#include "ShooterTypes.generated.h"

UENUM(BlueprintType)
enum class ETurnInPlace : uint8
{
	Left UMETA(DisplayName = "TurningLeft"),
	Right UMETA(DisplayName = "TurningRight"),
	NotTurning UMETA(DisplayName = "NotTurning")
};

/**
 * Custom movement modes on UShooterMovementComponent, used as MOVE_Custom + CustomMovementMode.
 * Slide is deliberately NOT here - it stays in MOVE_Walking so it keeps the engine's floor,
 * ramp and step handling, which is what preserves its current feel exactly.
 */
UENUM(BlueprintType)
enum class EShooterCustomMovementMode : uint8
{
	WallRun UMETA(DisplayName = "WallRun")
};

/** Which side of the character the wall being run on sits. Drives anim selection and camera roll. */
UENUM(BlueprintType)
enum class EWallRunSide : uint8
{
	None UMETA(DisplayName = "NotWallRunning"),
	Left UMETA(DisplayName = "WallOnLeft"),
	Right UMETA(DisplayName = "WallOnRight")
};

USTRUCT(BlueprintType)
struct FReticleParams
{
	GENERATED_BODY()
	
	// Shape Cut Factor
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float ShapeCutFactor_RoundFired = 0.f;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float ShapeCutFactor_Aiming = 0.f;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float ShapeCutFactor_NotAiming = 0.f;
	
	// Scale Factor
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float ScaleFactor_RoundFired = 0.f;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float ScaleFactor_Aiming = 0.f;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float ScaleFactor_NotAiming = 0.f;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float ScaleFactor_Targeting = 0.f;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float ScaleFactor_NotTargeting = 0.f;
	
	// Interp Speeds
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float RoundFiredInterpSpeed = 20.f;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float AimingInterpSpeed = 15.f;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float TargetingPlayerInterpSpeed = 10.f;
};

/**
 * Hit marker look and feel. Split out of FReticleParams rather than living in it because Category on a
 * UPROPERTY inside a USTRUCT is ignored - the Details panel always nests struct members under the
 * containing property's category - so its own struct is the only way these get their own FPS|HitMarker
 * heading. The split is also the honest one: reticle shape and hit feedback are separate concerns.
 */
USTRUCT(BlueprintType)
struct FHitMarkerParams
{
	GENERATED_BODY()

	/** Added to the marker's intensity on every server-confirmed hit. Accumulates under auto-fire, so a
	 *  burst reads brighter than a single round rather than restarting from zero each time. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float HitMarkerIntensity_Hit = 1.f;

	/** Added on a confirmed kill, on top of HitMarkerIntensity_Hit. Kills should punch harder. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float HitMarkerIntensity_Lethal = 1.5f;

	/** How quickly the marker's intensity decays back to 0. Lower = the marker lingers longer. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float HitMarkerInterpSpeed = 6.f;

	/** Upper bound on accumulated intensity, so held auto-fire can't drive the marker arbitrarily bright. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float HitMarkerIntensityMax = 2.f;

	/** Extra scale the marker is pushed out to at full intensity, snapping back in as it decays. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float HitMarkerScaleFactor = 0.25f;

	// Hit Marker - painted fallback geometry.
	// These only drive the C++/Slate-painted marker UShooterReticle falls back to when no
	// HitMarkerMaterial is assigned. Units are widget-local, so they are resolution and DPI
	// independent; the numbers below are tuned to read at a 1080p, DPI scale 1 viewport.

	/** Length of each diagonal tick. Raise for a longer, more aggressive marker. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float HitMarkerTickLength = 7.f;

	/** Line width of each tick. Raise for a heavier, blunter marker; 1.0 reads as a hairline. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float HitMarkerTickThickness = 2.f;

	/** Distance from screen centre to the inner end of each tick. This is the hole the crosshair reads through. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float HitMarkerGap = 6.f;

	/** Extra gap added at full intensity and settling back to HitMarkerGap as the marker decays - the outward pop. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float HitMarkerKick = 4.f;

	/** Tick thickness is multiplied by this at full lethal blend. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float HitMarkerLethalThicknessMultiplier = 1.6f;

	/** Tick length is multiplied by this at full lethal blend. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float HitMarkerLethalLengthMultiplier = 1.35f;

	/** Colour of a normal confirmed hit. Water teal, per the HUD palette. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FLinearColor HitMarkerColor = FLinearColor(0.25f, 0.85f, 0.85f, 1.f);

	/**
	 * Colour a headshot blends toward. Unlike the lethal red this needs no exception - it is the sunlit
	 * gold already in the world palette, so it reads as part of the same visual system as the rest of the
	 * HUD. Encodes to roughly #FFDD61.
	 *
	 * The two numbers that matter are chosen against the other two marker states rather than in isolation:
	 * blue is held right down at 0.12 so the hue cannot drift toward the teal non-lethal marker as the
	 * marker desaturates against the background mid-fade - a gold with a blue lift is exactly what reads
	 * as "dirty teal" at low opacity. Green sits at 0.72 rather than at full, because a full-green gold
	 * goes chartreuse, and chartreuse is the one warm hue that sits close to teal's green-cyan
	 * neighbourhood. Red stays at 1.0 so apparent brightness survives the alpha fade. The result lands at
	 * hue ~48 degrees, clear of both the teal (~180) and the lethal scarlet (~358).
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FLinearColor HitMarkerColor_Headshot = FLinearColor(1.f, 0.72f, 0.12f, 1.f);

	/**
	 * Colour a kill blends toward. A deliberate, approved exception to the world-palette HUD rule - a kill
	 * is the one moment that should break out of the teal/gold system so it can never be misread as an
	 * ordinary hit. Not pure (1,0,0): the small green and blue lift puts it at roughly #FF393F once
	 * encoded, a hot scarlet that keeps apparent brightness against warm travertine and stays clearly
	 * separated in hue from the teal non-lethal marker, where flat pure red goes muddy.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FLinearColor HitMarkerColor_Lethal = FLinearColor(1.f, 0.04f, 0.05f, 1.f);
};
