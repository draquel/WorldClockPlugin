// Copyright Daniel Raquel. All Rights Reserved.

#include "WorldClockSubsystem.h"
#include "DayNightController.h"
#include "WorldClock.h"
#include "WorldClockMath.h"
#include "WorldClockSettings.h"
#include "WorldClockState.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"

bool UWorldClockSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

UWorldClockSubsystem* UWorldClockSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UWorldClockSubsystem>() : nullptr;
}

bool UWorldClockSubsystem::HasAuthority() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

double UWorldClockSubsystem::GetServerNow() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	return GameState ? GameState->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.0);
}

void UWorldClockSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	const UWorldClockSettings* Settings = UWorldClockSettings::Get();
	TimeScale = Settings->GetDefaultTimeScale();
	TimeOfDaySeconds = FMath::Clamp(Settings->StartHour, 0.0f, 24.0f) * WorldClockConstants::SecondsPerHour;
	Day = 1;

	if (HasAuthority())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		State = InWorld.SpawnActor<AWorldClockState>(AWorldClockState::StaticClass(), FTransform::Identity, Params);
		PublishSync();

		if (Settings->bAutoSpawnController)
		{
			bool bHasController = false;
			for (TActorIterator<ADayNightController> It(&InWorld); It; ++It)
			{
				bHasController = true;
				break;
			}
			if (!bHasController)
			{
				SpawnedController = InWorld.SpawnActor<ADayNightController>(ADayNightController::StaticClass(), FTransform::Identity, Params);
				UE_LOG(LogWorldClock, Log, TEXT("WorldClock: spawned a DayNightController (none placed in %s)."), *InWorld.GetName());
			}
		}
	}
	UE_LOG(LogWorldClock, Log, TEXT("WorldClock: %s starts day %d at %s, scale %.1f (%s)."), *InWorld.GetName(), Day,
		*GetTimeText().ToString(), TimeScale, HasAuthority() ? TEXT("authority") : TEXT("client"));
}

void UWorldClockSubsystem::Deinitialize()
{
	State = nullptr;
	SpawnedController = nullptr;
	Super::Deinitialize();
}

bool UWorldClockSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World && World->HasBegunPlay();
}

TStatId UWorldClockSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UWorldClockSubsystem, STATGROUP_Tickables);
}

void UWorldClockSubsystem::Tick(float DeltaTime)
{
	if (HasAuthority())
	{
		TimeOfDaySeconds = UWorldClockMath::WrapTimeOfDay(TimeOfDaySeconds + static_cast<double>(DeltaTime) * TimeScale, Day);
		const double Now = GetServerNow();
		if (State && (LastSyncServerTime < 0.0 || Now - LastSyncServerTime >= UWorldClockSettings::Get()->SyncIntervalSeconds))
		{
			PublishSync();
		}
	}
	FireEdgeEvents();
}

void UWorldClockSubsystem::PublishSync()
{
	if (!State || !HasAuthority())
	{
		return;
	}
	LastSyncServerTime = GetServerNow();
	State->Publish(TimeOfDaySeconds, Day, TimeScale, LastSyncServerTime);
}

double UWorldClockSubsystem::GetTimeOfDaySeconds() const
{
	if (HasAuthority())
	{
		return TimeOfDaySeconds;
	}
	// Client: extrapolate from the last sync point; before the first sync, the settings' start time.
	if (!State)
	{
		const UWorld* World = GetWorld();
		if (World)
		{
			for (TActorIterator<AWorldClockState> It(World); It; ++It)
			{
				const_cast<UWorldClockSubsystem*>(this)->State = *It;
				break;
			}
		}
	}
	if (State && State->HasSync())
	{
		int32 UnusedDay = 0;
		return UWorldClockMath::Extrapolate(State->GetSyncTimeOfDay(), State->GetSyncDay(), State->GetSyncServerTime(),
			State->GetTimeScale(), GetServerNow(), UnusedDay);
	}
	return TimeOfDaySeconds;
}

int32 UWorldClockSubsystem::GetDay() const
{
	if (HasAuthority() || !State || !State->HasSync())
	{
		return Day;
	}
	int32 OutDay = 0;
	UWorldClockMath::Extrapolate(State->GetSyncTimeOfDay(), State->GetSyncDay(), State->GetSyncServerTime(),
		State->GetTimeScale(), GetServerNow(), OutDay);
	return OutDay;
}

float UWorldClockSubsystem::GetHour() const
{
	return UWorldClockMath::SecondsToHours(GetTimeOfDaySeconds());
}

EWorldClockPhase UWorldClockSubsystem::GetPhase() const
{
	const UWorldClockSettings* S = UWorldClockSettings::Get();
	return UWorldClockMath::PhaseForHour(GetHour(), S->SunriseHour, S->SunsetHour, S->TwilightHours);
}

