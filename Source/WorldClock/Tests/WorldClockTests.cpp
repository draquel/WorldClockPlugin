// Copyright Daniel Raquel. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "DayNightController.h"
#include "WorldClockMath.h"
#include "WorldClockTags.h"
#include "WorldClockTypes.h"
#include "WorldClockSubsystem.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

#if WITH_DEV_AUTOMATION_TESTS

#define WORLDCLOCK_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// ---------------------------------------------------------------------------
// Sun arc: horizon at sunrise / sunset, overhead halfway through the day, below at midnight.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldClockSunRotationTest, "WorldClock.Math.SunRotation", WORLDCLOCK_TEST_FLAGS)
bool FWorldClockSunRotationTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Sunrise on the horizon"), UWorldClockMath::SunElevationForHour(6.0f, 6.0f, 18.0f), 0.0f, 0.01f);
	TestEqual(TEXT("Noon overhead"), UWorldClockMath::SunElevationForHour(12.0f, 6.0f, 18.0f), 90.0f, 0.01f);
	TestEqual(TEXT("Sunset on the horizon"), UWorldClockMath::SunElevationForHour(18.0f, 6.0f, 18.0f), 0.0f, 0.01f);
	TestEqual(TEXT("Midnight straight below"), UWorldClockMath::SunElevationForHour(0.0f, 6.0f, 18.0f), -90.0f, 0.01f);
	TestTrue(TEXT("Mid-morning well above the horizon"), UWorldClockMath::SunElevationForHour(9.0f, 6.0f, 18.0f) > 60.0f);

	// A short day (8-16) still peaks overhead at its own midpoint.
	TestEqual(TEXT("Short day noon"), UWorldClockMath::SunElevationForHour(12.0f, 8.0f, 16.0f), 90.0f, 0.01f);

	// The light rotation points down at noon and its yaw sweeps 180 degrees between sunrise and sunset.
	const FRotator Noon = UWorldClockMath::SunRotationForHour(12.0f, 6.0f, 18.0f, 0.0f);
	TestEqual(TEXT("Noon light pitches straight down"), static_cast<float>(Noon.Pitch), -90.0f, 0.01f);
	const FRotator Rise = UWorldClockMath::SunRotationForHour(6.0f, 6.0f, 18.0f, 30.0f);
	const FRotator Set = UWorldClockMath::SunRotationForHour(18.0f, 6.0f, 18.0f, 30.0f);
	TestEqual(TEXT("Sunrise yaw = azimuth + 180"), static_cast<float>(FRotator::NormalizeAxis(Rise.Yaw)), static_cast<float>(FRotator::NormalizeAxis(210.0)), 0.01f);
	TestEqual(TEXT("Sunset yaw = azimuth"), static_cast<float>(FRotator::NormalizeAxis(Set.Yaw)), static_cast<float>(FRotator::NormalizeAxis(30.0)), 0.01f);
	return true;
}

// ---------------------------------------------------------------------------
// Phases and daylight.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldClockPhasesTest, "WorldClock.Math.Phases", WORLDCLOCK_TEST_FLAGS)
bool FWorldClockPhasesTest::RunTest(const FString& Parameters)
{
	auto Phase = [](float Hour) { return UWorldClockMath::PhaseForHour(Hour, 6.0f, 18.0f, 1.0f); };
	TestEqual(TEXT("05:30 is dawn"), Phase(5.5f), EWorldClockPhase::Dawn);
	TestEqual(TEXT("06:00 is day"), Phase(6.0f), EWorldClockPhase::Day);
	TestEqual(TEXT("12:00 is day"), Phase(12.0f), EWorldClockPhase::Day);
	TestEqual(TEXT("18:30 is dusk"), Phase(18.5f), EWorldClockPhase::Dusk);
	TestEqual(TEXT("19:00 is night"), Phase(19.0f), EWorldClockPhase::Night);
	TestEqual(TEXT("23:59 is night"), Phase(23.99f), EWorldClockPhase::Night);
	TestEqual(TEXT("04:59 is night"), Phase(4.99f), EWorldClockPhase::Night);

	// Windows that wrap past midnight (a 00:30 sunrise puts dawn at 23:30).
	TestEqual(TEXT("23:45 is dawn for a 00:30 sunrise"), UWorldClockMath::PhaseForHour(23.75f, 0.5f, 12.0f, 1.0f), EWorldClockPhase::Dawn);

	auto Daylight = [](float Hour) { return UWorldClockMath::DaylightForHour(Hour, 6.0f, 18.0f, 1.0f); };
	TestEqual(TEXT("Midnight daylight 0"), Daylight(0.0f), 0.0f, 0.001f);
	TestEqual(TEXT("Noon daylight 1"), Daylight(12.0f), 1.0f, 0.001f);
	TestEqual(TEXT("Sunrise daylight half"), Daylight(6.0f), 0.5f, 0.001f);
	TestEqual(TEXT("Sunset daylight half"), Daylight(18.0f), 0.5f, 0.001f);
	TestTrue(TEXT("Daylight climbs through dawn"), Daylight(5.5f) < Daylight(6.0f) && Daylight(6.0f) < Daylight(6.5f));
	TestEqual(TEXT("After dusk daylight 0"), Daylight(19.5f), 0.0f, 0.001f);

	TestTrue(TEXT("Phase tag"), UWorldClockMath::PhaseTag(EWorldClockPhase::Dusk) == FGameplayTag(WorldClockTags::Time_Phase_Dusk));
	return true;
}

