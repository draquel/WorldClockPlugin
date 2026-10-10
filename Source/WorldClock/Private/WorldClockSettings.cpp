// Copyright Daniel Raquel. All Rights Reserved.

#include "WorldClockSettings.h"
#include "WorldClockTypes.h"

UWorldClockSettings::UWorldClockSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("World Clock");
}

float UWorldClockSettings::GetDefaultTimeScale() const
{
	const float RealSeconds = FMath::Max(DayLengthMinutes, 0.1f) * 60.0f;
	return static_cast<float>(WorldClockConstants::SecondsPerDay / RealSeconds);
}

const UWorldClockSettings* UWorldClockSettings::Get()
{
	return GetDefault<UWorldClockSettings>();
}
