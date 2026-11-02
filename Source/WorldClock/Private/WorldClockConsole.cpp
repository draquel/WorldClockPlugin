// Copyright Daniel Raquel. All Rights Reserved.

// The world clock's console commands (code review CR4, FW-30: a plugin registers its own diagnostics): wc.Time, wc.TimeScale,
// wc.Clock. The game module's vox.Time, vox.TimeScale and vox.Clock run these.

#include "WorldClock.h"
#include "WorldClockMath.h"
#include "WorldClockSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

#if !UE_BUILD_SHIPPING

namespace WorldClockConsole
{
	/** The PIE or Game world: the console's own when it is one, else the first PIE/Game world context. */
	UWorld* FindPlayWorld(UWorld* World)
	{
		if (World && (World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Game))
		{
			return World;
		}
		if (GEngine)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if (Ctx.World() && (Ctx.WorldType == EWorldType::PIE || Ctx.WorldType == EWorldType::Game))
				{
					return Ctx.World();
				}
			}
		}
		return nullptr;
	}

	UWorldClockSubsystem* FindClock(UWorld* World)
	{
		UWorldClockSubsystem* Clock = World ? World->GetSubsystem<UWorldClockSubsystem>() : nullptr;
		if (!Clock)
		{
			UE_LOG(LogWorldClock, Warning, TEXT("wc.Time/TimeScale/Clock: no PIE/Game world clock."));
		}
		return Clock;
	}

	const APawn* FindPawn(UWorld* World)
	{
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		return PC ? PC->GetPawn() : nullptr;
	}

	void Time(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = FindPlayWorld(InWorld);
		UWorldClockSubsystem* Clock = FindClock(World);
		if (!Clock || Args.Num() < 1)
		{
			UE_LOG(LogWorldClock, Display, TEXT("wc.Time: usage wc.Time HH[:MM]"));
			return;
		}
		FString HourText, MinuteText;
		float Hours = 0.f;
		if (Args[0].Split(TEXT(":"), &HourText, &MinuteText))
		{
			Hours = FCString::Atof(*HourText) + FCString::Atof(*MinuteText) / 60.f;
		}
		else
		{
			Hours = FCString::Atof(*Args[0]);
		}
		// On a planet the hour is the local solar hour where the pawn stands (spherical Phase 4, D12).
		const APawn* Pawn = FindPawn(World);
		if (Clock->HasPlanetFrame() && Pawn)
		{
			Clock->SetLocalTimeOfDay(Pawn->GetActorLocation(), Hours);
			UE_LOG(LogWorldClock, Display, TEXT("wc.Time: local %s (%s) where the pawn stands; clock %s day %d."),
				*Clock->GetLocalTimeText(Pawn->GetActorLocation()).ToString(), *UEnum::GetValueAsString(Clock->GetLocalPhase(Pawn->GetActorLocation())),
				*Clock->GetTimeText().ToString(), Clock->GetDay());
			return;
		}
		Clock->SetTimeOfDay(Hours);
		UE_LOG(LogWorldClock, Display, TEXT("wc.Time: now %s (%s)."), *Clock->GetTimeText().ToString(), *UEnum::GetValueAsString(Clock->GetPhase()));
	}

	void TimeScale(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorldClockSubsystem* Clock = FindClock(FindPlayWorld(InWorld));
		if (!Clock || Args.Num() < 1)
		{
			UE_LOG(LogWorldClock, Display, TEXT("wc.TimeScale: usage wc.TimeScale <gameSecondsPerRealSecond> (60 = 24-minute day, 0 = pause)"));
			return;
		}
		Clock->SetTimeScale(FCString::Atof(*Args[0]));
	}

	void Clock(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = FindPlayWorld(InWorld);
		UWorldClockSubsystem* Clock = FindClock(World);
		if (!Clock)
		{
			return;
		}
		UE_LOG(LogWorldClock, Display, TEXT("wc.Clock: day %d %s phase=%s daylight=%.2f scale=%.1f sun=%s"),
			Clock->GetDay(), *Clock->GetTimeText().ToString(), *UEnum::GetValueAsString(Clock->GetPhase()), Clock->GetDaylight(),
			Clock->GetTimeScale(), *Clock->GetSunRotation().ToCompactString());
		const APawn* Pawn = FindPawn(World);
		if (Clock->HasPlanetFrame() && Pawn)
		{
			// A planet: the local solar time, phase, light and sun elevation where the pawn stands.
			const FVector Here = Pawn->GetActorLocation();
			UE_LOG(LogWorldClock, Display, TEXT("wc.Clock: local day %d %s phase=%s daylight=%.2f sun elevation %.1f deg (offset %+.2f h); sun toward %s"),
				Clock->GetLocalDay(Here), *Clock->GetLocalTimeText(Here).ToString(), *UEnum::GetValueAsString(Clock->GetLocalPhase(Here)),
				Clock->GetLocalDaylight(Here), Clock->GetLocalSunElevation(Here), UWorldClockMath::LocalHourOffset(Here - Clock->GetPlanetCenter()),
				*Clock->GetSunDirection().ToCompactString());
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GWorldClockTimeCmd(
	TEXT("wc.Time"),
	TEXT("Set the world clock's hour: wc.Time HH[:MM] (authority; on a planet the local solar hour where the pawn stands)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { WorldClockConsole::Time(Args, World); }));

static FAutoConsoleCommandWithWorldAndArgs GWorldClockTimeScaleCmd(
	TEXT("wc.TimeScale"),
	TEXT("Game seconds per real second for the world clock: wc.TimeScale N (60 = 24-minute day, 0 = pause)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { WorldClockConsole::TimeScale(Args, World); }));

static FAutoConsoleCommandWithWorldAndArgs GWorldClockClockCmd(
	TEXT("wc.Clock"),
	TEXT("Print the world clock: day, time, phase, daylight, scale, sun rotation (on a planet the local time where the pawn stands)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { WorldClockConsole::Clock(Args, World); }));

#endif // !UE_BUILD_SHIPPING