// ---------------------------------------------------------------------------
// Client extrapolation reproduces the authority, wrapping into new days.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldClockExtrapolationTest, "WorldClock.Clock.Extrapolation", WORLDCLOCK_TEST_FLAGS)
bool FWorldClockExtrapolationTest::RunTest(const FString& Parameters)
{
	int32 Day = 0;
	// Sync at 23:00 on day 3 with scale 60: 120 real seconds later it is 01:00 on day 4.
	const double T = UWorldClockMath::Extrapolate(23.0 * 3600.0, 3, 100.0, 60.0f, 220.0, Day);
	TestEqual(TEXT("Wrapped into the next day"), Day, 4);
	TestEqual(TEXT("01:00"), T, 3600.0, 0.001);

	// Time scale 0 holds.
	const double Held = UWorldClockMath::Extrapolate(5000.0, 1, 10.0, 0.0f, 999.0, Day);
	TestEqual(TEXT("Paused clock holds"), Held, 5000.0, 0.001);
	TestEqual(TEXT("Paused clock keeps its day"), Day, 1);

	// Server time never runs backwards for the clock (a stale sync point is clamped).
	const double Clamped = UWorldClockMath::Extrapolate(5000.0, 1, 50.0, 60.0f, 40.0, Day);
	TestEqual(TEXT("Negative elapsed clamps to the sync point"), Clamped, 5000.0, 0.001);

	// Wrap helper counts several days at once.
	int32 D = 1;
	const double Wrapped = UWorldClockMath::WrapTimeOfDay(86400.0 * 2.5, D);
	TestEqual(TEXT("Two and a half days -> day 3"), D, 3);
	TestEqual(TEXT("... at noon"), Wrapped, 43200.0, 0.001);
	TestEqual(TEXT("Format"), UWorldClockMath::FormatTimeOfDay(8.0 * 3600.0 + 24.0 * 60.0).ToString(), FString(TEXT("08:24")));
	return true;
}

// ---------------------------------------------------------------------------
// Sun colour: night blue, day white, warm at the horizon by day only.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldClockSunColorTest, "WorldClock.Lighting.SunColor", WORLDCLOCK_TEST_FLAGS)
bool FWorldClockSunColorTest::RunTest(const FString& Parameters)
{
	const FLinearColor Night(0.5f, 0.6f, 1.0f), Day(1.0f, 1.0f, 1.0f), Horizon(1.0f, 0.6f, 0.3f);
	TestTrue(TEXT("Midnight is the night colour"), ADayNightController::SunColorFor(0.0f, -90.0f, Night, Day, Horizon).Equals(Night, 0.001f));
	TestTrue(TEXT("Noon is the day colour"), ADayNightController::SunColorFor(1.0f, 90.0f, Night, Day, Horizon).Equals(Day, 0.001f));
	const FLinearColor Dusk = ADayNightController::SunColorFor(0.5f, 0.0f, Night, Day, Horizon);
	TestTrue(TEXT("Horizon by day is warm"), Dusk.R > Dusk.B);
	TestTrue(TEXT("Below the horizon at night stays cool"), ADayNightController::SunColorFor(0.0f, -30.0f, Night, Day, Horizon).Equals(Night, 0.001f));
	return true;
}

