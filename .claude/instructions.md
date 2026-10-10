# WorldClockPlugin — Claude instructions

## Plugin identity
- One runtime module `WorldClock`. Log category `LogWorldClock`. Depends on Core / CoreUObject / Engine /
  GameplayTags / UMG / DeveloperSettings (+ NetCore, Slate privately). **No game-framework plugin may be a
  dependency here**; consumers depend on this plugin, never the reverse.
- Tags: `Time.Phase.{Dawn,Day,Dusk,Night}` (native, `WorldClockTags.h`).

## Architecture
- `UWorldClockSettings` (developer settings, `[/Script/WorldClock.WorldClockSettings]`): `DayLengthMinutes` (24 =
  one real minute per game hour), `StartHour`, `SunriseHour`, `SunsetHour`, `TwilightHours`, `SunAzimuthDegrees`,
  `bAutoSpawnController`, `SyncIntervalSeconds`.
- `UWorldClockSubsystem` (tickable world subsystem, Game / PIE worlds): the authority owns `TimeOfDaySeconds`
  (0..86400), `Day`, `TimeScale` and advances them in `Tick`; it spawns one `AWorldClockState` (always-relevant
  info actor) and publishes a sync point every `SyncIntervalSeconds` and on every setter. Clients read
  `GetTimeOfDaySeconds()` by extrapolating from the sync point with `AGameStateBase::GetServerWorldTimeSeconds`
  (`UWorldClockMath::Extrapolate`). Delegates `OnPhaseChanged / OnHourChanged / OnNewDay` fire on every machine
  from that same value (`FireEdgeEvents`, primed on the first tick so a mid-day start fires nothing).
  Setters (`SetTimeOfDay`, `SetTimeScale`, `ImportState`) are authority-only and warn elsewhere.
- `UWorldClockMath`: pure arithmetic — sun arc (`SunElevationForHour`, `SunRotationForHour`: horizon at sunrise /
  sunset, overhead at the day's midpoint, the night half below), `PhaseForHour` (dawn before sunrise, dusk after
  sunset, windows may wrap midnight), `DaylightForHour` (0..1 smooth ramps of twilight width centred on sunrise /
  sunset), `WrapTimeOfDay`, `FormatTimeOfDay`. Tests live against these.
- `ADayNightController`: not replicated; every machine drives the lights from the clock each tick. Finds the
  first `ADirectionalLight` / `ASkyLight` / `AExponentialHeightFog` when its references are empty, captures their
  authored values at BeginPlay (noon reference) and applies curve multipliers by Daylight plus sun rotation /
  colour and fog colour; writes `TimeOfDay` / `Daylight` scalars to an optional `UMaterialParameterCollection`.
  The subsystem auto-spawns one when the world has none (settings).
- `UWorldClockWidget`: code-built HUD text "Day N  HH:MM" + phase; the game's controller decides where it goes.
- `FWorldClockSaveState` + `ExportState` / `ImportState` for a save system.

## Rules
- Never read `TimeOfDaySeconds` directly on clients — always `GetTimeOfDaySeconds()` (extrapolated).
- Lighting changes go through the controller's curves / colours, not scattered `SetIntensity` calls.
- Keep this plugin free of gameplay: darkness penalties, torches, schedules belong to consumers that bind
  the delegates or read `GetDaylight()` / `GetPhaseTag()`.
- Tests: `WorldClock.*` (EditorContext | EngineFilter), pure, no world.
