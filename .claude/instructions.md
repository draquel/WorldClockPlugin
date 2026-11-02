# WorldClockPlugin — Claude instructions

> State as of 2026-11-02 (code review CR5). This file is the plugin's reference; its design record is
> `Documentation/WORLD_CLOCK_PLAN.md` (feature 6 and its planet sections, frozen).

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
- **Planets** (VoxelWorlds spherical Phase 4, decision D12): `SetPlanetFrame(Centre, SeaRadius, TerrainTop)` /
  `ClearPlanetFrame` (set by VoxelWorldPOI's `UPOIPlanetClockBridge`; this plugin stays engine-only). With a frame the sun is world-fixed over the equator at
  longitude (12 − hour) × 15° (`UWorldClockMath::PlanetSunDirection`, no axial tilt) and the clock's hour is the solar
  hour at longitude 0. `GetLocalHour` / `GetLocalDay` / `GetLocalPhase` / `GetLocalDaylight` / `GetLocalSunElevation` /
  `GetLocalTimeText` at a position (the hour + longitude / 15, `LocalHourOffset`), `LocalToGlobal`,
  `SetLocalTimeOfDay`; without a frame every `Local*` is the global value. The controller's light curves read
  `DaylightForElevation` at the viewer (equal to `DaylightForHour` on the equator with sunrise 6 / sunset 18); it
  centres the SkyAtmosphere on the planet (`PlanetCenterAtComponentTransform`, bottom radius the sea radius in km) and
  hides the height fog and actors tagged `WorldClock.ZUpSky`. The widget shows the local day, time and phase.
- **The planet's sky** (with a planet frame, every tick):
  - *Scaled to the planet* (`bScalePlanetSky`, spherical Phase 6 WM-BP10): the atmosphere's height, Rayleigh and Mie
    scale heights and ozone layer times K, its scattering and absorption divided by K, so the sky from the ground looks
    as authored while from orbit it is a thin shell. K = `ComputePlanetSkyScale`: the sea radius over Earth's, but enough
    for the air to reach `PlanetAtmosphereOverTerrain` (4) times the highest terrain over the sea, at most 1. Every
    `VolumetricCloud` goes with it (`ComputePlanetCloudLayer`: thickness times K, floor at least the terrain's top; G46).
    From half the atmosphere's height up a second post-process meters exposure on the bright part of the view
    (`bPlanetSpaceExposure`, `ComputeSpaceExposureWeight`), so the sunlit planet keeps its exposure from orbit.
  - *The sky light at the viewer* (`bPlanetSkyLightAtViewer`, cvar `wc.Planet.SkyLightAtViewer`, default 1; visual
    polish VP1-O5): a real-time capture is taken where the sky light stands, and a map's sky light at the planet's
    centre captured the sky from inside the planet (no sky light anywhere, every shadow black). The controller moves it
    to the camera every tick (`ApplyPlanetSkyLight`; the sky light must be movable) and takes its lower hemisphere from
    the captured sky. On `PlanetTest` ground in a hill's shadow went from luma 0 to 69 seen from above.
    `IsSkyLightAtViewer` reports it.
  - *No sun shadows on dense scaled air* (`bLimitPlanetAtmosphereShadows`, cvar `wc.Planet.AtmosphereShadows`, default
    1; VP1-O6): the scaled sky packs a column of air into a layer K as thick, so the air within the sun's shadow cascades
    is 1 / K times as dense as Earth's, and the engine's shadows on the atmosphere (sampled into its camera-turned sky
    view LUT) showed as jagged dark patches moving with the camera, the "pixelated clouds". While the scaled air is over
    `PlanetAtmosphereShadowMaxDensity` (2) times the authored density the sun's `bCastShadowsOnAtmosphere` is off
    (`ShouldShadowPlanetAtmosphere`, `ApplyPlanetAtmosphereShadows`, `AreAtmosphereShadowsLimited`): off on `PlanetTest`
    and `Review_Planet_M`, kept on `Review_Planet_L`. Straight up at 30°N 20°E the sky's blotches fell about sixfold, the
    ground unchanged. The cvar at 0 keeps the light's own setting.
- `FWorldClockSaveState` + `ExportState` / `ImportState` for a save system.

## Rules
- Never read `TimeOfDaySeconds` directly on clients — always `GetTimeOfDaySeconds()` (extrapolated).
- Lighting changes go through the controller's curves / colours, not scattered `SetIntensity` calls.
- Keep this plugin free of gameplay: darkness penalties, torches, schedules belong to consumers that bind
  the delegates or read `GetDaylight()` / `GetPhaseTag()`.
- Consumers that act at a place (spawners, sleeping, the HUD) ask the `Local*` functions with that place: on a planet
  the global hour is longitude 0's.
- Tests: `WorldClock.*` (EditorContext | EngineFilter), pure, no world: `WorldClock.Math.*`, `Clock.Extrapolation`,
  `Lighting.SunColor`, `Planet.SolarTime`, `Planet.Sky`, `Planet.SkyScale`, `ZUpSky`.

## Console
- `wc.Time HH[:MM]` (authority; on a planet the local solar hour where the pawn stands), `wc.TimeScale N` (game seconds
  per real second, 60 = a 24-minute day, 0 = pause), `wc.Clock` (day, time, phase, daylight, scale, sun rotation; on a
  planet the local values too). Registered in `WorldClockConsole.cpp` (moved here from the game module in CR4); the
  game module's `vox.Time`, `vox.TimeScale` and `vox.Clock` run them. The two `wc.Planet.*` cvars are above. All of
  them: `Documentation/EDITOR_CONSOLE_GUIDE.md` §6 (project root).