// ---------------------------------------------------------------------------
// Planets (spherical Phase 4, D12; VoxelWorlds WORLD_MODES_REVIEW_PLAN.md §18, WM-SY14): local solar time by longitude,
// a world-fixed sun turning about +Z once a day, daylight by the sun's true elevation equal to the hour curve on the
// equator; the clock subsystem's Local* functions are the global values without a planet frame.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldClockPlanetSolarTimeTest, "WorldClock.Planet.SolarTime", WORLDCLOCK_TEST_FLAGS)
bool FWorldClockPlanetSolarTimeTest::RunTest(const FString& Parameters)
{
	// Longitudes and the sun.
	TestTrue(TEXT("Longitude 90: six hours ahead"), FMath::IsNearlyEqual(UWorldClockMath::LocalHourOffset(FVector(0, 1, 0)), 6.0f, 1.0e-4f));
	TestTrue(TEXT("Longitude -90: six hours behind"), FMath::IsNearlyEqual(UWorldClockMath::LocalHourOffset(FVector(0, -1, 0)), -6.0f, 1.0e-4f));
	TestEqual(TEXT("Longitude 0: the clock's hour"), UWorldClockMath::LocalHourOffset(FVector(1, 0, 0.5)), 0.0f);
	TestEqual(TEXT("A pole reads as longitude 0"), UWorldClockMath::LocalHourOffset(FVector(0, 0, 1)), 0.0f);
	TestTrue(TEXT("Noon at hour 12 over longitude 0"), UWorldClockMath::PlanetSunDirection(12.0f).Equals(FVector(1, 0, 0), 1.0e-6));
	TestTrue(TEXT("Hour 18: the sun over longitude -90"), UWorldClockMath::PlanetSunDirection(18.0f).Equals(FVector(0, -1, 0), 1.0e-6));
	TestEqual(TEXT("WrapHour(-1) = 23"), UWorldClockMath::WrapHour(-1.0f), 23.0f);

	// On the equator at any longitude the sun climbs 15 degrees an hour and the light follows the hour curve.
	double WorstElevation = 0.0, WorstDaylight = 0.0;
	for (const float Longitude : { 0.0f, 37.0f, -120.0f, 179.0f })
	{
		const FVector Here(FMath::Cos(FMath::DegreesToRadians(Longitude)), FMath::Sin(FMath::DegreesToRadians(Longitude)), 0.0);
		for (float Local = 0.0f; Local < 24.0f; Local += 0.25f)
		{
			const float Clock = UWorldClockMath::WrapHour(Local - Longitude / 15.0f);
			const double Elevation = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(FVector::DotProduct(Here, UWorldClockMath::PlanetSunDirection(Clock)), -1.0, 1.0)));
			WorstElevation = FMath::Max(WorstElevation, FMath::Abs(Elevation - (90.0 - FMath::Abs(Local - 12.0) * 15.0)));
			const float ByElevation = UWorldClockMath::DaylightForElevation(static_cast<float>(Elevation), 6.0f, 18.0f, 1.0f);
			WorstDaylight = FMath::Max(WorstDaylight, static_cast<double>(FMath::Abs(ByElevation - UWorldClockMath::DaylightForHour(Local, 6.0f, 18.0f, 1.0f))));
		}
	}
	AddInfo(FString::Printf(TEXT("Equator: worst elevation %.2e deg, worst daylight %.2e"), WorstElevation, WorstDaylight));
	TestTrue(TEXT("Equator: 15 degrees an hour from sunrise"), WorstElevation < 1.0e-3);
	TestTrue(TEXT("Equator: daylight by elevation equals the hour curve"), WorstDaylight < 1.0e-3);
	TestTrue(TEXT("A pole sits in twilight all day"), FMath::IsNearlyEqual(UWorldClockMath::DaylightForElevation(0.0f, 6.0f, 18.0f, 1.0f),
		UWorldClockMath::DaylightForHour(6.0f, 6.0f, 18.0f, 1.0f), 1.0e-6f));

	// The subsystem in a game world.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("WorldClockPlanetTest"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	UWorldClockSubsystem* Clock = World->GetSubsystem<UWorldClockSubsystem>();
	if (TestNotNull(TEXT("The game world has a clock"), Clock))
	{
		Clock->SetTimeOfDay(9.5f);
		const FVector Here(250000.0, 250000.0, 1000.0);
		TestFalse(TEXT("No planet frame by default"), Clock->HasPlanetFrame());
		TestEqual(TEXT("No frame: the local hour is the hour"), Clock->GetLocalHour(Here), Clock->GetHour());
		TestEqual(TEXT("No frame: the local day is the day"), Clock->GetLocalDay(Here), Clock->GetDay());
		TestTrue(TEXT("No frame: the local phase is the phase"), Clock->GetLocalPhase(Here) == Clock->GetPhase());
		TestEqual(TEXT("No frame: the local daylight is the daylight"), Clock->GetLocalDaylight(Here), Clock->GetDaylight());
		TestTrue(TEXT("No frame: the local text is the text"), Clock->GetLocalTimeText(Here).EqualTo(Clock->GetTimeText()));
		const FRotator PlaneSun = Clock->GetSunRotation();

		const FVector Centre(1000.0, -2000.0, 500.0);
		Clock->SetPlanetFrame(Centre, 249500.0f);
		const FVector East = Centre + FVector(0.0, 1.0, 0.0) * 250000.0; // longitude 90
		TestTrue(TEXT("Longitude 90 is six hours ahead"), FMath::IsNearlyEqual(Clock->GetLocalHour(East), 15.5f, 1.0e-3f));
		TestTrue(TEXT("The sun light points away from the sun"), Clock->GetSunRotation().Vector().Equals(-UWorldClockMath::PlanetSunDirection(9.5f), 1.0e-5));
		TestTrue(TEXT("The phase by the local hour"), Clock->GetLocalPhase(East) == UWorldClockMath::PhaseForHour(15.5f, 6.0f, 18.0f, 1.0f));

		int32 GlobalDay = 0;
		double GlobalSeconds = 0.0;
		Clock->LocalToGlobal(East, Clock->GetLocalDay(East), Clock->GetLocalHour(East), GlobalDay, GlobalSeconds);
		TestTrue(TEXT("LocalToGlobal inverts the local clock"), GlobalDay == Clock->GetDay() && FMath::IsNearlyEqual(GlobalSeconds, Clock->GetTimeOfDaySeconds(), 1.0));

		const FVector West = Centre + FVector(0.0, -1.0, 0.0) * 250000.0; // longitude -90
		Clock->SetLocalTimeOfDay(West, 7.25f);
		AddInfo(FString::Printf(TEXT("Local 07:15 at longitude -90: clock %s day %d"), *Clock->GetTimeText().ToString(), Clock->GetDay()));
		TestTrue(TEXT("SetLocalTimeOfDay sets the local hour there"), FMath::IsNearlyEqual(Clock->GetLocalHour(West), 7.25f, 1.0e-3f));
		TestTrue(TEXT("... which is 13:15 on the clock"), FMath::IsNearlyEqual(Clock->GetHour(), 13.25f, 1.0e-3f));

		Clock->ClearPlanetFrame();
		Clock->SetTimeOfDay(9.5f);
		TestTrue(TEXT("Cleared: the plane's sun again"), Clock->GetSunRotation().Equals(PlaneSun, 1.0e-4));
	}
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// ---------------------------------------------------------------------------
// Planets (WM-SY15): with a planet frame the controller centres the SkyAtmosphere on the planet with its ground at the
// sea radius and hides the tagged Z-up sky and the height fog; without one it touches none of them.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldClockPlanetSkyTest, "WorldClock.Planet.Sky", WORLDCLOCK_TEST_FLAGS)
bool FWorldClockPlanetSkyTest::RunTest(const FString& Parameters)
{
	for (const bool bPlanet : { false, true })
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, bPlanet ? TEXT("WorldClockSkyPlanet") : TEXT("WorldClockSkyPlane"));
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		Context.SetCurrentWorld(World);
		UWorldClockSubsystem* Clock = World->GetSubsystem<UWorldClockSubsystem>();
		ASkyAtmosphere* SkyActor = World->SpawnActor<ASkyAtmosphere>();
		AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>();
		AVolumetricCloud* CloudActor = World->SpawnActor<AVolumetricCloud>();
		UVolumetricCloudComponent* Cloud = CloudActor ? CloudActor->FindComponentByClass<UVolumetricCloudComponent>() : nullptr;
		AActor* SkySphere = World->SpawnActor<AActor>();
		SkySphere->Tags.Add(TEXT("WorldClock.ZUpSky"));
		ASkyLight* SkyLightActor = World->SpawnActor<ASkyLight>();
		USkyLightComponent* SkyLightComp = SkyLightActor ? SkyLightActor->GetLightComponent() : nullptr;
		// The sun casting shadows on the atmosphere (PlanetTest's)
		ADirectionalLight* SunActor = World->SpawnActor<ADirectionalLight>();
		UDirectionalLightComponent* SunComp = SunActor ? Cast<UDirectionalLightComponent>(SunActor->GetLightComponent()) : nullptr;
		if (SunComp)
		{
			SunComp->bCastShadowsOnAtmosphere = true;
		}
		ADayNightController* Controller = World->SpawnActor<ADayNightController>();
		USkyAtmosphereComponent* Sky = SkyActor ? SkyActor->FindComponentByClass<USkyAtmosphereComponent>() : nullptr;
		if (TestNotNull(TEXT("A clock"), Clock) && TestNotNull(TEXT("A sky atmosphere"), Sky) && TestNotNull(TEXT("A controller"), Controller)
			&& TestNotNull(TEXT("A cloud layer"), Cloud))
		{
			const float CloudBottomBefore = Cloud->LayerBottomAltitude;
			const float CloudHeightBefore = Cloud->LayerHeight;
			const float CloudTracingBefore = Cloud->TracingMaxDistance;
			const float CloudShadowBefore = Cloud->ShadowTracingDistance;
			const float CloudStartBefore = Cloud->TracingStartMaxDistance;
			const ESkyAtmosphereTransformMode ModeBefore = Sky->TransformMode;
			const float BottomBefore = Sky->BottomRadius;
			const float HeightBefore = Sky->AtmosphereHeight;
			const float RayleighBefore = Sky->RayleighExponentialDistribution;
			const float MieBefore = Sky->MieExponentialDistribution;
			const float RayleighColumnBefore = Sky->RayleighExponentialDistribution * Sky->RayleighScatteringScale;
			const float MieColumnBefore = Sky->MieExponentialDistribution * Sky->MieScatteringScale;
			const FVector Centre(1000.0, -2000.0, 500.0);
			if (bPlanet)
			{
				// The small review planet: the sea 500 under a 250,000 datum, terrain up to 6,500 over the sea.
				Clock->SetPlanetFrame(Centre, 249500.0f, 6500.0f);
			}
			// A movable sky light at the planet's centre, its lower hemisphere black (PlanetTest's)
			if (SkyLightComp)
			{
				SkyLightComp->SetMobility(EComponentMobility::Movable);
				SkyLightComp->bLowerHemisphereIsBlack = true;
				SkyLightActor->SetActorLocation(Centre);
			}
			Controller->SkyAtmosphereActor = SkyActor;
			Controller->HeightFog = Fog;
			Controller->SkyLight = SkyLightActor;
			Controller->SunLight = SunActor;
			Controller->Tick(0.0f);
			if (bPlanet)
			{
				TestTrue(TEXT("Planet: the atmosphere's planet centre is its component"), Sky->TransformMode == ESkyAtmosphereTransformMode::PlanetCenterAtComponentTransform);
				TestTrue(TEXT("Planet: the component at the planet centre"), Sky->GetComponentLocation().Equals(Centre, 1.0));
				TestTrue(TEXT("Planet: the ground at the sea radius (km)"), FMath::IsNearlyEqual(Sky->BottomRadius, 2.495f, 1.0e-4f));
				TestTrue(TEXT("Planet: the Z-up sky hidden"), SkySphere->IsHidden());
				TestTrue(TEXT("Planet: the height fog hidden"), Fog->IsHidden());

				// Scaled to the planet (spherical Phase 6, WM-BP10): 4 x 65 m over a 60 km authored height.
				const float K = Controller->GetPlanetSkyScale();
				AddInfo(FString::Printf(TEXT("Planet sky scale %.5f: atmosphere %.4f km (authored %.1f), Rayleigh %.5f km, Mie %.6f km"),
					K, Sky->AtmosphereHeight, HeightBefore, Sky->RayleighExponentialDistribution, Sky->MieExponentialDistribution));
				TestTrue(TEXT("Planet: the scale keeps the atmosphere four times the terrain"), FMath::IsNearlyEqual(K, 4.0f * 0.065f / HeightBefore, 1.0e-4f));
				TestTrue(TEXT("Planet: the atmosphere's height scaled"), FMath::IsNearlyEqual(Sky->AtmosphereHeight, HeightBefore * K, 1.0e-4f));
				TestTrue(TEXT("Planet: the Rayleigh and Mie scale heights scaled"),
					FMath::IsNearlyEqual(Sky->RayleighExponentialDistribution, RayleighBefore * K, 1.0e-5f) && FMath::IsNearlyEqual(Sky->MieExponentialDistribution, MieBefore * K, 1.0e-6f));
				TestTrue(TEXT("Planet: a column of air holds what the authored one holds"),
					FMath::IsNearlyEqual(Sky->RayleighExponentialDistribution * Sky->RayleighScatteringScale, RayleighColumnBefore, 1.0e-4f * RayleighColumnBefore)
					&& FMath::IsNearlyEqual(Sky->MieExponentialDistribution * Sky->MieScatteringScale, MieColumnBefore, 1.0e-4f * MieColumnBefore));

				// The cloud layer with it: its thickness by K, its floor over the highest terrain, inside the atmosphere.
				AddInfo(FString::Printf(TEXT("Planet clouds %.4f to %.4f km (authored %.1f to %.1f)"), Cloud->LayerBottomAltitude,
					Cloud->LayerBottomAltitude + Cloud->LayerHeight, CloudBottomBefore, CloudBottomBefore + CloudHeightBefore));
				TestTrue(TEXT("Planet: the cloud layer's thickness scaled"), FMath::IsNearlyEqual(Cloud->LayerHeight, CloudHeightBefore * K, 1.0e-5f));
				TestTrue(TEXT("Planet: the cloud layer's floor clear over the highest terrain"),
					Cloud->LayerBottomAltitude >= Controller->PlanetCloudFloorOverTerrain * 0.065f - 1.0e-6f && Controller->PlanetCloudFloorOverTerrain > 1.0f);
				TestTrue(TEXT("Planet: the cloud layer inside the atmosphere"), Cloud->LayerBottomAltitude + Cloud->LayerHeight <= Sky->AtmosphereHeight);
				TestTrue(TEXT("Planet: the clouds' tracing distances scaled"), FMath::IsNearlyEqual(Cloud->TracingMaxDistance, CloudTracingBefore * K, 1.0e-4f)
					&& FMath::IsNearlyEqual(Cloud->ShadowTracingDistance, CloudShadowBefore * K, 1.0e-4f)
					&& FMath::IsNearlyEqual(Cloud->TracingStartMaxDistance, CloudStartBefore * K, 1.0e-3f));
				// Under PlanetCloudMinRadiusKm (D-VP1): no volumetric clouds on the 2.5 km planet.
				TestFalse(TEXT("Planet: no volumetric clouds on the small planet"), Cloud->IsVisible());
				TestEqual(TEXT("Planet: one cloud layer reported hidden"), Controller->GetHiddenCloudLayers(), 1);

				// The sky light follows the viewer (visual polish VP1-O5): without one it stays put; with one it moves there
				// and takes its lower hemisphere from the captured sky; bPlanetSkyLightAtViewer off puts both back.
				if (TestNotNull(TEXT("A sky light"), SkyLightComp))
				{
					TestTrue(TEXT("Planet sky light: no viewer, no move"), SkyLightActor->GetActorLocation().Equals(Centre, 1.0) && !Controller->IsSkyLightAtViewer());
					const FVector Viewer = Centre + FVector(0.6, 0.0, 0.8) * 250180.0;
					Controller->ApplyPlanetSkyLight(&Viewer);
					TestTrue(TEXT("Planet sky light: at the viewer"), SkyLightActor->GetActorLocation().Equals(Viewer, 1.0) && Controller->IsSkyLightAtViewer());
					TestFalse(TEXT("Planet sky light: the lower hemisphere from the captured sky"), SkyLightComp->bLowerHemisphereIsBlack);
					Controller->bPlanetSkyLightAtViewer = false;
					Controller->ApplyPlanetSkyLight(&Viewer);
					TestTrue(TEXT("Planet sky light off: back where the map put it"), SkyLightActor->GetActorLocation().Equals(Centre, 1.0) && !Controller->IsSkyLightAtViewer());
					TestTrue(TEXT("Planet sky light off: its lower hemisphere black again"), SkyLightComp->bLowerHemisphereIsBlack);
					Controller->bPlanetSkyLightAtViewer = true;
				}

				// The sun's shadows on the atmosphere (visual polish VP1-O6): off in the small planet's dense scaled air;
				// bLimitPlanetAtmosphereShadows off gives the light its own setting back, on takes it again.
				if (TestNotNull(TEXT("A sun"), SunComp))
				{
					TestFalse(TEXT("Planet sun: no shadows on the dense scaled atmosphere"), SunComp->bCastShadowsOnAtmosphere);
					TestTrue(TEXT("Planet sun: reported as limited"), Controller->AreAtmosphereShadowsLimited());
					Controller->bLimitPlanetAtmosphereShadows = false;
					Controller->Tick(0.0f);
					TestTrue(TEXT("Planet sun, no limit: the light's own shadows on the atmosphere"), SunComp->bCastShadowsOnAtmosphere && !Controller->AreAtmosphereShadowsLimited());
					Controller->bLimitPlanetAtmosphereShadows = true;
					Controller->Tick(0.0f);
					TestFalse(TEXT("Planet sun, the limit again: off again"), SunComp->bCastShadowsOnAtmosphere);
					// A limit above the air's density keeps them
					Controller->PlanetAtmosphereShadowMaxDensity = 1.0f / Controller->GetPlanetSkyScale() + 1.0f;
					Controller->Tick(0.0f);
					TestTrue(TEXT("Planet sun: kept under a higher limit"), SunComp->bCastShadowsOnAtmosphere);
				}
			}
			else
			{
				TestTrue(TEXT("Plane: the atmosphere untouched"), Sky->TransformMode == ModeBefore && Sky->BottomRadius == BottomBefore);
				TestFalse(TEXT("Plane: the sky shown"), SkySphere->IsHidden());
				TestFalse(TEXT("Plane: the fog shown"), Fog->IsHidden());
				TestTrue(TEXT("Plane: the clouds untouched"), Cloud->LayerBottomAltitude == CloudBottomBefore && Cloud->LayerHeight == CloudHeightBefore
					&& Cloud->TracingMaxDistance == CloudTracingBefore && Cloud->IsVisible() && Controller->GetHiddenCloudLayers() == 0);
				TestTrue(TEXT("Plane: the sky light untouched"), !SkyLightComp || (SkyLightActor->GetActorLocation().Equals(Centre, 1.0) && SkyLightComp->bLowerHemisphereIsBlack));
				TestTrue(TEXT("Plane: the sun's shadows on the atmosphere untouched"), !SunComp || SunComp->bCastShadowsOnAtmosphere);
			}
		}
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}
	return true;
}

