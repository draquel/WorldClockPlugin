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
class UVolumetricCloudComponent;

/**
 * A volumetric cloud layer's altitudes and tracing distances (km), the values of a UVolumetricCloudComponent that
 * ADayNightController::ComputePlanetCloudLayer scales to a planet. The defaults are the engine's (Earth's).
 */
struct FWorldClockCloudLayer
{
	/** Layer bottom altitude over the atmosphere's ground (LayerBottomAltitude) */
	float BottomKm = 5.0f;
	/** Layer thickness (LayerHeight) */
	float HeightKm = 10.0f;
	/** Farthest the layer may be from the camera for tracing to start (TracingStartMaxDistance) */
	float TracingStartMaxDistanceKm = 350.0f;
	/** Tracing starts at least this far from the camera (TracingStartDistanceFromCamera) */
	float TracingStartDistanceFromCameraKm = 0.0f;
	/** Length traced through the layer (TracingMaxDistance) */
	float TracingMaxDistanceKm = 50.0f;
	/** Length traced toward the sun for the clouds' self-shadowing (ShadowTracingDistance) */
	float ShadowTracingDistanceKm = 15.0f;

	/** The layer as a component holds it. */
	static FWorldClockCloudLayer FromComponent(const UVolumetricCloudComponent& Cloud);

