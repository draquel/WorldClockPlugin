// Copyright Daniel Raquel. All Rights Reserved.

#include "WorldClockMath.h"
#include "WorldClockTags.h"

double UWorldClockMath::WrapTimeOfDay(double Seconds, int32& InOutDay)
{
	using namespace WorldClockConstants;
	while (Seconds >= SecondsPerDay)
	{
		Seconds -= SecondsPerDay;
		++InOutDay;
	}
	while (Seconds < 0.0)
	{
		Seconds += SecondsPerDay;
		--InOutDay;
	}
	return Seconds;
}

FText UWorldClockMath::FormatTimeOfDay(double Seconds)
{
	int32 Day = 0;
	Seconds = WrapTimeOfDay(Seconds, Day);
	const int32 Hours = static_cast<int32>(Seconds / WorldClockConstants::SecondsPerHour);
	const int32 Minutes = static_cast<int32>((Seconds - Hours * WorldClockConstants::SecondsPerHour) / 60.0);
	return FText::FromString(FString::Printf(TEXT("%02d:%02d"), Hours, Minutes));
}

float UWorldClockMath::SunElevationForHour(float Hour, float SunriseHour, float SunsetHour)
{
	const float DayLength = FMath::Max(SunsetHour - SunriseHour, 0.01f);
	const float NightLength = FMath::Max(24.0f - DayLength, 0.01f);
	float Since = FMath::Fmod(Hour - SunriseHour + 48.0f, 24.0f); // hours since sunrise, 0 .. 24
	float ArcDegrees;
	if (Since < DayLength)
	{
		ArcDegrees = 180.0f * (Since / DayLength);           // 0 (sunrise) .. 180 (sunset)
	}
	else
	{
		ArcDegrees = 180.0f + 180.0f * ((Since - DayLength) / NightLength); // 180 .. 360
	}
	return FMath::Sin(FMath::DegreesToRadians(ArcDegrees)) * 90.0f;
}

FRotator UWorldClockMath::SunRotationForHour(float Hour, float SunriseHour, float SunsetHour, float SunAzimuthDegrees)
{
	const float DayLength = FMath::Max(SunsetHour - SunriseHour, 0.01f);
	const float NightLength = FMath::Max(24.0f - DayLength, 0.01f);
	const float Since = FMath::Fmod(Hour - SunriseHour + 48.0f, 24.0f);
	const float ArcDegrees = Since < DayLength
		? 180.0f * (Since / DayLength)
		: 180.0f + 180.0f * ((Since - DayLength) / NightLength);
	const float Elevation = FMath::Sin(FMath::DegreesToRadians(ArcDegrees)) * 90.0f;
	// Bearing of the sun sweeps from the sunrise azimuth to its opposite; the light travels the other way.
	const float Bearing = SunAzimuthDegrees + ArcDegrees;
	return FRotator(-Elevation, Bearing + 180.0f, 0.0f);
}

EWorldClockPhase UWorldClockMath::PhaseForHour(float Hour, float SunriseHour, float SunsetHour, float TwilightHours)
{
	const float H = FMath::Fmod(Hour + 24.0f, 24.0f);
	auto InWindow = [&](float Start, float End) -> bool
	{
		// Windows may wrap past midnight (dawn at 23:30 for a 0:30 sunrise).
		const float S = FMath::Fmod(Start + 24.0f, 24.0f);
		const float E = FMath::Fmod(End + 24.0f, 24.0f);
		return S <= E ? (H >= S && H < E) : (H >= S || H < E);
	};
	if (InWindow(SunriseHour, SunsetHour))
	{
		return EWorldClockPhase::Day;
	}
	if (TwilightHours > 0.0f && InWindow(SunriseHour - TwilightHours, SunriseHour))
	{
		return EWorldClockPhase::Dawn;
	}
	if (TwilightHours > 0.0f && InWindow(SunsetHour, SunsetHour + TwilightHours))
	{
		return EWorldClockPhase::Dusk;
	}
	return EWorldClockPhase::Night;
}

