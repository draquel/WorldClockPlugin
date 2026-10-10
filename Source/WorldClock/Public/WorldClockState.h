// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "WorldClockState.generated.h"

/**
 * The clock's replication carrier: one always-relevant info actor the authority spawns, holding the
 * last sync point (seconds-of-day, day, server timestamp, time scale). Clients extrapolate from it
 * through UWorldClockSubsystem; nothing ticks or replicates per frame.
 */
UCLASS(NotBlueprintable)
class WORLDCLOCK_API AWorldClockState : public AInfo
{
	GENERATED_BODY()

public:
	AWorldClockState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Authority: publish a new sync point. */
	void Publish(double InTimeOfDay, int32 InDay, float InTimeScale, double InServerTime);

	double GetSyncTimeOfDay() const { return SyncTimeOfDay; }
	int32 GetSyncDay() const { return SyncDay; }
	float GetTimeScale() const { return TimeScale; }
	double GetSyncServerTime() const { return SyncServerTime; }
	bool HasSync() const { return SyncServerTime >= 0.0; }

protected:
	UPROPERTY(Replicated)
	double SyncTimeOfDay = 0.0;

	UPROPERTY(Replicated)
	int32 SyncDay = 1;

	UPROPERTY(Replicated)
	float TimeScale = 60.0f;

	/** Server world time (AGameStateBase::GetServerWorldTimeSeconds) when the sync point was taken; -1 = none yet. */
	UPROPERTY(Replicated)
	double SyncServerTime = -1.0;
};