// ---------------------------------------------------------------------------
// Spherical Phase 6, WM-BP10: the planet sky's scale and the space exposure. The scale is the planet's radius over Earth's
// unless the terrain needs more air (four times its top), at most 1; the space exposure fades in from half the
// atmosphere's height to one and a half.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldClockPlanetSkyScaleTest, "WorldClock.Planet.SkyScale", WORLDCLOCK_TEST_FLAGS)
bool FWorldClockPlanetSkyScaleTest::RunTest(const FString& Parameters)
{
	// Review_Planet_L: R 1,000 km, the sea 833 m under the datum, terrain to 7.6 km over the datum: the terrain decides.
	const float L = ADayNightController::ComputePlanetSkyScale(999.17f, 8.45f, 60.0f, 4.0f);
	// Review_Planet_M: R 50 km, terrain to 2.2 km over the sea.
	const float M = ADayNightController::ComputePlanetSkyScale(49.75f, 2.43f, 60.0f, 4.0f);
	// A planet of Earth's size with low relief: the radius decides.
	const float Earthlike = ADayNightController::ComputePlanetSkyScale(6000.0f, 1.0f, 60.0f, 4.0f);
	// Bigger than Earth: never more than the authored atmosphere.
	const float Giant = ADayNightController::ComputePlanetSkyScale(20000.0f, 1.0f, 60.0f, 4.0f);
	AddInfo(FString::Printf(TEXT("Sky scale: L %.4f (%.1f km of air), M %.4f (%.1f km), Earth-like %.4f, giant %.4f"), L, 60.0f * L, M, 60.0f * M, Earthlike, Giant));
	TestTrue(TEXT("L: the terrain decides"), FMath::IsNearlyEqual(L, 4.0f * 8.45f / 60.0f, 1.0e-5f));
	TestTrue(TEXT("M: the terrain decides"), FMath::IsNearlyEqual(M, 4.0f * 2.43f / 60.0f, 1.0e-5f));
	TestTrue(TEXT("Earth-like: the radius decides"), FMath::IsNearlyEqual(Earthlike, 6000.0f / 6360.0f, 1.0e-5f));
	TestEqual(TEXT("Giant: at most the authored atmosphere"), Giant, 1.0f);
	TestTrue(TEXT("Unknown terrain: the radius"), FMath::IsNearlyEqual(ADayNightController::ComputePlanetSkyScale(50.0f, 0.0f, 60.0f, 4.0f), 50.0f / 6360.0f, 1.0e-6f));

	// The cloud layer (the engine's: a 5 km floor, 10 km thick, traced from 350 km, 50 km through, 15 km of shadow) with
	// the sky (visual polish VP1-O1, D-VP1): its floor clear over the highest terrain (1.5 times it), inside the air, its
	// tracing distances by the same scale.
	const FWorldClockCloudLayer Engine;
	const FWorldClockCloudLayer CloudsL = ADayNightController::ComputePlanetCloudLayer(Engine, L, 8.45f, 1.5f);
	AddInfo(FString::Printf(TEXT("Clouds: L %.2f to %.2f km, traced %.1f km"), CloudsL.BottomKm, CloudsL.BottomKm + CloudsL.HeightKm, CloudsL.TracingMaxDistanceKm));
	TestTrue(TEXT("L clouds: the floor clear over the highest terrain"), FMath::IsNearlyEqual(CloudsL.BottomKm, 1.5f * 8.45f, 1.0e-4f));
	TestTrue(TEXT("L clouds: inside the air"), CloudsL.BottomKm + CloudsL.HeightKm <= 60.0f * L);
	const FWorldClockCloudLayer CloudsM = ADayNightController::ComputePlanetCloudLayer(Engine, M, 2.43f, 1.5f);
	AddInfo(FString::Printf(TEXT("Clouds: M %.2f to %.2f km, traced %.2f km, shadows %.2f km, from %.1f km"), CloudsM.BottomKm,
		CloudsM.BottomKm + CloudsM.HeightKm, CloudsM.TracingMaxDistanceKm, CloudsM.ShadowTracingDistanceKm, CloudsM.TracingStartMaxDistanceKm));
	TestTrue(TEXT("M clouds: the floor clear over the highest terrain"), FMath::IsNearlyEqual(CloudsM.BottomKm, 1.5f * 2.43f, 1.0e-4f));
	TestTrue(TEXT("M clouds: the thickness scaled"), FMath::IsNearlyEqual(CloudsM.HeightKm, 10.0f * M, 1.0e-5f));
	TestTrue(TEXT("M clouds: inside the air"), CloudsM.BottomKm + CloudsM.HeightKm <= 60.0f * M);
	TestTrue(TEXT("M clouds: the tracing distances scaled"), FMath::IsNearlyEqual(CloudsM.TracingStartMaxDistanceKm, 350.0f * M, 1.0e-3f)
		&& FMath::IsNearlyEqual(CloudsM.TracingMaxDistanceKm, 50.0f * M, 1.0e-4f) && FMath::IsNearlyEqual(CloudsM.ShadowTracingDistanceKm, 15.0f * M, 1.0e-4f)
		&& CloudsM.TracingStartDistanceFromCameraKm == 0.0f);
	const FWorldClockCloudLayer AtTerrain = ADayNightController::ComputePlanetCloudLayer(Engine, M, 2.43f, 1.0f);
	TestTrue(TEXT("M clouds, no clearance: the floor at the highest terrain"), FMath::IsNearlyEqual(AtTerrain.BottomKm, 2.43f, 1.0e-5f));
	const FWorldClockCloudLayer CloudsEarthlike = ADayNightController::ComputePlanetCloudLayer(Engine, Earthlike, 1.0f, 1.5f);
	TestTrue(TEXT("Earth-like clouds: the authored layer scaled"), FMath::IsNearlyEqual(CloudsEarthlike.BottomKm, 5.0f * Earthlike, 1.0e-4f)
		&& FMath::IsNearlyEqual(CloudsEarthlike.HeightKm, 10.0f * Earthlike, 1.0e-4f) && FMath::IsNearlyEqual(CloudsEarthlike.TracingMaxDistanceKm, 50.0f * Earthlike, 1.0e-3f));
	const FWorldClockCloudLayer Authored = ADayNightController::ComputePlanetCloudLayer(Engine, 1.0f, 0.0f, 1.5f);
	TestTrue(TEXT("The authored sky: the layer as authored"), Authored.BottomKm == Engine.BottomKm && Authored.HeightKm == Engine.HeightKm
		&& Authored.TracingStartMaxDistanceKm == Engine.TracingStartMaxDistanceKm && Authored.TracingMaxDistanceKm == Engine.TracingMaxDistanceKm
		&& Authored.ShadowTracingDistanceKm == Engine.ShadowTracingDistanceKm);

	// No volumetric clouds under the radius threshold (D-VP1, 10 km): none on PlanetTest (2.5 km), clouds on M and L.
	TestFalse(TEXT("Clouds: none on the small planet"), ADayNightController::ShouldDrawPlanetClouds(2.495f, 10.0f));
	TestTrue(TEXT("Clouds: on M"), ADayNightController::ShouldDrawPlanetClouds(49.75f, 10.0f));
	TestTrue(TEXT("Clouds: on L"), ADayNightController::ShouldDrawPlanetClouds(999.17f, 10.0f));
	TestTrue(TEXT("Clouds: at exactly the threshold"), ADayNightController::ShouldDrawPlanetClouds(10.0f, 10.0f));
	TestTrue(TEXT("Clouds: a threshold of 0 keeps them everywhere"), ADayNightController::ShouldDrawPlanetClouds(2.495f, 0.0f));

	TestEqual(TEXT("Space exposure: none on the ground"), ADayNightController::ComputeSpaceExposureWeight(0.0f, 30.0f), 0.0f);
	TestEqual(TEXT("Space exposure: none in the lower half of the air"), ADayNightController::ComputeSpaceExposureWeight(15.0f, 30.0f), 0.0f);
	TestTrue(TEXT("Space exposure: partly at the top of the air"), FMath::IsNearlyEqual(ADayNightController::ComputeSpaceExposureWeight(30.0f, 30.0f), 0.5f, 1.0e-6f));
	TestEqual(TEXT("Space exposure: whole in space"), ADayNightController::ComputeSpaceExposureWeight(45.0f, 30.0f), 1.0f);
	TestEqual(TEXT("Space exposure: none without an atmosphere"), ADayNightController::ComputeSpaceExposureWeight(1000.0f, 0.0f), 0.0f);

	// The sun's shadows on the atmosphere (visual polish VP1-O6): kept while the scaled air is at most twice as dense as authored.
	const float Small = ADayNightController::ComputePlanetSkyScale(2.5f, 0.065f, 60.0f, 4.0f);
	AddInfo(FString::Printf(TEXT("Air near the ground: PlanetTest %.0f, M %.1f, L %.1f, Earth-like %.2f times the authored"), 1.0f / Small, 1.0f / M, 1.0f / L, 1.0f / Earthlike));
	TestFalse(TEXT("Atmosphere shadows: off on the small planet (230 times the air)"), ADayNightController::ShouldShadowPlanetAtmosphere(Small, 2.0f));
	TestFalse(TEXT("Atmosphere shadows: off on M (6 times)"), ADayNightController::ShouldShadowPlanetAtmosphere(M, 2.0f));
	TestTrue(TEXT("Atmosphere shadows: kept on L (1.8 times)"), ADayNightController::ShouldShadowPlanetAtmosphere(L, 2.0f));
	TestTrue(TEXT("Atmosphere shadows: kept on an Earth-like planet"), ADayNightController::ShouldShadowPlanetAtmosphere(Earthlike, 2.0f));
	TestTrue(TEXT("Atmosphere shadows: kept on the authored atmosphere"), ADayNightController::ShouldShadowPlanetAtmosphere(1.0f, 1.0f));
	TestTrue(TEXT("Atmosphere shadows: kept at exactly the limit"), ADayNightController::ShouldShadowPlanetAtmosphere(0.5f, 2.0f));
	TestTrue(TEXT("Atmosphere shadows: a higher limit keeps M's"), ADayNightController::ShouldShadowPlanetAtmosphere(M, 8.0f));
	return true;
}

