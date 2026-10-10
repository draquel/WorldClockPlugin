// Copyright Daniel Raquel. All Rights Reserved.

#include "WorldClockState.h"
#include "Net/UnrealNetwork.h"

AWorldClockState::AWorldClockState()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(2.0f);
}

void AWorldClockState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWorldClockState, SyncTimeOfDay);
	DOREPLIFETIME(AWorldClockState, SyncDay);
	DOREPLIFETIME(AWorldClockState, TimeScale);
	DOREPLIFETIME(AWorldClockState, SyncServerTime);
}

void AWorldClockState::Publish(double InTimeOfDay, int32 InDay, float InTimeScale, double InServerTime)
{
	SyncTimeOfDay = InTimeOfDay;
	SyncDay = InDay;
	TimeScale = InTimeScale;
	SyncServerTime = InServerTime;
	ForceNetUpdate();
}
