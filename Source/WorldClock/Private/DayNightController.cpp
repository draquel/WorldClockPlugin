// Copyright Daniel Raquel. All Rights Reserved.

#include "DayNightController.h"
#include "WorldClock.h"
#include "WorldClockMath.h"
#include "WorldClockSettings.h"
#include "WorldClockSubsystem.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarWorldClockPlanetSkyLightAtViewer(
	TEXT("wc.Planet.SkyLightAtViewer"),
	1,
	TEXT("On a planet, the sky light's real-time capture follows the camera (visual polish VP1-O5,\n")
	TEXT("ADayNightController::bPlanetSkyLightAtViewer):\n")
	TEXT("  1 = at the camera, its lower hemisphere from the captured sky (default)\n")
	TEXT("  0 = where the map put it, as before (at the planet's centre it captures from inside the planet: no sky light)"),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarWorldClockPlanetAtmosphereShadows(
	TEXT("wc.Planet.AtmosphereShadows"),
	1,
	TEXT("On a planet, the sun's shadows on the atmosphere (visual polish VP1-O6, ADayNightController::bLimitPlanetAtmosphereShadows):\n")
	TEXT("  1 = off while the scaled air is denser than PlanetAtmosphereShadowMaxDensity times the authored (default; on a small\n")
	TEXT("      planet the shadows show as jagged dark patches in the sky that move with the camera)\n")
	TEXT("  0 = the light's own setting, as before"),
	ECVF_Default);

namespace
{
	void SetCurve(FRuntimeFloatCurve& Curve, std::initializer_list<TPair<float, float>> Keys)
	{
		FRichCurve* Rich = Curve.GetRichCurve();
		Rich->Reset();
		for (const TPair<float, float>& Key : Keys)
		{
			Rich->AddKey(Key.Key, Key.Value);
		}
		Rich->AutoSetTangents();
	}
}

ADayNightController::ADayNightController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bReplicates = false; // every machine drives its own lights from the synchronised clock
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// Night keeps a faint moon (about 1/120 of the authored sun), the sky light a little ambient.
	SetCurve(SunIntensityByDaylight, { {0.0f, 0.0083f}, {0.15f, 0.05f}, {0.5f, 0.55f}, {1.0f, 1.0f} });
	SetCurve(SkyLightIntensityByDaylight, { {0.0f, 0.12f}, {0.3f, 0.45f}, {1.0f, 1.0f} });
	SetCurve(FogDensityByDaylight, { {0.0f, 1.6f}, {0.5f, 1.2f}, {1.0f, 1.0f} });
	// About 3.5 stops under the daytime exposure at night; twilight sits between.
	SetCurve(ExposureBiasByDaylight, { {0.0f, -3.5f}, {0.25f, -1.5f}, {1.0f, 0.0f} });

	ExposurePostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("ExposurePostProcess"));
	ExposurePostProcess->SetupAttachment(Root);
	ExposurePostProcess->bUnbound = true;
	ExposurePostProcess->Priority = 100.0f; // after the map's own volumes, so the bias stacks on their result
	ExposurePostProcess->BlendWeight = 1.0f;
	ExposurePostProcess->Settings.bOverride_AutoExposureBias = true;
	ExposurePostProcess->Settings.AutoExposureBias = 0.0f;

	// Planets (spherical Phase 6, WM-BP10): weighted in by altitude, the histogram meters the brightest part of the view.
	SpaceExposurePostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("SpaceExposurePostProcess"));
	SpaceExposurePostProcess->SetupAttachment(Root);
	SpaceExposurePostProcess->bUnbound = true;
	SpaceExposurePostProcess->Priority = 101.0f;
	SpaceExposurePostProcess->BlendWeight = 0.0f;
	SpaceExposurePostProcess->bEnabled = false;
}

void ADayNightController::BeginPlay()
{
	Super::BeginPlay();
	if (bAutoFindTargets)
	{
		FindTargets();
	}
	CaptureAuthoredValues();
	UE_LOG(LogWorldClock, Log, TEXT("DayNightController: sun=%s sky=%s fog=%s mpc=%s"), *GetNameSafe(SunLight), *GetNameSafe(SkyLight),
		*GetNameSafe(HeightFog), *GetNameSafe(ParameterCollection));
}

