# WorldClockPlugin — Claude instructions

> State as of 2026-11-06 (the weather system's P1: the modifier slot; before it code review CR5 and the planet clouds, D-VP1). This file is the plugin's reference; its design record is
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
    `VolumetricCloud` goes with it (`ComputePlanetCloudLayer`, an `FWorldClockCloudLayer` in and out): thickness and
    tracing distances (start, from the camera, max, shadow) times K, floor at least `PlanetCloudFloorOverTerrain` (1.5)
    times the terrain's top (G46; D-VP1, below).
    From half the atmosphere's height up a second post-process meters exposure on the bright part of the view
    (`bPlanetSpaceExposure`, `ComputeSpaceExposureWeight`), so the sunlit planet keeps its exposure from orbit.
  - *The sky light at the viewer* (`bPlanetSkyLightAtViewer`, default on; visual
    polish VP1-O5): a real-time capture is taken where the sky light stands, and a map's sky light at the planet's
    centre captured the sky from inside the planet (no sky light anywhere, every shadow black). The controller moves it
    to the camera every tick (`ApplyPlanetSkyLight`; the sky light must be movable) and takes its lower hemisphere from
    the captured sky. On `PlanetTest` ground in a hill's shadow went from luma 0 to 69 seen from above.
    `IsSkyLightAtViewer` reports it.
  - *No sun shadows on dense scaled air* (`bLimitPlanetAtmosphereShadows`, default on;
    VP1-O6): the scaled sky packs a column of air into a layer K as thick, so the air within the sun's shadow cascades
    is 1 / K times as dense as Earth's, and the engine's shadows on the atmosphere (sampled into its camera-turned sky
    view LUT) showed as jagged dark patches moving with the camera, the "pixelated clouds". While the scaled air is over
    `PlanetAtmosphereShadowMaxDensity` (2) times the authored density the sun's `bCastShadowsOnAtmosphere` is off
    (`ShouldShadowPlanetAtmosphere`, `ApplyPlanetAtmosphereShadows`, `AreAtmosphereShadowsLimited`): off on `PlanetTest`
    and `Review_Planet_M`, kept on `Review_Planet_L`. Straight up at 30°N 20°E the sky's blotches fell about sixfold, the
    ground unchanged. Off, the light keeps its own setting.
  - *The clouds* (visual polish decision D-VP1, 2026-11-02): scaled as above (VP1-O1), and none on a planet whose sea
    radius is under `PlanetCloudMinRadiusKm` (10; `ShouldDrawPlanetClouds`, the component's visibility off,
    `GetHiddenCloudLayers`). Results below.
- `FWorldClockSaveState` + `ExportState` / `ImportState` for a save system.
- **The modifier slot** (the weather system's P1, 2026-11-06; the weather plan's D61): `FDayNightModifiers` on the
  controller (`Modifiers`, `SetModifiers`), identity by default: multipliers of the sun, the sky light and the height
  fog's density applied in `Apply` after the daylight curves, a tint multiplied into the fog's inscattering colour,
  absolute cloud coverage and density written to every `AVolumetricCloud` layer's material (`CloudLayers`, found by
  class at BeginPlay; `CloudCoverageParameter` / `CloudDensityParameter`, the engine's `Cloud_GlobalCoverage` /
  `Cloud_GlobalDensity`; a dynamic instance made on the first write, the authored values restored when the slot goes
  negative), and a lightning flash that brightens the sky light by `1 + flash x LightningFlashSkyLight`. The controller
  stays the one writer of the sky actors: `UWorldWeatherSubsystem` fills the slot every tick on every machine; a world
  without weather is untouched. Test `WorldClock.Lighting.Modifiers`.

## Planet clouds (D-VP1, 2026-11-02)

Before, the engine's layer (5 km floor, 10 km thick, traced from 350 km, 50 km through, 15 km of shadow) had only its
altitudes scaled: on `PlanetTest` (2.5 km) 65-108 m with the floor at the terrain's top, on `Review_Planet_M` (50 km)
2.43-4.05 km, a peak in the cloud base. Now the tracing distances scale with K too and the floor is 1.5 times the terrain
top: `PlanetTest` 98-141 m (hidden), M 3.64-5.26 km traced 8.1 km, shadows 2.4 km, from 56.7 km; L 12.7-18.3 km.

Seen in PIE (30°N 20°E, noon and a 10° sun, eye level, 30° up and straight up, and from 60 and 150 km; images in the
WorldClockPlugin and parent PRs):
- **`PlanetTest`**: its clouds were never visible (VP1); hidden, every view differs from before by under 0.7 luma of 255
  (p99 5). 10 km sits between it and M, the only planets looked at: a planet's layer is K x 10 km thick, tens of metres
  on a small one, where the Earth-sized cloud material draws nothing.
- **`Review_Planet_M`**: from the ground a few soft cumulus near the horizon and thin wisps overhead, as before but
  higher and smaller; no flicker either way (the old and new layer set by hand in one PIE: 0.28-0.34 luma frame to frame
  with the camera still, 4.6-5.0 turning 2° a frame). From orbit the clouds are gone: the scaled start distance (56.7
  km) is shorter than the way to the layer from 60 km up. Before, they showed there as faint pale smudges and a ragged
  halo of patches around the limb; the start distance left at 350 km brings both back.
- **`VoxelDemo`** (a plane): no planet frame, untouched (the same clouds, drifted by the material's wind).

The look is the cloud material's: `m_SimpleVolumetricCloud_Inst` shapes clouds with noise kilometres across, so M gets
a sparse field of Earth-sized clouds and its orbit view no cloud pattern. A planet-aware material whose noise follows K
(VP1-O3, not done) would give smaller, denser clouds on M and a readable pattern from orbit.

## Rules
- Never read `TimeOfDaySeconds` directly on clients — always `GetTimeOfDaySeconds()` (extrapolated).
- Lighting changes go through the controller's curves / colours, not scattered `SetIntensity` calls.
- Keep this plugin free of gameplay: darkness penalties, torches, schedules belong to consumers that bind
  the delegates or read `GetDaylight()` / `GetPhaseTag()`.
- Consumers that act at a place (spawners, sleeping, the HUD) ask the `Local*` functions with that place: on a planet
  the global hour is longitude 0's.
- Tests: `WorldClock.*` (EditorContext | EngineFilter), pure, no world: `WorldClock.Math.*`, `Clock.Extrapolation`,
  `Lighting.SunColor`, `Lighting.Modifiers`, `Planet.SolarTime`, `Planet.Sky`, `Planet.SkyScale`, `ZUpSky`.

## Console
- `wc.Time HH[:MM]` (authority; on a planet the local solar hour where the pawn stands), `wc.TimeScale N` (game seconds
  per real second, 60 = a 24-minute day, 0 = pause), `wc.Clock` (day, time, phase, daylight, scale, sun rotation; on a
  planet the local values too). Registered in `WorldClockConsole.cpp` (moved here from the game module in CR4); the
  game module's `vox.Time`, `vox.TimeScale` and `vox.Clock` run them. The two planet cvars (SkyLightAtViewer,
  AtmosphereShadows) went in VoxelWorlds 8; the properties above stay. All of them: `Documentation/EDITOR_CONSOLE_GUIDE.md`
  §6 (project root).