FGameplayTag UWorldClockSubsystem::GetPhaseTag() const
{
	return UWorldClockMath::PhaseTag(GetPhase());
}

float UWorldClockSubsystem::GetDaylight() const
{
	const UWorldClockSettings* S = UWorldClockSettings::Get();
	return UWorldClockMath::DaylightForHour(GetHour(), S->SunriseHour, S->SunsetHour, S->TwilightHours);
}

FRotator UWorldClockSubsystem::GetSunRotation() const
{
	if (bPlanetFrame)
	{
		// The light travels away from the sun.
		return FRotationMatrix::MakeFromX(-UWorldClockMath::PlanetSunDirection(GetHour())).Rotator();
	}
	const UWorldClockSettings* S = UWorldClockSettings::Get();
	return UWorldClockMath::SunRotationForHour(GetHour(), S->SunriseHour, S->SunsetHour, S->SunAzimuthDegrees);
}

void UWorldClockSubsystem::SetPlanetFrame(const FVector& Center, float SeaRadius, float TerrainTop)
{
	bPlanetFrame = true;
	PlanetCenter = Center;
	PlanetSeaRadius = SeaRadius;
	PlanetTerrainTop = FMath::Max(TerrainTop, 0.0f);
	UE_LOG(LogWorldClock, Log, TEXT("WorldClock: planet frame at %s (sea radius %.0f, terrain up to %.0f over it): a world-fixed sun, local solar time by longitude."),
		*Center.ToCompactString(), SeaRadius, PlanetTerrainTop);
}

void UWorldClockSubsystem::ClearPlanetFrame()
{
	bPlanetFrame = false;
}

float UWorldClockSubsystem::GetLocalHour(const FVector& WorldPos) const
{
	if (!bPlanetFrame)
	{
		return GetHour();
	}
	return UWorldClockMath::WrapHour(GetHour() + UWorldClockMath::LocalHourOffset(WorldPos - PlanetCenter));
}

int32 UWorldClockSubsystem::GetLocalDay(const FVector& WorldPos) const
{
	if (!bPlanetFrame)
	{
		return GetDay();
	}
	const double Total = static_cast<double>(GetDay()) * 24.0 + GetHour() + UWorldClockMath::LocalHourOffset(WorldPos - PlanetCenter);
	return FMath::FloorToInt32(Total / 24.0);
}

EWorldClockPhase UWorldClockSubsystem::GetLocalPhase(const FVector& WorldPos) const
{
	if (!bPlanetFrame)
	{
		return GetPhase();
	}
	const UWorldClockSettings* S = UWorldClockSettings::Get();
	return UWorldClockMath::PhaseForHour(GetLocalHour(WorldPos), S->SunriseHour, S->SunsetHour, S->TwilightHours);
}

float UWorldClockSubsystem::GetLocalSunElevation(const FVector& WorldPos) const
{
	if (!bPlanetFrame)
	{
		const UWorldClockSettings* S = UWorldClockSettings::Get();
		return UWorldClockMath::SunElevationForHour(GetHour(), S->SunriseHour, S->SunsetHour);
	}
	const FVector Up = (WorldPos - PlanetCenter).GetSafeNormal();
	return static_cast<float>(FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(FVector::DotProduct(Up, UWorldClockMath::PlanetSunDirection(GetHour())), -1.0, 1.0))));
}

float UWorldClockSubsystem::GetLocalDaylight(const FVector& WorldPos) const
{
	if (!bPlanetFrame)
	{
		return GetDaylight();
	}
	const UWorldClockSettings* S = UWorldClockSettings::Get();
	return UWorldClockMath::DaylightForElevation(GetLocalSunElevation(WorldPos), S->SunriseHour, S->SunsetHour, S->TwilightHours);
}

FText UWorldClockSubsystem::GetLocalTimeText(const FVector& WorldPos) const
{
	if (!bPlanetFrame)
	{
		return GetTimeText();
	}
	return UWorldClockMath::FormatTimeOfDay(static_cast<double>(GetLocalHour(WorldPos)) * WorldClockConstants::SecondsPerHour);
}

FVector UWorldClockSubsystem::GetSunDirection() const
{
	return bPlanetFrame ? UWorldClockMath::PlanetSunDirection(GetHour()) : -GetSunRotation().Vector();
}

void UWorldClockSubsystem::LocalToGlobal(const FVector& WorldPos, int32 LocalDay, float LocalHour, int32& OutDay, double& OutSeconds) const
{
	if (!bPlanetFrame)
	{
		OutDay = LocalDay;
		OutSeconds = static_cast<double>(LocalHour) * WorldClockConstants::SecondsPerHour;
		return;
	}
	const double Total = static_cast<double>(LocalDay) * 24.0 + LocalHour - UWorldClockMath::LocalHourOffset(WorldPos - PlanetCenter);
	OutDay = FMath::FloorToInt32(Total / 24.0);
	OutSeconds = FMath::Clamp((Total - OutDay * 24.0) * WorldClockConstants::SecondsPerHour, 0.0, WorldClockConstants::SecondsPerDay - 1.0e-3);
}