void ADayNightController::FindTargets()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (!SunLight)
	{
		for (TActorIterator<ADirectionalLight> It(World); It; ++It) { SunLight = *It; break; }
	}
	if (!SkyLight)
	{
		for (TActorIterator<ASkyLight> It(World); It; ++It) { SkyLight = *It; break; }
	}
	if (!HeightFog)
	{
		for (TActorIterator<AExponentialHeightFog> It(World); It; ++It) { HeightFog = *It; break; }
	}
	if (!SkyAtmosphereActor)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->FindComponentByClass<USkyAtmosphereComponent>())
			{
				SkyAtmosphereActor = *It;
				break;
			}
		}
	}
}

bool ADayNightController::GetCameraLocation(FVector& OutLocation) const
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (PC && PC->PlayerCameraManager)
	{
		OutLocation = PC->PlayerCameraManager->GetCameraLocation();
		return true;
	}
	return GetViewerLocation(OutLocation);
}

float ADayNightController::ComputePlanetSkyScale(float SeaRadiusKm, float TerrainTopKm, float AuthoredHeightKm, float OverTerrain)
{
	constexpr float EarthRadiusKm = 6360.0f;
	const float ByRadius = SeaRadiusKm / EarthRadiusKm;
	const float ByTerrain = AuthoredHeightKm > 0.0f ? FMath::Max(OverTerrain, 1.0f) * FMath::Max(TerrainTopKm, 0.0f) / AuthoredHeightKm : 0.0f;
	return FMath::Clamp(FMath::Max(ByRadius, ByTerrain), 1.0e-4f, 1.0f);
}

void ADayNightController::ComputePlanetCloudLayer(float AuthoredBottomKm, float AuthoredHeightKm, float SkyScale, float TerrainTopKm, float& OutBottomKm, float& OutHeightKm)
{
	const float K = FMath::Clamp(SkyScale, 1.0e-4f, 1.0f);
	OutBottomKm = FMath::Max(FMath::Max(AuthoredBottomKm, 0.0f) * K, FMath::Max(TerrainTopKm, 0.0f));
	OutHeightKm = FMath::Max(AuthoredHeightKm, 0.0f) * K;
}

bool ADayNightController::ShouldShadowPlanetAtmosphere(float SkyScale, float MaxDensity)
{
	// The scattering is divided by the scale, so the air near the ground is 1 / scale times as dense as the authored.
	return SkyScale * FMath::Max(MaxDensity, 1.0f) >= 1.0f;
}

float ADayNightController::ComputeSpaceExposureWeight(float AltitudeKm, float AtmosphereHeightKm)
{
	if (AtmosphereHeightKm <= 0.0f)
	{
		return 0.0f;
	}
	return FMath::SmoothStep(0.5f * AtmosphereHeightKm, 1.5f * AtmosphereHeightKm, AltitudeKm);
}

bool ADayNightController::GetViewerLocation(FVector& OutLocation) const
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return false;
	}
	if (const APawn* Pawn = PC->GetPawn())
	{
		OutLocation = Pawn->GetActorLocation();
		return true;
	}
	if (PC->PlayerCameraManager)
	{
		OutLocation = PC->PlayerCameraManager->GetCameraLocation();
		return true;
	}
	return false;
}

