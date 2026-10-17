// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "GameFramework/Actor.h"
#include "DayNightController.generated.h"

class ADirectionalLight;
class ASkyLight;
class AExponentialHeightFog;
class UMaterialParameterCollection;
class UPostProcessComponent;
class UWorldClockSubsystem;

/**
 * Drives the world's lighting from the clock (feature 6): sun rotation, sun intensity / colour, sky
 * light intensity, fog density / colour and an optional material parameter collection, every tick,
 * on every machine (the clock is already synchronised). Place one in a map and point it at the
 * lights, or leave the references empty and it finds the first of each class at BeginPlay
 * (the subsystem auto-spawns one when a game world has none and the settings allow it).
 *
 * Curves map Daylight (0 night .. 1 noon) to multipliers of the lights' authored values, so a map's
 * own look stays the "noon" reference. Defaults keep a faint moon at night so the world remains
 * navigable until feature 7 adds torches.
 *
 * Auto-exposure would otherwise lift a moonlit scene back to daylight brightness, so the controller
 * carries an unbound post-process component that applies ExposureBiasByDaylight (stops, 0 by day)
 * on top of whatever exposure the map's own volumes settle on. Disable bDriveExposure for maps that
 * manage exposure themselves.
 */
UCLASS(BlueprintType, Blueprintable)
class WORLDCLOCK_API ADayNightController : public AActor
{
	GENERATED_BODY()

public:
	ADayNightController();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** The sun. Found by class when empty. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "DayNight|Targets")
	TObjectPtr<ADirectionalLight> SunLight;

	/** Ambient sky light. Found by class when empty. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "DayNight|Targets")
	TObjectPtr<ASkyLight> SkyLight;

	/** Height fog. Found by class when empty. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "DayNight|Targets")
	TObjectPtr<AExponentialHeightFog> HeightFog;

	/**
	 * The actor carrying the USkyAtmosphereComponent (found when empty). With a planet frame (spherical Phase 4, D12) the
	 * atmosphere is centred on the planet (PlanetCenterAtComponentTransform) with its ground at the sea radius.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "DayNight|Targets")
	TObjectPtr<AActor> SkyAtmosphereActor;

	/** With a planet frame, actors carrying this tag (a Z-up sky sphere) are hidden; so is the height fog. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Planet")
	FName ZUpSkyTag = TEXT("WorldClock.ZUpSky");

	/** Optional: scalar parameters TimeOfDay (hours) and Daylight (0-1) are written here each tick for materials. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Targets")
	TObjectPtr<UMaterialParameterCollection> ParameterCollection;

	/** Find missing targets by class at BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Targets")
	bool bAutoFindTargets = true;

	/** Apply ExposureBiasByDaylight through the unbound post-process component (keeps night dark under auto-exposure). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Exposure")
	bool bDriveExposure = true;

	/** Exposure bias in stops by Daylight: negative at night so the faint moon reads as night, 0 at noon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Exposure")
	FRuntimeFloatCurve ExposureBiasByDaylight;

	/** Unbound, high-priority post-process carrying the exposure bias. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DayNight|Exposure")
	TObjectPtr<UPostProcessComponent> ExposurePostProcess;

	/** Sun intensity multiplier of the authored value by Daylight (0.0083 ~ a faint moon at 6 lux authored). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Curves")
	FRuntimeFloatCurve SunIntensityByDaylight;

	/** Sky light intensity multiplier of the authored value by Daylight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Curves")
	FRuntimeFloatCurve SkyLightIntensityByDaylight;

	/** Fog density multiplier of the authored value by Daylight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Curves")
	FRuntimeFloatCurve FogDensityByDaylight;

	/** Sun colour at night (moonlight) and by day (lerped by Daylight; dusk tints toward SunColorHorizon near the horizon). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Colors")
	FLinearColor SunColorNight = FLinearColor(0.55f, 0.65f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Colors")
	FLinearColor SunColorDay = FLinearColor(1.0f, 0.98f, 0.93f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Colors")
	FLinearColor SunColorHorizon = FLinearColor(1.0f, 0.62f, 0.32f);

	/** Fog colour at night and by day. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Colors")
	FLinearColor FogColorNight = FLinearColor(0.05f, 0.07f, 0.12f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Colors")
	FLinearColor FogColorDay = FLinearColor(0.75f, 0.82f, 0.92f);

	/** Last values applied (for debugging / tests of the mapping). */
	UFUNCTION(BlueprintPure, Category = "DayNight")
	float GetLastDaylight() const { return LastDaylight; }

	/** Pure: the sun colour for a daylight value and sun elevation (horizon tint within +-12 degrees). */
	static FLinearColor SunColorFor(float Daylight, float ElevationDegrees, const FLinearColor& Night, const FLinearColor& Day, const FLinearColor& Horizon);

protected:
	/** The local player's pawn (or camera) location: a planet's light is the sun's elevation there. */
	bool GetViewerLocation(FVector& OutLocation) const;

	/** Once a planet frame is set: centre the atmosphere on the planet, hide the Z-up sky and the height fog. */
	void ApplyPlanetSky(const UWorldClockSubsystem& Clock);
	bool bPlanetSkyApplied = false;

	void FindTargets();
	void CaptureAuthoredValues();
	void Apply(const UWorldClockSubsystem& Clock);

	float AuthoredSunIntensity = 6.0f;
	float AuthoredSkyLightIntensity = 1.0f;
	float AuthoredFogDensity = 0.02f;
	bool bCaptured = false;
	float LastDaylight = -1.0f;
};
