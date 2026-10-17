# Feature 6 — World clock and day / night cycle (plan, 2026-10-10)

Roadmap row: *"Surface time-of-day driving the sun. Prerequisite for darkness mattering."* Feature 7 (light as a
mechanic) and feature 8 (surface gameplay) consume it; feature 9 (persistence) saves it.

Status: **BUILT + VERIFIED 2026-10-10** (all D1–D8 as proposed; new repo; 24-minute day; faint moon at night). See §10.

## 1. Goal

A server-authoritative game clock that every machine agrees on, a day / night cycle that visibly moves the
sun, sky, sky light and fog in the demo world, phase events (dawn / day / dusk / night) other systems can
subscribe to, a HUD readout, and debug commands to jump / speed up time. Everything reachable by hand in
`VoxelDemo` (watch a sunset, read the clock).

**In scope:** clock subsystem + replication, lighting controller with curves, phase tags + delegates, material
parameter collection for shaders (consumed later), HUD clock, `vox.Time` / `vox.TimeScale` / `vox.Clock`,
save-state struct + import / export, tests, demo wiring.

**Out of scope:** moon / stars / weather, temperature or gameplay effects of darkness (feature 7), per-dungeon
time (dungeons stay interior-lit), calendar / seasons, saving to disk (feature 9 calls the export).

## 2. What the survey found

- No time-of-day code anywhere in the project or plugins.
- `VoxelDemo` lighting: a **Movable** `DirectionalLight` (intensity 6, `AtmosphereSunLight` on), a
  `SkyAtmosphere` (so sky colour follows the sun direction for free), a `SkyLight`, `VolumetricCloud`,
  `ExponentialHeightFog` (density 0.0256) and the engine `SM_SkySphere` with `M_SimpleSkyDome` (no sun
  parameters; the atmosphere draws over it). The sun sits at pitch -16° — a fixed late afternoon.
- Dungeons are tile-lit interiors with their own post-process (feature 3 E5), unaffected by the sun.
- Plugins are independent git submodules depending on engine modules + GameplayTags (`WorldClaims` is the
  template: `.uplugin`, one runtime module, native tags in a `*Tags.cpp`).
- `AVCPlayerController` creates HUD widgets from `TSubclassOf<UUserWidget>` slots set by the demo Blueprint;
  adding a `ClockWidgetClass` slot keeps the character plugin agnostic of the clock.

## 3. Decisions proposed