	/** Write the layer to a component (its setters, which dirty the render state). */
	void ApplyTo(UVolumetricCloudComponent& Cloud) const;
};

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

	/**
	 * With a planet frame, scale the atmosphere to the planet (spherical Phase 6, WM-BP10): its height, its Rayleigh and Mie
	 * scale heights and its ozone layer are the authored (Earth's) times GetPlanetSkyScale, and its scattering and
	 * absorption divided by it, so the air straight up and over a view distance holds as much as authored and the sky
	 * looks as authored from the ground, while from orbit the atmosphere is a thin shell around the planet (the engine's
	 * 60 km over a 2.5 km planet was a glowing ball 24 times its size). The volumetric cloud layers in the level go with it
	 * (ComputePlanetCloudLayer). Off: the authored atmosphere, only centred.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Planet")
	bool bScalePlanetSky = true;

	/** The scaled atmosphere reaches at least this many times the highest terrain over the sea (its peaks stay in the air). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Planet", meta = (ClampMin = "1.0"))
	float PlanetAtmosphereOverTerrain = 4.0f;

	/**
	 * With a scaled planet sky, the cloud layers' floor is at least this many times the highest terrain over the sea
	 * (visual polish D-VP1, VP1-O1), so a camera on the highest ground is clear under the clouds rather than in their base:
	 * volumetric clouds are traced at quarter resolution and rebuilt from earlier frames, which holds for a layer kilometres
	 * away but smears one that sweeps across the screen just over the camera.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Planet", meta = (ClampMin = "1.0"))
	float PlanetCloudFloorOverTerrain = 1.5f;

	/**
	 * With a planet frame, a planet whose sea radius is under this (km) draws no volumetric clouds (visual polish D-VP1):
	 * its layer, scaled with its sky, is tens of metres thick and over the peaks, where the engine's Earth-sized cloud
	 * material gives nothing visible. 0 = clouds on every planet.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Planet", meta = (ClampMin = "0.0"))
	float PlanetCloudMinRadiusKm = 10.0f;

	/** How many volumetric cloud layers the planet sky hid (PlanetCloudMinRadiusKm), as last applied. */
	UFUNCTION(BlueprintPure, Category = "DayNight|Planet")
	int32 GetHiddenCloudLayers() const { return HiddenCloudLayers; }

	/**
	 * Pure: whether a planet draws volumetric clouds: its sea radius at least MinRadiusKm.
	 * @param SeaRadiusKm The planet's sea radius (km)
	 * @param MinRadiusKm PlanetCloudMinRadiusKm
	 * @return True when the clouds stay
	 */
	static bool ShouldDrawPlanetClouds(float SeaRadiusKm, float MinRadiusKm);

	/**
	 * With a planet frame, the space exposure (WM-BP10): from the top half of the atmosphere up, auto-exposure meters the
	 * brightest part of the view (SpaceExposurePostProcess: the histogram's low and high percent) instead of a frame mostly
	 * black space, so the sunlit planet keeps its daylight exposure from orbit instead of washing out.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Planet")
	bool bPlanetSpaceExposure = true;

	/** The space exposure's metering: the histogram percentiles averaged in space (the brightest part of the view). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Planet", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float SpaceExposureLowPercent = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Planet", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float SpaceExposureHighPercent = 98.0f;

	/** Unbound post-process carrying the space exposure, weighted by GetSpaceExposureWeight. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DayNight|Planet")
	TObjectPtr<UPostProcessComponent> SpaceExposurePostProcess;

	/**
	 * With a planet frame, the sky light follows the camera (visual polish VP1-O5). A real-time capture is taken from the
	 * sky light's own position, and one left at the planet's centre (where a map's sky light usually sits) sees the sky
	 * from inside the planet: no sky light anywhere, every shadow black. Its lower hemisphere is taken from the captured
	 * sky too: "lower" is world -Z, which on a planet can be half the sky, and the atmosphere already draws the planet's
	 * ground under the local horizon. The sky light must be movable. Turned off, the sky light goes back where the map put
	 * it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Planet")
	bool bPlanetSkyLightAtViewer = true;

	/**
	 * Place the sky light for a viewer on a planet (bPlanetSkyLightAtViewer), or put it back
	 * where the map put it when that is off. Called every tick on a planet with the camera's location.
	 * @param Viewer The camera's location, or null when there is no camera
	 */
	void ApplyPlanetSkyLight(const FVector* Viewer);

	/** Whether the sky light is following the viewer (as last applied). */
	UFUNCTION(BlueprintPure, Category = "DayNight|Planet")
	bool IsSkyLightAtViewer() const { return bSkyLightAtViewerApplied; }

	/**
	 * With a planet frame, the sun casts no shadows onto the atmosphere while the scaled air near the ground is more than
	 * PlanetAtmosphereShadowMaxDensity times as dense as the authored (visual polish VP1-O6, the "pixelated clouds"). The
	 * scaled sky (bScalePlanetSky) packs a column of air into a layer GetPlanetSkyScale as thick, so the air within the sun's
	 * shadow cascades, a few hundred metres of it, is 1 / scale times as dense as Earth's (230 times on a 2.5 km planet).
	 * The terrain's and the trees' shadows in it then darken the sky strongly, and the engine draws them through its
	 * low-resolution sky view LUT (oriented with the camera) from a few shadow samples per ray: jagged dark patches that
	 * move and change with the camera. Turned off, the light keeps its own setting.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Planet")
	bool bLimitPlanetAtmosphereShadows = true;

	/** The densest scaled air (times the authored, 1 / GetPlanetSkyScale) the sun still casts shadows onto (bLimitPlanetAtmosphereShadows). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Planet", meta = (ClampMin = "1.0"))
	float PlanetAtmosphereShadowMaxDensity = 2.0f;

	/**
	 * Set the sun's shadows on the atmosphere for a planet (bLimitPlanetAtmosphereShadows):
	 * the light's own setting, off while the scaled air is too dense. Called every tick on a planet once the sky is scaled.
	 */
	void ApplyPlanetAtmosphereShadows();

	/** Whether the sun's shadows on the atmosphere are turned off for the planet's dense air (as last applied). */
	UFUNCTION(BlueprintPure, Category = "DayNight|Planet")
	bool AreAtmosphereShadowsLimited() const { return bAtmosphereShadowsLimited; }

	/**
	 * Pure: whether the sun may cast shadows onto a planet's scaled atmosphere: its air near the ground, 1 / SkyScale times
	 * as dense as the authored, at most MaxDensity times.
	 * @param SkyScale GetPlanetSkyScale (1 = the authored atmosphere)
	 * @param MaxDensity PlanetAtmosphereShadowMaxDensity
	 * @return True when the shadows may stay
	 */
	static bool ShouldShadowPlanetAtmosphere(float SkyScale, float MaxDensity);

	/** The atmosphere's scale on a planet (1 = as authored; set once the planet sky is applied). */
	UFUNCTION(BlueprintPure, Category = "DayNight|Planet")
	float GetPlanetSkyScale() const { return PlanetSkyScale; }

	/** How much of the space exposure applies (0 in the lower atmosphere, 1 in space), as last applied. */
	UFUNCTION(BlueprintPure, Category = "DayNight|Planet")
	float GetSpaceExposureWeight() const { return SpaceExposureWeight; }

	/**
	 * Pure: the atmosphere's scale on a planet: the planet's radius over Earth's (6,360 km, the engine's default ground),
	 * but at least enough for the atmosphere to reach OverTerrain times the highest terrain, at most 1.
	 * @param SeaRadiusKm The sky's ground radius (km)
	 * @param TerrainTopKm The highest terrain over it (km; 0 = unknown)
	 * @param AuthoredHeightKm The authored atmosphere height (km)
	 * @param OverTerrain PlanetAtmosphereOverTerrain
	 * @return Scale in (0, 1]
	 */
	static float ComputePlanetSkyScale(float SeaRadiusKm, float TerrainTopKm, float AuthoredHeightKm, float OverTerrain);

	/**
	 * Pure: a volumetric cloud layer scaled with the atmosphere. Its thickness and its tracing distances (start, from the
	 * camera, max, shadow) are the authored times the sky's scale, so a ray through the layer spends its samples on a
	 * layer the planet's size instead of Earth's (visual polish VP1-O1). Its bottom is the authored bottom times the scale
	 * but at least FloorOverTerrain times the highest terrain (its altitudes are over the same ground as the atmosphere's),
	 * so the layer stays inside the scaled air and clear over every peak; Earth's 5 km floor over a 50 km planet was a halo
	 * of clouds far outside its air (G46).
	 * @param Authored The layer as authored (km)
	 * @param SkyScale GetPlanetSkyScale
	 * @param TerrainTopKm The highest terrain over the sea (km; 0 = unknown)
	 * @param FloorOverTerrain PlanetCloudFloorOverTerrain (at least 1)
	 * @return The scaled layer (km)
	 */
	static FWorldClockCloudLayer ComputePlanetCloudLayer(const FWorldClockCloudLayer& Authored, float SkyScale, float TerrainTopKm, float FloorOverTerrain);

	/**
	 * Pure: the space exposure's weight at an altitude over the sea: 0 up to half the atmosphere's height, 1 from one and a
	 * half of it, smooth between.
	 * @param AltitudeKm Viewer altitude over the sky's ground (km)
	 * @param AtmosphereHeightKm The (scaled) atmosphere height (km)
	 * @return Weight 0..1
	 */
	static float ComputeSpaceExposureWeight(float AltitudeKm, float AtmosphereHeightKm);

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

	/** The local player's camera location (or pawn): where the view, and so the space exposure, is from. */
	bool GetCameraLocation(FVector& OutLocation) const;

	/**
	 * Once a planet frame is set: centre the atmosphere on the planet with its ground at the sea radius, scale it to the
	 * planet (bScalePlanetSky) with its cloud layers, hide the Z-up sky and the height fog, and the clouds on a planet
	 * under PlanetCloudMinRadiusKm.
	 */
	void ApplyPlanetSky(const UWorldClockSubsystem& Clock);
	bool bPlanetSkyApplied = false;

	/** Every tick on a planet: the space exposure's weight from the camera's altitude. */
	void ApplySpaceExposure(const UWorldClockSubsystem& Clock);

	float PlanetSkyScale = 1.0f;
	float PlanetAtmosphereHeightKm = 0.0f;
	int32 HiddenCloudLayers = 0;
	float SpaceExposureWeight = 0.0f;

	/** The sky light following the viewer (ApplyPlanetSkyLight), and what to put back when it stops */
	bool bSkyLightAtViewerApplied = false;
	bool bWarnedSkyLightNotMovable = false;
	FVector AuthoredSkyLightLocation = FVector::ZeroVector;
	bool bAuthoredLowerHemisphereIsBlack = true;

	/** The sun's shadows on the atmosphere (ApplyPlanetAtmosphereShadows): the light's own setting, read the first time */
	bool bAtmosphereShadowsLimited = false;
	bool bAuthoredAtmosphereShadowsCaptured = false;
	bool bAuthoredCastShadowsOnAtmosphere = false;

	void FindTargets();
	void CaptureAuthoredValues();
	void Apply(const UWorldClockSubsystem& Clock);

	float AuthoredSunIntensity = 6.0f;
	float AuthoredSkyLightIntensity = 1.0f;
	float AuthoredFogDensity = 0.02f;
	bool bCaptured = false;
	float LastDaylight = -1.0f;
};