void ADayNightController::ApplyPlanetSky(const UWorldClockSubsystem& Clock)
{
	if (bPlanetSkyApplied)
	{
		return;
	}
	bPlanetSkyApplied = true;
	USkyAtmosphereComponent* Sky = SkyAtmosphereActor ? SkyAtmosphereActor->FindComponentByClass<USkyAtmosphereComponent>() : nullptr;
	PlanetSkyScale = 1.0f;
	PlanetAtmosphereHeightKm = 0.0f;
	if (Sky)
	{
		// The engine's sky is already a sphere around a planet: tell it where the planet is (BottomRadius in km).
		Sky->SetMobility(EComponentMobility::Movable);
		Sky->TransformMode = ESkyAtmosphereTransformMode::PlanetCenterAtComponentTransform;
		Sky->SetWorldLocation(Clock.GetPlanetCenter());
		const float SeaRadiusKm = FMath::Max(Clock.GetPlanetSeaRadius() / 100000.0f, 0.1f);
		Sky->SetBottomRadius(SeaRadiusKm);
		// Scaled to the planet (spherical Phase 6, WM-BP10): the heights by K, the coefficients by 1 / K, so a column of air
		// (and the aerial perspective over a distance, its distance scaled by K) holds what the authored one holds.
		if (bScalePlanetSky)
		{
			const float K = ComputePlanetSkyScale(SeaRadiusKm, Clock.GetPlanetTerrainTop() / 100000.0f, Sky->AtmosphereHeight, PlanetAtmosphereOverTerrain);
			PlanetSkyScale = K;
			Sky->SetAtmosphereHeight(Sky->AtmosphereHeight * K);
			Sky->SetRayleighExponentialDistribution(Sky->RayleighExponentialDistribution * K);
			Sky->SetMieExponentialDistribution(Sky->MieExponentialDistribution * K);
			Sky->SetRayleighScatteringScale(Sky->RayleighScatteringScale / K);
			Sky->SetMieScatteringScale(Sky->MieScatteringScale / K);
			Sky->SetMieAbsorptionScale(Sky->MieAbsorptionScale / K);
			Sky->SetOtherAbsorptionScale(Sky->OtherAbsorptionScale / K);
			Sky->OtherTentDistribution.TipAltitude *= K;
			Sky->OtherTentDistribution.Width *= K;
			Sky->SetAerialPespectiveViewDistanceScale(Sky->AerialPespectiveViewDistanceScale * K);
		}
		PlanetAtmosphereHeightKm = Sky->AtmosphereHeight;
		Sky->MarkRenderStateDirty();
	}
	int32 Hidden = 0;
	int32 Clouds = 0;
	float CloudBottomKm = 0.0f, CloudHeightKm = 0.0f;
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (!ZUpSkyTag.IsNone() && It->ActorHasTag(ZUpSkyTag))
			{
				It->SetActorHiddenInGame(true);
				++Hidden;
			}
			// The cloud layers' altitudes are over the atmosphere's ground: scaled with it (WM-BP10).
			UVolumetricCloudComponent* Cloud = (Sky && bScalePlanetSky) ? It->FindComponentByClass<UVolumetricCloudComponent>() : nullptr;
			if (Cloud)
			{
				ComputePlanetCloudLayer(Cloud->LayerBottomAltitude, Cloud->LayerHeight, PlanetSkyScale, Clock.GetPlanetTerrainTop() / 100000.0f, CloudBottomKm, CloudHeightKm);
				Cloud->SetLayerBottomAltitude(CloudBottomKm);
				Cloud->SetLayerHeight(CloudHeightKm);
				++Clouds;
			}
		}
	}
	if (HeightFog)
	{
		HeightFog->SetActorHiddenInGame(true);
		if (UExponentialHeightFogComponent* Fog = HeightFog->GetComponent())
		{
			Fog->SetVisibility(false);
		}
	}
	UE_LOG(LogWorldClock, Log, TEXT("DayNightController: planet sky: atmosphere %s at %s (ground %.3f km, %.3f km high, scale %.4f of the authored), %d cloud layer(s) scaled (%.3f to %.3f km), %d Z-up sky actor(s) hidden, height fog %s."),
		*GetNameSafe(SkyAtmosphereActor), *Clock.GetPlanetCenter().ToCompactString(), Clock.GetPlanetSeaRadius() / 100000.0f,
		PlanetAtmosphereHeightKm, PlanetSkyScale, Clouds, CloudBottomKm, CloudBottomKm + CloudHeightKm, Hidden, HeightFog ? TEXT("hidden") : TEXT("none"));
}

void ADayNightController::ApplySpaceExposure(const UWorldClockSubsystem& Clock)
{
	if (!SpaceExposurePostProcess)
	{
		return;
	}
	FVector Camera;
	const bool bActive = bPlanetSpaceExposure && PlanetAtmosphereHeightKm > 0.0f && GetCameraLocation(Camera);
	SpaceExposureWeight = bActive
		? ComputeSpaceExposureWeight(static_cast<float>((FVector::Dist(Camera, Clock.GetPlanetCenter()) - Clock.GetPlanetSeaRadius()) / 100000.0), PlanetAtmosphereHeightKm)
		: 0.0f;
	SpaceExposurePostProcess->bEnabled = SpaceExposureWeight > 0.0f;
	SpaceExposurePostProcess->BlendWeight = SpaceExposureWeight;
	SpaceExposurePostProcess->Settings.bOverride_AutoExposureLowPercent = true;
	SpaceExposurePostProcess->Settings.AutoExposureLowPercent = SpaceExposureLowPercent;
	SpaceExposurePostProcess->Settings.bOverride_AutoExposureHighPercent = true;
	SpaceExposurePostProcess->Settings.AutoExposureHighPercent = FMath::Max(SpaceExposureHighPercent, SpaceExposureLowPercent);
}