| # | Decision | Proposed |
|---|----------|----------|
| D1 | Home | **New plugin `WorldClockPlugin`** (module `WorldClock`, own repo + submodule like the others), depending on Core / Engine / GameplayTags / UMG only. Nothing in the game framework stack knows about it; consumers (character HUD, feature 7, POI) reach it through its subsystem. |
| D2 | Time model | `UWorldClockSubsystem` (world subsystem, game worlds only): `TimeOfDaySeconds` 0–86400 plus `Day` counter; advances by `TimeScale` (game seconds per real second, default 60 → one 24-minute day). Authority ticks it; a replicated `AWorldClockState` info actor (always relevant) carries a sync point (time, server timestamp, scale) and clients extrapolate from the game state's server time, so every machine reads the same hour without per-frame replication. `UWorldClockSettings` (developer settings, `DefaultGame.ini`): `DayLengthMinutes` 24, `StartHour` 8, `SunriseHour` 6, `SunsetHour` 18, `TwilightHours` 1 (dawn / dusk width). |
| D3 | Phases | `Time.Phase.Dawn / Day / Dusk / Night` native tags in the plugin; `GetPhase()`, `OnPhaseChanged(Old, New)`, `OnHourChanged(Hour)`, `OnNewDay(Day)` delegates (server + clients, from the extrapolated time). `GetDaylight()` 0–1 (night → noon) for simple consumers. |
| D4 | Lighting | `ADayNightController` actor: references the directional light, sky light, fog and (optional) sky sphere; finds them by class when unset; placed in the map by the demo script (auto-spawn when missing is an option on the settings). Every machine drives from the clock each tick: sun yaw/pitch from the hour (noon straight overhead on the map's `SunAzimuthDegrees`), sun intensity / colour, sky light intensity, fog density / colour from `FRuntimeFloatCurve` / colour curves with code defaults (day 6 lux-ish, dusk warm, night 0.05 "moon" so the world stays navigable until feature 7 adds torches). Writes `MPC_WorldClock` (material parameter collection: `TimeOfDay`, `Daylight`) for shaders later. |
| D5 | HUD | `UWorldClockWidget` (code-built text, "Day 1  08:24", top-centre) in the plugin; `AVCPlayerController::ClockWidgetClass` slot (None = no clock) set on the demo controller Blueprint. |
| D6 | Debug | `vox.Time HH[:MM]` (set the hour, authority), `vox.TimeScale N`, `vox.Clock` (print day / time / phase / scale). |
| D7 | Persistence hook | `FWorldClockSaveState { Day, TimeOfDaySeconds, TimeScale }` with `ExportState` / `ImportState` on the subsystem; feature 9 serialises it. |
| D8 | Dungeons | Untouched: the sun never reaches interiors. (Feature 7 decides whether dungeons get "always night" rules.) |

Open questions: (a) day length 24 real minutes (1 minute = 1 game hour) — fine for QA, or longer? (b) should the
clock pause while the inventory / map UI is open (proposal: no, the world keeps turning)? (c) night brightness:
a faint moon so you can still see (proposal) vs true dark now?

## 4. Ownership map

| Repo | Change |
|------|--------|
| WorldClockPlugin (new) | settings, subsystem + state actor, tags, lighting controller + MPC asset, HUD widget, tests, docs, instructions. |
| VoxelCharacterPlugin | `ClockWidgetClass` slot on `AVCPlayerController` (created + anchored top-centre like the others). |
| Parent | `.gitmodules` + `.uproject` entry, `Config/DefaultGame.ini` section, `Scripts/configure_world_clock.py` (place the controller in VoxelDemo pointing at the existing lights, set the clock widget on `BP_VCPlayerController`), `vox.Time` / `vox.TimeScale` / `vox.Clock`, CLAUDE.md, memory. |

## 5. Flows

**Server tick:** `TimeOfDaySeconds += Delta * TimeScale`; wrap at 86400 → `Day++`, `OnNewDay`; hour / phase
edges → delegates. Every 5 s (and on any `SetTimeOfDay` / `SetTimeScale`) the state actor republishes the sync
point.
**Client read:** `Time = SyncTime + (ServerNow - SyncStamp) * TimeScale`; delegates fire from the same
extrapolated value so UI and lighting never disagree with the server by more than a replication hop.
**Lighting tick (all machines):** controller asks the subsystem for the hour → curves → light / fog / MPC.
**Debug:** `vox.Time 20` → `SetTimeOfDay(20 h)` on the server → republish → sunset everywhere.

## 6. Replication

One always-relevant info actor with three replicated values; no per-frame traffic. Authority-only setters;
clients that call them get a warning.

## 7. Tests (`WorldClock.*`)

- `Math.SunRotation` — hour → pitch / yaw (noon up, midnight down, sunrise / sunset on the horizon).
- `Math.Phases` — boundaries with twilight width; wrap at midnight.
- `Clock.Extrapolation` — a sync point + elapsed server time reproduces the authority's clock; wrap → new day.
- `Lighting.SunColor` — night blue, day white, warm only at the daytime horizon.

## 8. PR plan (user merges each)

| PR | Repo |
|----|------|
| A | WorldClockPlugin (new repo) — everything in the plugin |
| B | VoxelCharacterPlugin — `ClockWidgetClass` slot |
| C | Parent — submodule + `.uproject`, config, demo script, vox commands, bumps |

## 9. Verification + QA rows (demo access)

| Scenario | How to reach in VoxelDemo | Expected |
|----------|---------------------------|----------|
| Clock runs | start PIE, read the top-centre clock | starts at 08:00, one game hour per real minute |
| Sunset | `vox.Time 17:30`, wait | sun drops, sky reddens, fog warms, HUD reads 17:30 → 18:00, phase Dusk |
| Night | `vox.Time 23` | dark with a faint moon, torches in the dungeon unaffected |
| Fast-forward | `vox.TimeScale 600` | a full day in a few minutes; `vox.Clock` shows the day counter rising |
| Pause | `vox.TimeScale 0` | sun and clock hold |

## 10. Verification log (2026-10-10, PIE in VoxelDemo)

- Tests: `WorldClock.Math.SunRotation`, `Math.Phases`, `Clock.Extrapolation`, `Lighting.SunColor` all pass in-editor.
  `Math.Phases` caught a real bug: the first daylight ramp measured "nearest edge" on the 24-hour circle, which
  aliases when night is exactly 12 hours (sunset read 0 instead of 0.5); it now measures hours since sunrise.
- Demo wiring (`Scripts/configure_world_clock.py`): `MPC_WorldClock` created, `DayNightController` placed in
  `VoxelDemo` bound to `DirectionalLight` / `SkyLight` / `ExponentialHeightFog`. The controller Blueprint lives in
  VoxelCharacterPlugin (which stays clock-agnostic), so the demo got `BP_VoxelDemoPlayerController` (a game-side
  subclass with `ClockWidgetClass = WorldClockWidget`) and `BP_ThirdPersonGameMode` now points at it.
- Clock runs: HUD reads `Day 1  08:00` top-centre at PIE start and `08:32` about 32 s later (1 h / min).
- Sunset: `vox.Time 17:30` → warm low sun; `vox.Time 18:30` logs `phase Day -> Dusk`; `vox.Time 23` logs
  `Dusk -> Night`.
- Fast-forward: `vox.TimeScale 600` → `day 2 01:06` after 12 s. Pause: `vox.TimeScale 0` holds `01:17` across
  two reads 6 s apart.
- First night pass was far too bright: the map has no post-process volume, so default auto-exposure lifted the
  moonlit scene back to daylight. Fix: the controller now carries an unbound `UPostProcessComponent` applying an
  exposure bias curve (-3.5 stops at night, 0 by day).

## 11. Planets (VoxelWorlds spherical Phase 4, 2026-10-17)

Decision D12 (VoxelWorlds `WORLD_MODES_REVIEW_PLAN.md` §8, §18): a **world-fixed sun** over a planet that cannot turn.
With a planet frame (`UWorldClockSubsystem::SetPlanetFrame(Centre, SeaRadius)`, set by VoxelWorldPOI's
`UPOIPlanetClockBridge` once the chunk manager reports a shell) the sun's direction is (cos λs, sin λs, 0) with
λs = (12 − hour) × 15°, at declination 0 (no axial tilt), and the clock's hour is the local solar hour at longitude 0.

- Local solar time at a place: the hour + longitude / 15 (`UWorldClockMath::LocalHourOffset`, `WrapHour`):
  `GetLocalHour`, `GetLocalDay`, `GetLocalPhase` (the plane's hour rules), `GetLocalTimeText`; `LocalToGlobal` and
  `SetLocalTimeOfDay` convert a local time back.
- Light by the sun's true elevation at the viewer: `DaylightForElevation(e)` = `DaylightForHour` at the morning hour of
  the same elevation, so on the equator it equals the hour curve (sunrise 6, sunset 18) and the poles sit in a low sun.
- The controller centres the SkyAtmosphere on the planet (`PlanetCenterAtComponentTransform`, bottom radius = the sea
  radius in km), skips the height fog and hides actors tagged `WorldClock.ZUpSky` (the setup script tags PlanetTest's sky
  sphere); the widget shows the local day, time and phase.
- Consumers on local time: the HUD, the surface NPC spawner (each creature's phase where it stands), sleeping at a
  campsite (wakes at the local dawn there), `vox.Time` (sets the local hour where the pawn stands) and `vox.Clock`.
- Without a frame every `Local*` function returns the global value, so heightmap worlds are unchanged.
- Tests: `WorldClock.Planet.SolarTime`, `WorldClock.Planet.Sky`. Live (the exit check on `Review_Planet`): 17:32 local
  Day with the sun at 6.8°, 18:15 Dusk at −3.7°, 23:02 Night at −71°, fast-forward over two days, pause holds.