// ---------------------------------------------------------------------------
// The modifier slot (the weather plan's D61): identity leaves the lights as the curves set them; a filled slot scales the
// sun, the sky light and the fog, tints the fog, drives the clouds' material through a dynamic instance and flashes the
// sky light; cleared, the authored cloud values come back.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldClockModifiersTest, "WorldClock.Lighting.Modifiers", WORLDCLOCK_TEST_FLAGS)
bool FWorldClockModifiersTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("WorldClockModifiersTest"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	UWorldClockSubsystem* Clock = World->GetSubsystem<UWorldClockSubsystem>();
	ADirectionalLight* SunActor = World->SpawnActor<ADirectionalLight>();
	UDirectionalLightComponent* Sun = SunActor ? Cast<UDirectionalLightComponent>(SunActor->GetLightComponent()) : nullptr;
	ASkyLight* SkyLightActor = World->SpawnActor<ASkyLight>();
	USkyLightComponent* Sky = SkyLightActor ? SkyLightActor->GetLightComponent() : nullptr;
	AExponentialHeightFog* FogActor = World->SpawnActor<AExponentialHeightFog>();
	UExponentialHeightFogComponent* Fog = FogActor ? FogActor->GetComponent() : nullptr;
	AVolumetricCloud* CloudActor = World->SpawnActor<AVolumetricCloud>();
	UVolumetricCloudComponent* Cloud = CloudActor ? CloudActor->FindComponentByClass<UVolumetricCloudComponent>() : nullptr;
	ADayNightController* Controller = World->SpawnActor<ADayNightController>();
	if (TestNotNull(TEXT("A clock"), Clock) && TestNotNull(TEXT("A sun"), Sun) && TestNotNull(TEXT("A sky light"), Sky) && TestNotNull(TEXT("A fog"), Fog)
		&& TestNotNull(TEXT("A cloud layer"), Cloud) && TestNotNull(TEXT("A controller"), Controller))
	{
		Clock->SetTimeOfDay(12.0f); // noon: every daylight curve at 1
		Sun->SetIntensity(6.0f);
		Sky->SetIntensity(1.5f);
		Fog->SetFogDensity(0.02f);
		Controller->SunLight = SunActor;
		Controller->SkyLight = SkyLightActor;
		Controller->HeightFog = FogActor;
		Controller->CloudLayers.Add(Cloud);
		UMaterialInterface* AuthoredMaterial = Cloud->GetMaterial();

		TestTrue(TEXT("The slot is identity by default"), Controller->GetModifiers().IsIdentity());
		Controller->Tick(0.0f); // captures the authored values on the first apply
		Controller->Tick(0.0f);
		const float SunNoon = Sun->Intensity;
		const float SkyNoon = Sky->Intensity;
		const float FogNoon = Fog->FogDensity;
		const FLinearColor FogColourNoon = Fog->FogInscatteringLuminance;
		AddInfo(FString::Printf(TEXT("Noon, identity: sun %.3f sky %.3f fog %.4f"), SunNoon, SkyNoon, FogNoon));
		TestTrue(TEXT("Identity: the sun as the curve sets it"), FMath::IsNearlyEqual(SunNoon, 6.0f * Controller->SunIntensityByDaylight.GetRichCurveConst()->Eval(1.0f), 1.0e-4f));
		TestTrue(TEXT("Identity: the cloud material untouched"), Cloud->GetMaterial() == AuthoredMaterial && Controller->GetCloudMaterialCount() == 0);

		FDayNightModifiers M;
		M.SunIntensityScale = 0.3f;
		M.SkyLightIntensityScale = 0.6f;
		M.FogDensityScale = 4.0f;
		M.FogTint = FLinearColor(0.5f, 0.5f, 0.5f);
		M.CloudCoverage = 0.9f;
		M.CloudDensity = 0.8f;
		TestFalse(TEXT("A filled slot is not identity"), M.IsIdentity());
		Controller->SetModifiers(M);
		Controller->Tick(0.0f);
		TestTrue(TEXT("The sun scaled"), FMath::IsNearlyEqual(Sun->Intensity, SunNoon * 0.3f, 1.0e-4f));
		TestTrue(TEXT("The sky light scaled"), FMath::IsNearlyEqual(Sky->Intensity, SkyNoon * 0.6f, 1.0e-4f));
		TestTrue(TEXT("The fog density scaled"), FMath::IsNearlyEqual(Fog->FogDensity, FogNoon * 4.0f, 1.0e-6f));
		TestTrue(TEXT("The fog colour tinted"), Fog->FogInscatteringLuminance.Equals(FogColourNoon * M.FogTint, 1.0e-4f)); // the tint keeps the alpha
		UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Cloud->GetMaterial());
		if (AuthoredMaterial)
		{
			if (TestNotNull(TEXT("The cloud layer has a dynamic material now"), MID))
			{
				TestEqual(TEXT("One cloud material slot"), Controller->GetCloudMaterialCount(), 1);
				float Coverage = -1.0f, Density = -1.0f;
				const bool bCoverage = MID->GetScalarParameterValue(FMaterialParameterInfo(Controller->CloudCoverageParameter), Coverage);
				const bool bDensity = MID->GetScalarParameterValue(FMaterialParameterInfo(Controller->CloudDensityParameter), Density);
				AddInfo(FString::Printf(TEXT("Cloud material %s: %s %s%.2f, %s %s%.2f"), *AuthoredMaterial->GetName(), *Controller->CloudCoverageParameter.ToString(),
					bCoverage ? TEXT("") : TEXT("(absent) "), Coverage, *Controller->CloudDensityParameter.ToString(), bDensity ? TEXT("") : TEXT("(absent) "), Density));
				if (bCoverage)
				{
					TestTrue(TEXT("The coverage written"), FMath::IsNearlyEqual(Coverage, 0.9f, 1.0e-5f));
				}
				if (bDensity)
				{
					TestTrue(TEXT("The density written"), FMath::IsNearlyEqual(Density, 0.8f, 1.0e-5f));
				}
			}
		}
		else
		{
			AddInfo(TEXT("The cloud layer has no material here (headless): the material path is not covered by this run"));
		}

		// A flash brightens the sky light on top of the scale; cleared, everything returns, the clouds to the authored values.
		M.LightningFlash = 1.0f;
		Controller->SetModifiers(M);
		Controller->Tick(0.0f);
		TestTrue(TEXT("A full flash brightens the sky light"), FMath::IsNearlyEqual(Sky->Intensity, SkyNoon * 0.6f * (1.0f + Controller->LightningFlashSkyLight), 1.0e-4f));
		Controller->SetModifiers(FDayNightModifiers());
		Controller->Tick(0.0f);
		TestTrue(TEXT("Cleared: the sun back"), FMath::IsNearlyEqual(Sun->Intensity, SunNoon, 1.0e-4f));
		TestTrue(TEXT("Cleared: the sky light back"), FMath::IsNearlyEqual(Sky->Intensity, SkyNoon, 1.0e-4f));
		TestTrue(TEXT("Cleared: the fog back"), FMath::IsNearlyEqual(Fog->FogDensity, FogNoon, 1.0e-6f) && Fog->FogInscatteringLuminance.Equals(FogColourNoon, 1.0e-4f));
		if (MID)
		{
			float Coverage = -1.0f;
			if (MID->GetScalarParameterValue(FMaterialParameterInfo(Controller->CloudCoverageParameter), Coverage))
			{
				float Authored = -1.0f;
				AuthoredMaterial->GetScalarParameterValue(FMaterialParameterInfo(Controller->CloudCoverageParameter), Authored);
				TestTrue(TEXT("Cleared: the clouds' authored coverage back"), FMath::IsNearlyEqual(Coverage, Authored, 1.0e-5f));
			}
		}
	}
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#undef WORLDCLOCK_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