void ADayNightController::ApplyPlanetSkyLight(const FVector* Viewer)
{
	USkyLightComponent* Comp = SkyLight ? SkyLight->GetLightComponent() : nullptr;
	if (!Comp)
	{
		return;
	}
	const bool bEnabled = bPlanetSkyLightAtViewer && CVarWorldClockPlanetSkyLightAtViewer.GetValueOnGameThread() != 0;
	const bool bMovable = Comp->Mobility == EComponentMobility::Movable;
	UE_CLOG(bEnabled && !bMovable && !bWarnedSkyLightNotMovable, LogWorldClock, Warning,
		TEXT("DayNightController: the sky light %s is not movable, so it cannot follow the viewer on a planet."), *GetNameSafe(SkyLight));
	bWarnedSkyLightNotMovable |= bEnabled && !bMovable;

	if (bEnabled && bMovable && Viewer)
	{
		if (!bSkyLightAtViewerApplied)
		{
			bSkyLightAtViewerApplied = true;
			AuthoredSkyLightLocation = SkyLight->GetActorLocation();
			bAuthoredLowerHemisphereIsBlack = Comp->bLowerHemisphereIsBlack;
			// "Lower" is world -Z: on a planet that can be half the sky. The atmosphere draws the planet's ground under the
			// local horizon, so the captured sky serves the whole sphere.
			if (Comp->bLowerHemisphereIsBlack)
			{
				Comp->bLowerHemisphereIsBlack = false;
				Comp->MarkRenderStateDirty();
			}
			UE_LOG(LogWorldClock, Log, TEXT("DayNightController: planet sky light %s follows the camera (it was at %s)."),
				*GetNameSafe(SkyLight), *AuthoredSkyLightLocation.ToCompactString());
		}
		if (!SkyLight->GetActorLocation().Equals(*Viewer, 1.0))
		{
			SkyLight->SetActorLocation(*Viewer);
		}
	}
	else if (bSkyLightAtViewerApplied && !bEnabled)
	{
		bSkyLightAtViewerApplied = false;
		SkyLight->SetActorLocation(AuthoredSkyLightLocation);
		if (Comp->bLowerHemisphereIsBlack != bAuthoredLowerHemisphereIsBlack)
		{
			Comp->bLowerHemisphereIsBlack = bAuthoredLowerHemisphereIsBlack;
			Comp->MarkRenderStateDirty();
		}
		UE_LOG(LogWorldClock, Log, TEXT("DayNightController: planet sky light %s back at %s."), *GetNameSafe(SkyLight), *AuthoredSkyLightLocation.ToCompactString());
	}
}

void ADayNightController::ApplyPlanetAtmosphereShadows()
{
	UDirectionalLightComponent* Comp = SunLight ? Cast<UDirectionalLightComponent>(SunLight->GetLightComponent()) : nullptr;
	if (!Comp)
	{
		return;
	}
	if (!bAuthoredAtmosphereShadowsCaptured)
	{
		bAuthoredAtmosphereShadowsCaptured = true;
		bAuthoredCastShadowsOnAtmosphere = Comp->bCastShadowsOnAtmosphere;
	}
	const bool bLimit = bLimitPlanetAtmosphereShadows && CVarWorldClockPlanetAtmosphereShadows.GetValueOnGameThread() != 0;
	const bool bWanted = bAuthoredCastShadowsOnAtmosphere && (!bLimit || ShouldShadowPlanetAtmosphere(PlanetSkyScale, PlanetAtmosphereShadowMaxDensity));
	bAtmosphereShadowsLimited = bAuthoredCastShadowsOnAtmosphere && !bWanted;
	if (Comp->bCastShadowsOnAtmosphere != bWanted)
	{
		// No setter: the proxy copies the flag when it is created.
		Comp->bCastShadowsOnAtmosphere = bWanted;
		Comp->MarkRenderStateDirty();
		UE_LOG(LogWorldClock, Log, TEXT("DayNightController: the sun %s shadows on the atmosphere (the planet's scaled air %.0f times as dense as authored near the ground, the limit %.1f; wc.Planet.AtmosphereShadows %d)."),
			bWanted ? TEXT("casts") : TEXT("casts no"), 1.0f / FMath::Max(PlanetSkyScale, 1.0e-4f), PlanetAtmosphereShadowMaxDensity,
			CVarWorldClockPlanetAtmosphereShadows.GetValueOnGameThread());
	}
}

