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
  Carries an unbound `UPostProcessComponent` (priority 100) applying `ExposureBiasByDaylight` (-3.5 stops at
  night) so auto-exposure cannot lift the moonlit scene back to day brightness (`bDriveExposure`).
  The subsystem auto-spawns one when the world has none (settings).
- `UWorldClockWidget`: code-built HUD text "Day N  HH:MM" + phase; the game's controller decides where it goes.
- **Planets** (VoxelWorlds spherical Phase 4, decision D12): `SetPlanetFrame(Centre, SeaRadius)` / `ClearPlanetFrame`
  (set by a game-side bridge; this plugin stays engine-only). With a frame the sun is world-fixed over the equator at
  longitude (12 − hour) × 15° (`UWorldClockMath::PlanetSunDirection`, no axial tilt) and the clock's hour is the solar
  hour at longitude 0. `GetLocalHour` / `GetLocalDay` / `GetLocalPhase` / `GetLocalDaylight` / `GetLocalSunElevation` /
  `GetLocalTimeText` at a position (the hour + longitude / 15, `LocalHourOffset`), `LocalToGlobal`,
  `SetLocalTimeOfDay`; without a frame every `Local*` is the global value. The controller's light curves read
  `DaylightForElevation` at the viewer (equal to `DaylightForHour` on the equator with sunrise 6 / sunset 18); it
  centres the SkyAtmosphere on the planet (`PlanetCenterAtComponentTransform`, bottom radius the sea radius in km) and
  hides the height fog and actors tagged `WorldClock.ZUpSky`. The sky light follows the camera on a planet
  (`bPlanetSkyLightAtViewer`, `wc.Planet.SkyLightAtViewer`; its real-time capture is taken where it stands, and a map's
  sky light at the planet's centre captured nothing), with its lower hemisphere from the captured sky. The widget shows
  the local day, time and phase.
- `FWorldClockSaveState` + `ExportState` / `ImportState` for a save system.

## Rules
- Never read `TimeOfDaySeconds` directly on clients — always `GetTimeOfDaySeconds()` (extrapolated).
- Lighting changes go through the controller's curves / colours, not scattered `SetIntensity` calls.
- Keep this plugin free of gameplay: darkness penalties, torches, schedules belong to consumers that bind
  the delegates or read `GetDaylight()` / `GetPhaseTag()`.
- Consumers that act at a place (spawners, sleeping, the HUD) ask the `Local*` functions with that place: on a planet
  the global hour is longitude 0's.
- Tests: `WorldClock.*` (EditorContext | EngineFilter), pure, no world.