void UWorldClockSubsystem::SetLocalTimeOfDay(const FVector& WorldPos, float Hours)
{
	if (!bPlanetFrame)
	{
		SetTimeOfDay(Hours);
		return;
	}
	if (!HasAuthority())
	{
		UE_LOG(LogWorldClock, Warning, TEXT("SetLocalTimeOfDay(%.2f) called on a client — ignored."), Hours);
		return;
	}
	int32 NewDay = Day;
	double NewSeconds = 0.0;
	LocalToGlobal(WorldPos, GetLocalDay(WorldPos), FMath::Clamp(Hours, 0.0f, 24.0f - 1.0e-4f), NewDay, NewSeconds);
	Day = NewDay;
	TimeOfDaySeconds = NewSeconds;
	PublishSync();
	UE_LOG(LogWorldClock, Log, TEXT("WorldClock: local time at %s set to %s (clock %s, day %d)."), *WorldPos.ToCompactString(),
		*GetLocalTimeText(WorldPos).ToString(), *GetTimeText().ToString(), Day);
}

FText UWorldClockSubsystem::GetTimeText() const
{
	return UWorldClockMath::FormatTimeOfDay(GetTimeOfDaySeconds());
}

void UWorldClockSubsystem::SetTimeOfDay(float Hours)
{
	if (!HasAuthority())
	{
		UE_LOG(LogWorldClock, Warning, TEXT("SetTimeOfDay(%.2f) called on a client — ignored."), Hours);
		return;
	}
	TimeOfDaySeconds = FMath::Clamp(static_cast<double>(Hours), 0.0, 24.0) * WorldClockConstants::SecondsPerHour;
	if (TimeOfDaySeconds >= WorldClockConstants::SecondsPerDay)
	{
		TimeOfDaySeconds = 0.0;
	}
	PublishSync();
	UE_LOG(LogWorldClock, Log, TEXT("WorldClock: time set to %s (day %d)."), *GetTimeText().ToString(), Day);
}

void UWorldClockSubsystem::SetTimeScale(float InTimeScale)
{
	if (!HasAuthority())
	{
		UE_LOG(LogWorldClock, Warning, TEXT("SetTimeScale(%.1f) called on a client — ignored."), InTimeScale);
		return;
	}
	TimeScale = FMath::Max(InTimeScale, 0.0f);
	PublishSync();
	UE_LOG(LogWorldClock, Log, TEXT("WorldClock: time scale %.1f."), TimeScale);
}

FWorldClockSaveState UWorldClockSubsystem::ExportState() const
{
	FWorldClockSaveState Out;
	Out.Day = GetDay();
	Out.TimeOfDaySeconds = GetTimeOfDaySeconds();
	Out.TimeScale = TimeScale;
	return Out;
}

void UWorldClockSubsystem::ImportState(const FWorldClockSaveState& InState)
{
	if (!HasAuthority())
	{
		UE_LOG(LogWorldClock, Warning, TEXT("ImportState called on a client — ignored."));
		return;
	}
	Day = FMath::Max(InState.Day, 1);
	TimeOfDaySeconds = UWorldClockMath::WrapTimeOfDay(InState.TimeOfDaySeconds, Day);
	TimeScale = FMath::Max(InState.TimeScale, 0.0f);
	PublishSync();
	bEdgesPrimed = false; // re-prime so a loaded dusk does not fire a dawn->dusk cascade
}

void UWorldClockSubsystem::FireEdgeEvents()
{
	const int32 NowDay = GetDay();
	const int32 NowHour = FMath::Clamp(static_cast<int32>(GetHour()), 0, 23);
	const EWorldClockPhase NowPhase = GetPhase();
	if (!bEdgesPrimed)
	{
		LastDay = NowDay;
		LastHour = NowHour;
		LastPhase = NowPhase;
		bEdgesPrimed = true;
		return;
	}
	if (NowDay != LastDay)
	{
		LastDay = NowDay;
		OnNewDay.Broadcast(NowDay);
	}
	if (NowHour != LastHour)
	{
		LastHour = NowHour;
		OnHourChanged.Broadcast(NowHour);
	}
	if (NowPhase != LastPhase)
	{
		const EWorldClockPhase Old = LastPhase;
		LastPhase = NowPhase;
		UE_LOG(LogWorldClock, Log, TEXT("WorldClock: phase %s -> %s at %s."), *UEnum::GetValueAsString(Old), *UEnum::GetValueAsString(NowPhase), *GetTimeText().ToString());
		OnPhaseChanged.Broadcast(Old, NowPhase);
	}
}