void ADayNightController::CaptureAuthoredValues()
{
	if (bCaptured)
	{
		return;
	}
	// ALight::GetLightComponent, not ADirectionalLight::GetComponent: that one is editor-only (a packaged game does not compile)
	if (const UDirectionalLightComponent* Comp = SunLight ? Cast<UDirectionalLightComponent>(SunLight->GetLightComponent()) : nullptr)
	{
		AuthoredSunIntensity = Comp->Intensity;
	}
	if (SkyLight && SkyLight->GetLightComponent())
	{
		AuthoredSkyLightIntensity = SkyLight->GetLightComponent()->Intensity;
	}
	if (HeightFog && HeightFog->GetComponent())
	{
		AuthoredFogDensity = HeightFog->GetComponent()->FogDensity;
	}
	bCaptured = true;
}

FLinearColor ADayNightController::SunColorFor(float Daylight, float ElevationDegrees, const FLinearColor& Night, const FLinearColor& Day, const FLinearColor& Horizon)
{
	const FLinearColor Base = FMath::Lerp(Night, Day, FMath::Clamp(Daylight, 0.0f, 1.0f));
	// Warm tint while the sun sits within 12 degrees of the horizon (above it), strongest right at the horizon.
	const float HorizonBlend = (ElevationDegrees >= -2.0f && ElevationDegrees <= 12.0f)
		? 1.0f - FMath::Clamp(ElevationDegrees / 12.0f, 0.0f, 1.0f)
		: 0.0f;
	return FMath::Lerp(Base, Horizon, HorizonBlend * FMath::Clamp(Daylight * 2.0f, 0.0f, 1.0f));
}

void ADayNightController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const UWorldClockSubsystem* Clock = UWorldClockSubsystem::Get(this);
	if (!Clock)
	{
		return;
	}
	Apply(*Clock);
}

void ADayNightController::Apply(const UWorldClockSubsystem& Clock)
{
	const UWorldClockSettings* S = UWorldClockSettings::Get();
	// On a planet the light is the sun's true elevation where the player stands, the hour the local one (D12).
	FVector Viewer;
	const bool bPlanet = Clock.HasPlanetFrame();
	const bool bLocal = bPlanet && GetViewerLocation(Viewer);
	if (bPlanet)
	{
		ApplyPlanetSky(Clock);
		ApplyPlanetAtmosphereShadows();
		ApplySpaceExposure(Clock);
		FVector Camera;
		ApplyPlanetSkyLight(GetCameraLocation(Camera) ? &Camera : nullptr);
	}
	const float Hour = bLocal ? Clock.GetLocalHour(Viewer) : Clock.GetHour();
	const float Daylight = bLocal ? Clock.GetLocalDaylight(Viewer) : Clock.GetDaylight();
	const float Elevation = bLocal ? Clock.GetLocalSunElevation(Viewer) : UWorldClockMath::SunElevationForHour(Hour, S->SunriseHour, S->SunsetHour);
	LastDaylight = Daylight;

	if (SunLight)
	{
		SunLight->SetActorRotation(Clock.GetSunRotation());
		if (UDirectionalLightComponent* Comp = Cast<UDirectionalLightComponent>(SunLight->GetLightComponent()))
		{
			Comp->SetIntensity(AuthoredSunIntensity * SunIntensityByDaylight.GetRichCurveConst()->Eval(Daylight));
			Comp->SetLightColor(SunColorFor(Daylight, Elevation, SunColorNight, SunColorDay, SunColorHorizon));
		}
	}
	if (SkyLight && SkyLight->GetLightComponent())
	{
		SkyLight->GetLightComponent()->SetIntensity(AuthoredSkyLightIntensity * SkyLightIntensityByDaylight.GetRichCurveConst()->Eval(Daylight));
	}
	if (HeightFog && HeightFog->GetComponent() && !bPlanet)
	{
		UExponentialHeightFogComponent* Fog = HeightFog->GetComponent();
		Fog->SetFogDensity(AuthoredFogDensity * FogDensityByDaylight.GetRichCurveConst()->Eval(Daylight));
		Fog->SetFogInscatteringColor(FMath::Lerp(FogColorNight, FogColorDay, Daylight));
	}
	if (ExposurePostProcess)
	{
		ExposurePostProcess->bEnabled = bDriveExposure;
		ExposurePostProcess->Settings.bOverride_AutoExposureBias = true;
		ExposurePostProcess->Settings.AutoExposureBias = ExposureBiasByDaylight.GetRichCurveConst()->Eval(Daylight);
	}
	if (ParameterCollection)
	{
		UKismetMaterialLibrary::SetScalarParameterValue(this, ParameterCollection, TEXT("TimeOfDay"), Hour);
		UKismetMaterialLibrary::SetScalarParameterValue(this, ParameterCollection, TEXT("Daylight"), Daylight);
	}
}
