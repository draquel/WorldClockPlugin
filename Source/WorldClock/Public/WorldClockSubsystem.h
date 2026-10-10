// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "WorldClockTypes.h"
#include "WorldClockSubsystem.generated.h"

class AWorldClockState;
class ADayNightController;

/**
 * The game clock (feature 6). Game worlds only. The authority advances seconds-of-day by TimeScale
 * and publishes sync points through AWorldClockState; clients extrapolate from the last sync point
 * and the game state's server time, so every machine reads the same hour. Phase / hour / day
 * delegates fire on every machine from that same value. Setters are authority-only.
 */
UCLASS()
class WORLDCLOCK_API UWorldClockSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// --- Subsystem ---
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

	/** Convenience: the clock of an object's world (null outside game worlds). */
	UFUNCTION(BlueprintPure, Category = "WorldClock", meta = (WorldContext = "WorldContextObject"))
	static UWorldClockSubsystem* Get(const UObject* WorldContextObject);

	// --- Reading ---

	/** Seconds into the current day (0 .. 86400), extrapolated on clients. */
	UFUNCTION(BlueprintPure, Category = "WorldClock")
	double GetTimeOfDaySeconds() const;

	/** Hours into the day (0 .. 24). */
	UFUNCTION(BlueprintPure, Category = "WorldClock")
	float GetHour() const;

	/** Days since the world started (day 1 = first). */
	UFUNCTION(BlueprintPure, Category = "WorldClock")
	int32 GetDay() const;

	UFUNCTION(BlueprintPure, Category = "WorldClock")
	float GetTimeScale() const { return TimeScale; }

	UFUNCTION(BlueprintPure, Category = "WorldClock")
	EWorldClockPhase GetPhase() const;

	/** Time.Phase.* for the current phase. */
	UFUNCTION(BlueprintPure, Category = "WorldClock")
	FGameplayTag GetPhaseTag() const;

	/** 0 at night, 1 by day (smooth twilight ramps). */
	UFUNCTION(BlueprintPure, Category = "WorldClock")
	float GetDaylight() const;

	/** Directional-light rotation for now (from the settings' sunrise / sunset / azimuth). */
	UFUNCTION(BlueprintPure, Category = "WorldClock")
	FRotator GetSunRotation() const;

	/** "HH:MM". */
	UFUNCTION(BlueprintPure, Category = "WorldClock")
	FText GetTimeText() const;

	// --- Writing (authority) ---

	/** Jump to an hour of the current day (0 .. 24). Authority only; logs a warning elsewhere. */
	UFUNCTION(BlueprintCallable, Category = "WorldClock")
	void SetTimeOfDay(float Hours);

	/** Game seconds per real second (0 pauses). Authority only. */
	UFUNCTION(BlueprintCallable, Category = "WorldClock")
	void SetTimeScale(float InTimeScale);

	// --- Save (feature 9) ---

	UFUNCTION(BlueprintCallable, Category = "WorldClock|Save")
	FWorldClockSaveState ExportState() const;

	/** Authority only. */
	UFUNCTION(BlueprintCallable, Category = "WorldClock|Save")
	void ImportState(const FWorldClockSaveState& State);

	// --- Events (every machine) ---

	UPROPERTY(BlueprintAssignable, Category = "WorldClock|Events")
	FOnWorldClockPhaseChanged OnPhaseChanged;

	UPROPERTY(BlueprintAssignable, Category = "WorldClock|Events")
	FOnWorldClockHourChanged OnHourChanged;

	UPROPERTY(BlueprintAssignable, Category = "WorldClock|Events")
	FOnWorldClockNewDay OnNewDay;

protected:
	bool HasAuthority() const;
	double GetServerNow() const;
	void PublishSync();
	void FireEdgeEvents();

	/** Authority-owned clock. */
	double TimeOfDaySeconds = 0.0;
	int32 Day = 1;
	float TimeScale = 60.0f;
	double LastSyncServerTime = -1.0;

	UPROPERTY()
	TObjectPtr<AWorldClockState> State;

	UPROPERTY()
	TObjectPtr<ADayNightController> SpawnedController;

	// Edge tracking for delegates (every machine).
	int32 LastHour = -1;
	int32 LastDay = -1;
	EWorldClockPhase LastPhase = EWorldClockPhase::Night;
	bool bEdgesPrimed = false;
};
