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
	if (Sky)
	{
		// The engine's sky is already a sphere around a planet: tell it where the planet is (BottomRadius in km).
		Sky->SetMobility(EComponentMobility::Movable);
		Sky->TransformMode = ESkyAtmosphereTransformMode::PlanetCenterAtComponentTransform;
		Sky->SetWorldLocation(Clock.GetPlanetCenter());
		Sky->SetBottomRadius(FMath::Max(Clock.GetPlanetSeaRadius() / 100000.0f, 0.1f));
		Sky->MarkRenderStateDirty();
	}
	int32 Hidden = 0;
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (!ZUpSkyTag.IsNone() && It->ActorHasTag(ZUpSkyTag))
			{
				It->SetActorHiddenInGame(true);
				++Hidden;
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
	UE_LOG(LogWorldClock, Log, TEXT("DayNightController: planet sky: atmosphere %s at %s (ground %.3f km), %d Z-up sky actor(s) hidden, height fog %s."),
		*GetNameSafe(SkyAtmosphereActor), *Clock.GetPlanetCenter().ToCompactString(), Clock.GetPlanetSeaRadius() / 100000.0f, Hidden,
		HeightFog ? TEXT("hidden") : TEXT("none"));
}

void ADayNightController::CaptureAuthoredValues()
{
	if (bCaptured)
	{
		return;
	}
	if (SunLight && SunLight->GetComponent())
	{
		AuthoredSunIntensity = SunLight->GetComponent()->Intensity;
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
	}
	const float Hour = bLocal ? Clock.GetLocalHour(Viewer) : Clock.GetHour();
	const float Daylight = bLocal ? Clock.GetLocalDaylight(Viewer) : Clock.GetDaylight();
	const float Elevation = bLocal ? Clock.GetLocalSunElevation(Viewer) : UWorldClockMath::SunElevationForHour(Hour, S->SunriseHour, S->SunsetHour);
	LastDaylight = Daylight;

	if (SunLight)
	{
		SunLight->SetActorRotation(Clock.GetSunRotation());
		if (UDirectionalLightComponent* Comp = SunLight->GetComponent())
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
