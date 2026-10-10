// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WorldClockTypes.generated.h"

/** The four phases of a day. Order matters for arithmetic (Dawn < Day < Dusk < Night). */
UENUM(BlueprintType)
enum class EWorldClockPhase : uint8
{
	Dawn,
	Day,
	Dusk,
	Night,
};

/** Everything a save system needs to restore the clock (feature 9). */
USTRUCT(BlueprintType)
struct WORLDCLOCK_API FWorldClockSaveState
{
	GENERATED_BODY()

	/** Days elapsed since the world started (day 1 = first day). */
	UPROPERTY(BlueprintReadWrite, Category = "WorldClock")
	int32 Day = 1;

	/** Seconds into the current day, 0 .. 86400. */
	UPROPERTY(BlueprintReadWrite, Category = "WorldClock")
	double TimeOfDaySeconds = 0.0;

	/** Game seconds per real second. */
	UPROPERTY(BlueprintReadWrite, Category = "WorldClock")
	float TimeScale = 60.0f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWorldClockPhaseChanged, EWorldClockPhase, OldPhase, EWorldClockPhase, NewPhase);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWorldClockHourChanged, int32, Hour);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWorldClockNewDay, int32, Day);

namespace WorldClockConstants
{
	constexpr double SecondsPerDay = 86400.0;
	constexpr double SecondsPerHour = 3600.0;
}
