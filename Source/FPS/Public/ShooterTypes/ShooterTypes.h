#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "ShooterTypes.generated.h"

class UCameraShakeBase;
class USoundBase;

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

	/**
	 * Sustained reticle opening that tracks the weapon's real bullet cone, expressed as a multiple of
	 * ScaleFactor_RoundFired and reaching that multiple at full recoil heat. This is the one reticle term
	 * driven by the *actual* cone rather than by a per-shot cosmetic bloom, so the crosshair cannot
	 * under-report where rounds will go - which matters, because players calibrate their engagement range
	 * against the crosshair.
	 *
	 * Deliberately a multiple of the existing per-shot term rather than an absolute number: that term is
	 * already authored with the correct sign and magnitude for this weapon's reticle material, so scaling it
	 * inherits both. An absolute value here would have to guess the material's sign convention, and guessing
	 * it backwards would produce a crosshair that tightens as the cone opens. Set to 0 to opt out.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float SpreadScaleMultiplier = 2.5f;

	/** How quickly the spread term follows the real cone. Fast on purpose - a laggy crosshair misinforms. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float SpreadInterpSpeed = 18.f;
};

/**
 * Everything about how one weapon kicks. Per weapon on purpose: recoil is the primary cost side of the
 * GDD's loadout trade-offs (larger magazine vs faster reload, better hip-fire vs better ranged accuracy),
 * so it has to be authorable per item rather than being a global rule.
 *
 * The whole struct is driven by one shared scalar - "heat" - which rises with each round fired and decays
 * once firing stops. Spread, view punch and the visible weapon kick all scale off it, so a weapon's
 * sustained-fire behaviour is tuned in one place and the three channels can never disagree about how hot
 * the gun is. Heat is what makes recoil a function of *recency* of fire rather than of a single shot.
 */
USTRUCT(BlueprintType)
struct FRecoilParams
{
	GENERATED_BODY()

	// ---------------------------------------------------------------------------------------------
	// Heat. The shared "how hard has this been fired lately" scalar, normalised 0-1.
	// ---------------------------------------------------------------------------------------------

