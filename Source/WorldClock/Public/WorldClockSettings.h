// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "WorldClockSettings.generated.h"

/**
 * World clock configuration (Project Settings > Plugins > World Clock; DefaultGame.ini
 * [/Script/WorldClock.WorldClockSettings]). Read once by the subsystem at world start.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "World Clock"))
class WORLDCLOCK_API UWorldClockSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UWorldClockSettings();

	/** Real minutes per game day. 24 = one real minute per game hour. */
	UPROPERTY(Config, EditAnywhere, Category = "Clock", meta = (ClampMin = "0.1"))
	float DayLengthMinutes = 24.0f;

	/** Hour the world starts at (0-24). */
	UPROPERTY(Config, EditAnywhere, Category = "Clock", meta = (ClampMin = "0", ClampMax = "24"))
	float StartHour = 8.0f;

	/** Sun on the horizon in the morning. */
	UPROPERTY(Config, EditAnywhere, Category = "Phases", meta = (ClampMin = "0", ClampMax = "24"))
	float SunriseHour = 6.0f;

	/** Sun on the horizon in the evening. */
	UPROPERTY(Config, EditAnywhere, Category = "Phases", meta = (ClampMin = "0", ClampMax = "24"))
	float SunsetHour = 18.0f;

	/** Width of dawn (before sunrise) and dusk (after sunset), hours. Also the daylight ramp width. */
	UPROPERTY(Config, EditAnywhere, Category = "Phases", meta = (ClampMin = "0", ClampMax = "6"))
	float TwilightHours = 1.0f;

	/** Compass bearing of sunrise (degrees, yaw). The sun travels bearing + 180 by sunset. */
	UPROPERTY(Config, EditAnywhere, Category = "Sun", meta = (ClampMin = "-360", ClampMax = "360"))
	float SunAzimuthDegrees = 0.0f;

	/** Spawn an ADayNightController in game worlds that have none (it finds the lights by class). */
	UPROPERTY(Config, EditAnywhere, Category = "Lighting")
	bool bAutoSpawnController = true;

	/** Seconds between sync-point republishes to clients (also republished on every setter). */
	UPROPERTY(Config, EditAnywhere, Category = "Replication", meta = (ClampMin = "0.5"))
	float SyncIntervalSeconds = 5.0f;

	/** Game seconds per real second implied by DayLengthMinutes. */
	float GetDefaultTimeScale() const;

	static const UWorldClockSettings* Get();
};