float UWorldClockMath::DaylightForHour(float Hour, float SunriseHour, float SunsetHour, float TwilightHours)
{
	// Measured from sunrise so the day / night halves never alias on the 24-hour circle (a 12-hour
	// night puts sunrise exactly opposite sunset, where a symmetric "nearest edge" test is ambiguous).
	const float W = FMath::Max(TwilightHours, 0.01f);
	const float DayLength = FMath::Fmod(SunsetHour - SunriseHour + 48.0f, 24.0f);
	const float Since = FMath::Fmod(Hour - SunriseHour + 48.0f, 24.0f); // hours since sunrise, 0 .. 24
	float Daylight;
	if (Since < DayLength)
	{
		// Day: the sunrise ramp finishes W after sunrise; the sunset ramp starts W before sunset.
		const float Rise = FMath::Clamp((Since + W) / (2.0f * W), 0.0f, 1.0f);
		const float Set = FMath::Clamp((Since - DayLength + W) / (2.0f * W), 0.0f, 1.0f);
		Daylight = Rise * (1.0f - Set);
	}
	else
	{
		// Night: the sunset ramp keeps fading for W after sunset; the sunrise ramp starts W before sunrise.
		const float SinceSunset = Since - DayLength;
		const float UntilSunrise = 24.0f - Since;
		const float Fading = 1.0f - FMath::Clamp((SinceSunset + W) / (2.0f * W), 0.0f, 1.0f);
		const float Rising = FMath::Clamp((W - UntilSunrise) / (2.0f * W), 0.0f, 1.0f);
		Daylight = FMath::Clamp(Fading + Rising, 0.0f, 1.0f);
	}
	return FMath::SmoothStep(0.0f, 1.0f, Daylight);
}

double UWorldClockMath::Extrapolate(double SyncTimeOfDay, int32 SyncDay, double SyncServerTime, float TimeScale, double ServerNow, int32& OutDay)
{
	OutDay = SyncDay;
	const double Elapsed = FMath::Max(ServerNow - SyncServerTime, 0.0);
	return WrapTimeOfDay(SyncTimeOfDay + Elapsed * TimeScale, OutDay);
}

float UWorldClockMath::LocalHourOffset(const FVector& Direction)
{
	if (FMath::IsNearlyZero(Direction.X) && FMath::IsNearlyZero(Direction.Y))
	{
		return 0.0f; // a pole: every longitude, read as 0
	}
	return static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X)) / 15.0);
}

float UWorldClockMath::WrapHour(float Hour)
{
	const float Wrapped = FMath::Fmod(Hour, 24.0f);
	return Wrapped < 0.0f ? Wrapped + 24.0f : Wrapped;
}

FVector UWorldClockMath::PlanetSunDirection(float Hour)
{
	const double Longitude = FMath::DegreesToRadians((12.0 - static_cast<double>(Hour)) * 15.0);
	return FVector(FMath::Cos(Longitude), FMath::Sin(Longitude), 0.0);
}

float UWorldClockMath::DaylightForElevation(float ElevationDegrees, float SunriseHour, float SunsetHour, float TwilightHours)
{
	const float DayLength = FMath::Max(FMath::Fmod(SunsetHour - SunriseHour + 48.0f, 24.0f), 0.01f);
	const float NightLength = FMath::Max(24.0f - DayLength, 0.01f);
	const float E = FMath::Clamp(ElevationDegrees, -90.0f, 90.0f);
	const float Hour = SunriseHour + E / 90.0f * 0.5f * (E >= 0.0f ? DayLength : NightLength);
	return DaylightForHour(WrapHour(Hour), SunriseHour, SunsetHour, TwilightHours);
}

FGameplayTag UWorldClockMath::PhaseTag(EWorldClockPhase Phase)
{
	switch (Phase)
	{
	case EWorldClockPhase::Dawn: return WorldClockTags::Time_Phase_Dawn;
	case EWorldClockPhase::Day: return WorldClockTags::Time_Phase_Day;
	case EWorldClockPhase::Dusk: return WorldClockTags::Time_Phase_Dusk;
	default: return WorldClockTags::Time_Phase_Night;
	}
}