	/** Heat added per round. 1 / this is roughly how many rounds it takes to reach full heat. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Heat", meta = (ClampMin = "0.0"))
	float HeatPerShot = 0.22f;

	/** Heat shed per second once the delay below has elapsed. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Heat", meta = (ClampMin = "0.0"))
	float HeatDecayPerSecond = 1.1f;

	/**
	 * Grace period after the last round before heat starts falling. This is what stops a slow weapon from
	 * cooling completely between its own shots: it must sit above FireTime for sustained fire to build
	 * heat at all. A lever rifle with a 1.5s cycle and a 0.12s delay is fully cool on every shot by
	 * design - its cost is paid in a single heavy kick, not in a climbing spray.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Heat", meta = (ClampMin = "0.0"))
	float HeatDecayDelay = 0.12f;

	// ---------------------------------------------------------------------------------------------
	// Bullet spread. The only channel that changes where rounds actually go.
	// ---------------------------------------------------------------------------------------------

	/** Cone half-angle in degrees at zero heat. 0 makes the first shot from cold perfectly accurate. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Spread", meta = (ClampMin = "0.0"))
	float SpreadBaseDegrees = 0.25f;

	/** Cone half-angle in degrees at full heat. The difference from base is the sustained-fire penalty. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Spread", meta = (ClampMin = "0.0"))
	float SpreadMaxDegrees = 4.f;

	/** Spread multiplier while aiming down sights. Below 1 makes ADS the accurate stance. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Spread", meta = (ClampMin = "0.0"))
	float AimSpreadMultiplier = 0.35f;

	/** Spread multiplier while moving faster than SpreadMovementSpeedThreshold. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Spread", meta = (ClampMin = "0.0"))
	float MovingSpreadMultiplier = 1.35f;

	/**
	 * Spread multiplier while airborne. Deliberately the harshest of the three: the movement pillar wants
	 * jumping and wall-jumping to be traversal tech, not a way to win a gunfight mid-air.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Spread", meta = (ClampMin = "0.0"))
	float AirborneSpreadMultiplier = 1.8f;

	/** Ground speed above which MovingSpreadMultiplier applies. Sits under WalkSpeed so a walk is penalised. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Spread", meta = (ClampMin = "0.0"))
	float SpreadMovementSpeedThreshold = 250.f;

	// ---------------------------------------------------------------------------------------------
	// View punch. Moves the actual aim point, so this is the channel that costs accuracy.
	// ---------------------------------------------------------------------------------------------

	/** Upward view kick in degrees for a shot fired from cold. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "View Punch")
	float ViewPunchPitchMin = 0.5f;

	/** Upward view kick in degrees at full heat. Above min so a held burst climbs. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "View Punch")
	float ViewPunchPitchMax = 1.05f;

	/**
	 * Horizontal view kick in degrees, randomised within +/- this and scaled by heat. Left cold so the
	 * first round of a burst goes exactly where it is aimed and only sustained fire wanders.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "View Punch", meta = (ClampMin = "0.0"))
	float ViewPunchYawRange = 0.35f;

	/**
	 * Ceiling on how far one burst may push the view up, in degrees. Without this a long auto burst walks
	 * the crosshair into the sky, which reads as a bug rather than as recoil.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "View Punch", meta = (ClampMin = "0.0"))
	float ViewPunchMaxAccumulatedPitch = 7.f;

	/** How quickly the punch is fed onto the view. High = snappy; low = a shove. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "View Punch", meta = (ClampMin = "0.1"))
	float ViewPunchInterpSpeed = 24.f;

	/**
	 * Fraction of the accumulated punch that returns on its own, 0-1. Below 1 leaves the player to correct
	 * the remainder by hand, which is what makes recoil control a skill rather than a formality.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "View Punch", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ViewRecoveryFraction = 0.8f;

	/** Seconds after the last round before recovery begins, so it never fights an in-progress burst. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "View Punch", meta = (ClampMin = "0.0"))
	float ViewRecoveryDelay = 0.07f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "View Punch", meta = (ClampMin = "0.1"))
	float ViewRecoverySpeed = 10.f;

	/** View punch multiplier while aiming down sights. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "View Punch", meta = (ClampMin = "0.0"))
	float AimViewPunchMultiplier = 0.65f;

	// ---------------------------------------------------------------------------------------------
	// Visible kick on the weapon mesh. Purely cosmetic - never touches the aim point or the trace.
	// ---------------------------------------------------------------------------------------------

	/** Centimetres the weapon is driven back toward the camera. The main "this gun has mass" cue. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Weapon Kick")
	float WeaponKickBackward = 3.f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Weapon Kick")
	float WeaponKickUpward = 0.7f;

	/** Degrees the muzzle rises. Reads as far more powerful than translation alone. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Weapon Kick")
	float WeaponKickPitch = 4.f;

	/** Randomised within +/- this, in degrees, so repeat shots never look identical. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Weapon Kick", meta = (ClampMin = "0.0"))
	float WeaponKickYawRange = 1.f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Weapon Kick", meta = (ClampMin = "0.0"))
	float WeaponKickRollRange = 2.f;

	/**
	 * Ceiling on accumulated kick as a multiple of one shot's worth. Sustained fire stacks toward this
	 * instead of restarting from rest each round - which is the concrete failure of driving kick from a
	 * montage, where Montage_Play truncates the previous shot's recovery on every trigger pull.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Weapon Kick", meta = (ClampMin = "1.0"))
	float WeaponKickMaxAccumulated = 2.5f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Weapon Kick", meta = (ClampMin = "0.1"))
	float WeaponKickRecoverySpeed = 12.f;

	/** Weapon kick multiplier while aiming. Trimmed so the sights stay readable, never to zero. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Weapon Kick", meta = (ClampMin = "0.0"))
	float AimWeaponKickMultiplier = 0.55f;

	// ---------------------------------------------------------------------------------------------
	// Screen shake.
	// ---------------------------------------------------------------------------------------------

	/**
	 * Peak procedural shake in degrees. Deliberately small - shake sells impact, but anything large enough
	 * to notice consciously also costs the player the ability to track a target, and this is a 1v1 shooter.
	 * Set to 0 to disable the built-in shake (an authored CameraShakeClass below still plays).
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Camera Shake", meta = (ClampMin = "0.0"))
	float CameraShakeAmplitude = 0.5f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Camera Shake", meta = (ClampMin = "0.0"))
	float CameraShakeFrequency = 24.f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Camera Shake", meta = (ClampMin = "0.0"))
	float CameraShakeDuration = 0.2f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Camera Shake", meta = (ClampMin = "0.0"))
	float AimCameraShakeMultiplier = 0.5f;

	/**
	 * Optional authored shake, played through the camera manager in addition to the procedural one above.
	 * Exists so a designer can curve-author a signature shake per weapon without code, exactly as
	 * HitMarkerMaterial optionally replaces the painted hit marker. Leave unset for procedural only.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Camera Shake")
	TSubclassOf<UCameraShakeBase> CameraShakeClass;
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

	// Hit Marker - audio.
	// All three are optional per-weapon overrides. Left unset (the default) the weapon falls back to the
	// matching HitConfirmSound* on UCombatComponent, which is where the one global confirm tone is assigned.
	// They live here rather than only on the component because a weapon's confirm tone is part of its
	// identity - a suppressed sidearm should be able to click rather than chime - and per the equipment
	// pillar that has to be authorable per weapon rather than branched on weapon type in code.

	/** Confirm tone for an ordinary body hit. Unset = use UCombatComponent::HitConfirmSound. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TObjectPtr<USoundBase> HitMarkerSound;

	/** Unset falls back to the component's headshot sound, then to the plain hit sound above. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TObjectPtr<USoundBase> HitMarkerSound_Headshot;

	/** Unset falls back to the component's lethal sound, then to the plain hit sound above. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TObjectPtr<USoundBase> HitMarkerSound_Lethal;
};
