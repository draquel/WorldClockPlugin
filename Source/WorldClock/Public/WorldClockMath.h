// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "WorldClockTypes.h"
#include "WorldClockMath.generated.h"

/**
 * Pure clock arithmetic shared by the subsystem, the lighting controller, tests and Blueprints:
 * hours to sun rotation, phases, daylight, client extrapolation. No state, no world.
 */
UCLASS()
class WORLDCLOCK_API UWorldClockMath : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Wrap seconds into 0 .. 86400 and count the days crossed (negative deltas wrap backwards). */
	static double WrapTimeOfDay(double Seconds, int32& InOutDay);

	/** Hours (0-24) from seconds-of-day. */
	UFUNCTION(BlueprintPure, Category = "WorldClock|Math")
	static float SecondsToHours(double Seconds) { return static_cast<float>(Seconds / WorldClockConstants::SecondsPerHour); }

	/** "HH:MM" for a time of day. */
	UFUNCTION(BlueprintPure, Category = "WorldClock|Math")
	static FText FormatTimeOfDay(double Seconds);

	/**
	 * Where the sun is. The sun rises at SunriseHour on the SunAzimuth bearing, peaks straight overhead
	 * halfway to SunsetHour and sets opposite; the night half of the arc continues below the horizon.
	 * @return Rotation for a directional light (its forward = the direction light travels).
	 */
	UFUNCTION(BlueprintPure, Category = "WorldClock|Math")
	static FRotator SunRotationForHour(float Hour, float SunriseHour, float SunsetHour, float SunAzimuthDegrees);

	/** Sun elevation in degrees (-90 .. 90) for the same arc. */
	UFUNCTION(BlueprintPure, Category = "WorldClock|Math")
	static float SunElevationForHour(float Hour, float SunriseHour, float SunsetHour);

	/** Dawn = [sunrise - twilight, sunrise), Day = [sunrise, sunset), Dusk = [sunset, sunset + twilight), else Night. */
	UFUNCTION(BlueprintPure, Category = "WorldClock|Math")
	static EWorldClockPhase PhaseForHour(float Hour, float SunriseHour, float SunsetHour, float TwilightHours);

	/** 0 at night, 1 by day, smooth ramps of TwilightHours width centred on sunrise and sunset. */
	UFUNCTION(BlueprintPure, Category = "WorldClock|Math")
	static float DaylightForHour(float Hour, float SunriseHour, float SunsetHour, float TwilightHours);

	/**
	 * A client's clock from the last sync point.
	 * @param SyncTimeOfDay   Seconds-of-day the server published.
	 * @param SyncDay         Day the server published.
	 * @param SyncServerTime  Server world time at the publish.
	 * @param TimeScale       Game seconds per real second.
	 * @param ServerNow       Current server world time (replicated by the game state).
	 * @param OutDay          Day after wrapping.
	 * @return Seconds-of-day now.
	 */
	static double Extrapolate(double SyncTimeOfDay, int32 SyncDay, double SyncServerTime, float TimeScale, double ServerNow, int32& OutDay);

	/** The Time.Phase.* tag for a phase. */
	UFUNCTION(BlueprintPure, Category = "WorldClock|Math")
	static FGameplayTag PhaseTag(EWorldClockPhase Phase);
};
