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

	// --- Planets (spherical Phase 4, decision D12) ---
	// With a planet frame the sun is a fixed world direction turning about the planet's +Z axis once a day
	// (UWorldClockMath::PlanetSunDirection; no axial tilt), the clock's hour is the local solar hour at longitude 0, and a
	// place's local hour runs ahead by its longitude / 15. Without one every Local* function returns the global value.

	/**
	 * Give the clock a planet (a game-side bridge calls this once the voxel world reports a shell).
	 * @param Center Planet centre
	 * @param SeaRadius The sea's radius (the datum without water): the sky's ground
	 * @param TerrainTop The highest the terrain reaches over the sea radius (uu; 0 = unknown): the sky, scaled to the
	 *        planet (ADayNightController, spherical Phase 6), keeps its atmosphere well above it
	 */
	UFUNCTION(BlueprintCallable, Category = "WorldClock|Planet")
	void SetPlanetFrame(const FVector& Center, float SeaRadius, float TerrainTop = 0.0f);

	/** Back to the one-frame clock. */
	UFUNCTION(BlueprintCallable, Category = "WorldClock|Planet")
	void ClearPlanetFrame();

	UFUNCTION(BlueprintPure, Category = "WorldClock|Planet")
	bool HasPlanetFrame() const { return bPlanetFrame; }

	UFUNCTION(BlueprintPure, Category = "WorldClock|Planet")
	FVector GetPlanetCenter() const { return PlanetCenter; }

	UFUNCTION(BlueprintPure, Category = "WorldClock|Planet")
	float GetPlanetSeaRadius() const { return PlanetSeaRadius; }

	/** The highest the terrain reaches over the sea radius (uu; 0 = unknown). */
	UFUNCTION(BlueprintPure, Category = "WorldClock|Planet")
	float GetPlanetTerrainTop() const { return PlanetTerrainTop; }

	/** The local solar hour at a position (0 .. 24); GetHour without a planet frame. */
	UFUNCTION(BlueprintPure, Category = "WorldClock|Planet")
	float GetLocalHour(const FVector& WorldPos) const;

	/** The local day at a position (the day count where the local clock stands); GetDay without a planet frame. */
	UFUNCTION(BlueprintPure, Category = "WorldClock|Planet")
	int32 GetLocalDay(const FVector& WorldPos) const;

	/** The phase by the local hour (the clock's hour rules); GetPhase without a planet frame. */
	UFUNCTION(BlueprintPure, Category = "WorldClock|Planet")
	EWorldClockPhase GetLocalPhase(const FVector& WorldPos) const;

	/** Daylight from the sun's true elevation at a position (DaylightForElevation); GetDaylight without a planet frame. */
	UFUNCTION(BlueprintPure, Category = "WorldClock|Planet")
	float GetLocalDaylight(const FVector& WorldPos) const;

	/** The sun's elevation at a position (degrees); the clock's arc without a planet frame. */
	UFUNCTION(BlueprintPure, Category = "WorldClock|Planet")
	float GetLocalSunElevation(const FVector& WorldPos) const;

	/** "HH:MM" of the local hour. */
	UFUNCTION(BlueprintPure, Category = "WorldClock|Planet")
	FText GetLocalTimeText(const FVector& WorldPos) const;

	/** The unit direction toward the sun (minus the sun light's forward). */
	UFUNCTION(BlueprintPure, Category = "WorldClock|Planet")
	FVector GetSunDirection() const;

	/**
	 * The clock's day and seconds at which a position's local clock reads a local day and hour (identity without a planet
	 * frame).
	 * @param WorldPos Position
	 * @param LocalDay Local day
	 * @param LocalHour Local hour (0 .. 24)
	 * @param OutDay Clock day
	 * @param OutSeconds Clock seconds of day
	 */
	UFUNCTION(BlueprintPure, Category = "WorldClock|Planet")
	void LocalToGlobal(const FVector& WorldPos, int32 LocalDay, float LocalHour, int32& OutDay, double& OutSeconds) const;

	/** Jump so the local hour at a position is Hours (its local day kept); SetTimeOfDay without a planet frame. Authority only. */
	UFUNCTION(BlueprintCallable, Category = "WorldClock|Planet")
	void SetLocalTimeOfDay(const FVector& WorldPos, float Hours);

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

	// Planet frame (D12).
	bool bPlanetFrame = false;
	FVector PlanetCenter = FVector::ZeroVector;
	float PlanetSeaRadius = 0.0f;
	float PlanetTerrainTop = 0.0f;

	// Edge tracking for delegates (every machine).
	int32 LastHour = -1;
	int32 LastDay = -1;
	EWorldClockPhase LastPhase = EWorldClockPhase::Night;
	bool bEdgesPrimed = false;
};
