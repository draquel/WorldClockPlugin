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

	// --- Planets (spherical Phase 4, decision D12): a world-fixed sun over a planet turning once a day, no axial tilt ---

	/**
	 * How far a place's local solar hour runs ahead of the clock: its longitude / 15 (longitude from +X toward +Y about the
	 * planet's +Z axis, as vox.TPLatLon reads it).
	 * @param Direction Direction from the planet centre
	 * @return Hours (-12 .. 12)
	 */
	UFUNCTION(BlueprintPure, Category = "WorldClock|Math")
	static float LocalHourOffset(const FVector& Direction);

	/** An hour wrapped into 0 .. 24. */
	UFUNCTION(BlueprintPure, Category = "WorldClock|Math")
	static float WrapHour(float Hour);

	/**
	 * The direction toward the sun at a clock hour: over the equator at longitude (12 - Hour) * 15 degrees, so it is noon
	 * where the local hour is 12 and the sun sets where it is 18.
	 * @param Hour Clock (longitude 0) hour
	 * @return Unit direction toward the sun
	 */
	UFUNCTION(BlueprintPure, Category = "WorldClock|Math")
	static FVector PlanetSunDirection(float Hour);

	/**
	 * Daylight for a true sun elevation: DaylightForHour at the morning hour the clock's own arc reaches that elevation,
	 * sunrise + e / 90 * half the day (before sunrise: half the night). On a planet's equator the elevation runs 15
	 * degrees an hour, so with sunrise 6 and sunset 18 this equals the hour curve there; at the poles the sun stays on
	 * the horizon and so does the light.
	 * @param ElevationDegrees Sun elevation (-90 .. 90)
	 * @return Daylight (0 .. 1)
	 */
	UFUNCTION(BlueprintPure, Category = "WorldClock|Math")
	static float DaylightForElevation(float ElevationDegrees, float SunriseHour, float SunsetHour, float TwilightHours);
};
