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
	const float H = FMath::Fmod(Hour + 24.0f, 24.0f);
	const float W = FMath::Max(TwilightHours, 0.01f);
	// Signed hours from the nearest of sunrise / sunset, taking midnight wrap into account.
	auto Delta = [](float A, float B) -> float
	{
		float D = FMath::Fmod(A - B + 36.0f, 24.0f) - 12.0f; // -12 .. 12
		return D;
	};
	const float Rise = FMath::Clamp((Delta(H, SunriseHour) + W) / (2.0f * W), 0.0f, 1.0f); // 0 before dawn .. 1 after
	const float Set = FMath::Clamp((Delta(H, SunsetHour) + W) / (2.0f * W), 0.0f, 1.0f);   // 0 before dusk .. 1 after
	const float Daylight = Rise * (1.0f - Set);
	return FMath::SmoothStep(0.0f, 1.0f, Daylight);
}

double UWorldClockMath::Extrapolate(double SyncTimeOfDay, int32 SyncDay, double SyncServerTime, float TimeScale, double ServerNow, int32& OutDay)
{
	OutDay = SyncDay;
	const double Elapsed = FMath::Max(ServerNow - SyncServerTime, 0.0);
	return WrapTimeOfDay(SyncTimeOfDay + Elapsed * TimeScale, OutDay);
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
