// Copyright Daniel Raquel. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "DayNightController.h"
#include "WorldClockMath.h"
#include "WorldClockTags.h"
#include "WorldClockTypes.h"
#include "WorldClockSubsystem.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/Engine.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"

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
		ADayNightController* Controller = World->SpawnActor<ADayNightController>();
		USkyAtmosphereComponent* Sky = SkyActor ? SkyActor->FindComponentByClass<USkyAtmosphereComponent>() : nullptr;
		if (TestNotNull(TEXT("A clock"), Clock) && TestNotNull(TEXT("A sky atmosphere"), Sky) && TestNotNull(TEXT("A controller"), Controller)
			&& TestNotNull(TEXT("A cloud layer"), Cloud))
		{
			const float CloudBottomBefore = Cloud->LayerBottomAltitude;
			const float CloudHeightBefore = Cloud->LayerHeight;
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
				TestTrue(TEXT("Planet: the cloud layer's floor over the highest terrain"), Cloud->LayerBottomAltitude >= 0.065f - 1.0e-6f);
				TestTrue(TEXT("Planet: the cloud layer inside the atmosphere"), Cloud->LayerBottomAltitude + Cloud->LayerHeight <= Sky->AtmosphereHeight);

				// The sky light follows the viewer (visual polish VP1-O5): without one it stays put; with one it moves there
				// and takes its lower hemisphere from the captured sky; wc.Planet.SkyLightAtViewer 0 puts both back.
				if (TestNotNull(TEXT("A sky light"), SkyLightComp))
				{
					TestTrue(TEXT("Planet sky light: no viewer, no move"), SkyLightActor->GetActorLocation().Equals(Centre, 1.0) && !Controller->IsSkyLightAtViewer());
					const FVector Viewer = Centre + FVector(0.6, 0.0, 0.8) * 250180.0;
					Controller->ApplyPlanetSkyLight(&Viewer);
					TestTrue(TEXT("Planet sky light: at the viewer"), SkyLightActor->GetActorLocation().Equals(Viewer, 1.0) && Controller->IsSkyLightAtViewer());
					TestFalse(TEXT("Planet sky light: the lower hemisphere from the captured sky"), SkyLightComp->bLowerHemisphereIsBlack);
					IConsoleVariable* AtViewer = IConsoleManager::Get().FindConsoleVariable(TEXT("wc.Planet.SkyLightAtViewer"));
					if (TestNotNull(TEXT("wc.Planet.SkyLightAtViewer"), AtViewer))
					{
						AtViewer->Set(0, ECVF_SetByCode);
						Controller->ApplyPlanetSkyLight(&Viewer);
						TestTrue(TEXT("Planet sky light off: back where the map put it"), SkyLightActor->GetActorLocation().Equals(Centre, 1.0) && !Controller->IsSkyLightAtViewer());
						TestTrue(TEXT("Planet sky light off: its lower hemisphere black again"), SkyLightComp->bLowerHemisphereIsBlack);
						AtViewer->Set(1, ECVF_SetByCode);
					}
				}
			}
			else
			{
				TestTrue(TEXT("Plane: the atmosphere untouched"), Sky->TransformMode == ModeBefore && Sky->BottomRadius == BottomBefore);
				TestFalse(TEXT("Plane: the sky shown"), SkySphere->IsHidden());
				TestFalse(TEXT("Plane: the fog shown"), Fog->IsHidden());
				TestTrue(TEXT("Plane: the clouds untouched"), Cloud->LayerBottomAltitude == CloudBottomBefore && Cloud->LayerHeight == CloudHeightBefore);
				TestTrue(TEXT("Plane: the sky light untouched"), !SkyLightComp || (SkyLightActor->GetActorLocation().Equals(Centre, 1.0) && SkyLightComp->bLowerHemisphereIsBlack));
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

	// The cloud layer (the engine's 5 km floor, 10 km thick) with the sky: over the highest terrain, inside the air.
	float Bottom = 0.0f, Height = 0.0f;
	ADayNightController::ComputePlanetCloudLayer(5.0f, 10.0f, L, 8.45f, Bottom, Height);
	AddInfo(FString::Printf(TEXT("Clouds: L %.2f to %.2f km"), Bottom, Bottom + Height));
	TestTrue(TEXT("L clouds: the floor at the highest terrain"), FMath::IsNearlyEqual(Bottom, 8.45f, 1.0e-5f));
	TestTrue(TEXT("L clouds: inside the air"), Bottom + Height <= 60.0f * L);
	ADayNightController::ComputePlanetCloudLayer(5.0f, 10.0f, M, 2.43f, Bottom, Height);
	AddInfo(FString::Printf(TEXT("Clouds: M %.2f to %.2f km"), Bottom, Bottom + Height));
	TestTrue(TEXT("M clouds: the floor at the highest terrain"), FMath::IsNearlyEqual(Bottom, 2.43f, 1.0e-5f));
	TestTrue(TEXT("M clouds: the thickness scaled"), FMath::IsNearlyEqual(Height, 10.0f * M, 1.0e-5f));
	TestTrue(TEXT("M clouds: inside the air"), Bottom + Height <= 60.0f * M);
	ADayNightController::ComputePlanetCloudLayer(5.0f, 10.0f, Earthlike, 1.0f, Bottom, Height);
	TestTrue(TEXT("Earth-like clouds: the authored layer scaled"), FMath::IsNearlyEqual(Bottom, 5.0f * Earthlike, 1.0e-4f) && FMath::IsNearlyEqual(Height, 10.0f * Earthlike, 1.0e-4f));

	TestEqual(TEXT("Space exposure: none on the ground"), ADayNightController::ComputeSpaceExposureWeight(0.0f, 30.0f), 0.0f);
	TestEqual(TEXT("Space exposure: none in the lower half of the air"), ADayNightController::ComputeSpaceExposureWeight(15.0f, 30.0f), 0.0f);
	TestTrue(TEXT("Space exposure: partly at the top of the air"), FMath::IsNearlyEqual(ADayNightController::ComputeSpaceExposureWeight(30.0f, 30.0f), 0.5f, 1.0e-6f));
	TestEqual(TEXT("Space exposure: whole in space"), ADayNightController::ComputeSpaceExposureWeight(45.0f, 30.0f), 1.0f);
	TestEqual(TEXT("Space exposure: none without an atmosphere"), ADayNightController::ComputeSpaceExposureWeight(1000.0f, 0.0f), 0.0f);
	return true;
}

#undef WORLDCLOCK_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
