// Copyright Daniel Raquel. All Rights Reserved.

#include "WorldClock.h"

#define LOCTEXT_NAMESPACE "FWorldClockModule"

DEFINE_LOG_CATEGORY(LogWorldClock);

void FWorldClockModule::StartupModule()
{
	UE_LOG(LogWorldClock, Log, TEXT("WorldClock module started"));
}

void FWorldClockModule::ShutdownModule()
{
	UE_LOG(LogWorldClock, Log, TEXT("WorldClock module shutdown"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FWorldClockModule, WorldClock)
