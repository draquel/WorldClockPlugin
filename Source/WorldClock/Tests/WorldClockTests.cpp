// Copyright Daniel Raquel. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "DayNightController.h"
#include "WorldClockMath.h"
#include "WorldClockTags.h"
#include "WorldClockTypes.h"

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
	TestEqual(TEXT("Noon light pitches straight down"), Noon.Pitch, -90.0f, 0.01f);
	const FRotator Rise = UWorldClockMath::SunRotationForHour(6.0f, 6.0f, 18.0f, 30.0f);
	const FRotator Set = UWorldClockMath::SunRotationForHour(18.0f, 6.0f, 18.0f, 30.0f);
	TestEqual(TEXT("Sunrise yaw = azimuth + 180"), FRotator::NormalizeAxis(Rise.Yaw), FRotator::NormalizeAxis(210.0f), 0.01f);
	TestEqual(TEXT("Sunset yaw = azimuth"), FRotator::NormalizeAxis(Set.Yaw), FRotator::NormalizeAxis(30.0f), 0.01f);
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

	TestEqual(TEXT("Phase tag"), UWorldClockMath::PhaseTag(EWorldClockPhase::Dusk), WorldClockTags::Time_Phase_Dusk);
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

#undef WORLDCLOCK_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
