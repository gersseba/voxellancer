# Plan 6: Move authoritative simulation to a headless server

## Goal

Separate authoritative game state and simulation from the local client so the
same simulation can run headless on a server and be presented by one or more
clients. Preserve single-player and existing scenarios during the migration;
do not replace the game loop, entity model, and networking in one change.

This is an architecture plan, not a claim that the current benchmark predicts
server capacity. The recent 800-ship run measured about 5.25 ms average game
update time on the development machine, but it does not include server
networking and is not a headless server measurement:
[seeded benchmark](../../benchmark-results/large-combat-vsync-1793874188116.json).

## Current architecture and migration constraints

- [`main.cpp`](../../src/main.cpp) owns startup for configuration, GLFW/OpenGL,
  resource loading, `Game`, the interactive loop, and benchmark execution.
  The executable is therefore not currently headless.
- [`Game`](../../src/gamestate/game.cpp) owns a `Viewer` and a `GamePlay`.
  `GamePlay` constructs its scene, sound manager, input handlers, scenario, and
  running/paused states in
  [`gameplay.cpp`](../../src/gamestate/gameplay/gameplay.cpp).
- [`GamePlayRunning`](../../src/gamestate/gameplay/running/gameplayrunning.cpp)
  advances the global `World`. [`World`](../../src/world/world.cpp) owns
  simulation systems such as `WorldLogic`, `God`, factions, missions, scripts,
  and events, but also owns the local `Player`, `Skybox`, and voxel-particle
  engine. The client scene reads and draws these same global objects in
  [`gameplayscene.cpp`](../../src/gamestate/gameplay/gameplayscene.cpp).
- [`Player`](../../src/player.cpp) mixes ship commands with the camera, HUD,
  aimer, and target selector. Replace the server-facing part with timestamped
  input commands; keep camera and HUD state client-side.
- [`WorldObject`](../../src/worldobject/worldobject.h) combines physics,
  collision, equipment, scripting, handles, and voxel state. Its `VoxelCluster`
  base also owns `VoxelRenderData`
  ([`voxelcluster.h`](../../src/voxel/voxelcluster.h)). A server/client split
  must separate authoritative voxel/entity data from GPU-facing render data.
- [`BaseScenario`](../../src/scenarios/basescenario.h) is constructed with
  `GamePlay` and gets its world through the global singleton. Scenario setup
  needs an explicit simulation context so servers can load missions and combat
  without constructing a client scene.
- [`CMakeLists.txt`](../../CMakeLists.txt) currently glob-compiles `src/` into
  one `voxellancer` executable and links OpenGL, GLEW, GLFW, SFML Audio, Lua,
  OpenMP, and shared libraries together. [`vcpkg.json`](../../vcpkg.json) has
  no networking dependency, and current source has no network protocol or
  state serialization boundary.

These are real seams to address, not reasons to immediately rewrite all
systems. Keep the existing game playable at each phase.

## Recommended runtime model

Use an authoritative server with fixed simulation ticks; do not make clients
run lockstep copies of the entire world.

| Work | Initial rate | Ownership |
|---|---:|---|
| Physics, movement, collision, damage, weapon state | 60 Hz | Server, fixed 1/60 s simulation step |
| AI steering/control | 10 Hz | Server; apply the latest control intent each simulation tick |
| Fighter primary-target reconsideration | 5 Hz | Server; keep event-driven invalidation for destroyed targets |
| Per-hardpoint target reconsideration | 2-5 Hz | Server; stagger by stable entity/hardpoint identity |
| State snapshots | 20 Hz initially; test 30 Hz for active combat | Server to client; send only relevant entities and changes |
| Rendering and presentation effects | Variable | Client; interpolate remote entities and predict local input |

These are starting points to measure, not fixed design laws. Use integer tick
indices for simulation schedules and command ordering. The server owns combat
outcomes; clients send intent, not authoritative positions, damage, or hits.
This avoids requiring bit-identical floating-point simulation on every client.

At 60 Hz each server tick has 16.67 ms of wall-clock budget. Target p95 tick
processing below 12 ms and avoid sustained deadline misses; validate those
targets with a headless server benchmark before treating them as capacity
commitments. If a server tick exceeds its interval, fixed-step simulation falls
behind rather than becoming free. Record tick duration and backlog, cap
catch-up work to avoid an unbounded burst, and shed non-authoritative work such
as distant AI scans before considering dropping authoritative simulation time.

## Migration phases

### 0. Define contracts and preserve a comparable workload

- Document server authority, client prediction/reconciliation, entity
  visibility, disconnect behavior, and the intended single-player hosting
  path.
- Keep the existing large-combat scenario as the common workload. Make ship
  count configurable so the same scenario can run at about 400 ships and at
  the current 800-ship stress level; do not create a separate benchmark for
  every ship type.
- Add a headless simulation benchmark mode that advances an exact number of
  fixed ticks and measures wall-clock tick cost independently from rendering.
- Decide the sustained-overload policy before deployment. Do not silently drop
  authoritative combat ticks.

**Exit criteria:** the existing game and benchmark still run, and the new
headless benchmark reports tick-time percentiles, missed deadlines, entity
counts, and simulation tick count.

### 1. Extract a headless simulation session

- Introduce an owned `SimulationSession`/`SimulationWorld` that contains
  authoritative entities and systems: movement, collision/damage, weapons,
  AI, factions, missions, event handling, and gameplay scripts.
- Move presentation-only state out of the server world: local camera and HUD,
  `Viewer`/scene/render passes, skybox drawing, audio playback, and
  voxel-particle presentation. Server-side gameplay events may request client
  effects, but the server must not load or draw those effects.
- Split `Player` into a client controller (camera, HUD, target selection,
  local input) and a server-recognized pilot/actor receiving input commands.
- Refactor scenarios to accept a `ScenarioContext` or simulation session,
  rather than `GamePlay` plus `World::instance()`. Keep mission and combat
  scripts on the authoritative side; separate presentation-only script APIs.
- Remove the `World` singleton incrementally by passing a world/context
  reference to the systems that currently reach global state. Keep a temporary
  adapter for unmigrated single-player code if needed.
- Separate authoritative voxel contents and collision state from
  `VoxelRenderData`. The client builds or updates render proxies from simulation
  state; the server must not create GPU resources.

**Exit criteria:** a simulation session can load the existing combat scenario,
advance it without a window or audio device, and run in the current client
without changing gameplay outcomes beyond the new fixed-tick policy.

### 2. Establish fixed-tick simulation and server-owned inputs

- Add a `SimulationRunner` driven by a monotonic clock. It advances the world
  only in fixed 1/60-second steps and exposes the current integer tick.
- Collect client input into bounded, sequenced commands. Apply commands at
  defined tick boundaries; keep rendering cadence and frame interpolation out
  of the simulation API.
- Convert AI and target-refresh intervals to tick counts. Preserve the current
  fighter focus and per-hardpoint behavior, but use stable schedule keys and
  deterministic staggering.
- Replace shared gameplay use of global `std::rand` with session/entity-owned
  random streams so one ship's extra random draws do not shift unrelated
  ships' behavior. Server authority remains the source of truth; this is for
  replay/debug consistency, not client lockstep.
- Add fake-clock tests that feed the same input/tick sequence while rendering
  at 30, 60, and 144 Hz and compare authoritative state after the same number
  of simulation ticks.

**Exit criteria:** the same command sequence produces the same simulation
progression for the same tick count regardless of render cadence; timer and
catch-up behavior is covered by tests.

### 3. Define entity identity and a versioned replication protocol

- Add stable server entity IDs (with generation/reuse protection). Existing
  pointer-based `Handle<T>` values remain process-local and must never cross
  the network.
- Define versioned messages for connection/session setup, sequenced input,
  initial world state, snapshots/deltas, and authoritative events such as
  spawn, destruction, damage, weapon fire, mission changes, and voxel removal.
- Replicate only state the client needs to present/predict: entity identity,
  type/loadout, transform and velocity, health/shields, relevant weapon state,
  and gameplay status. Do not serialize C++ objects, pointers, renderer state,
  or entire `World` instances.
- For voxel ships, send a shared blueprint/content identifier plus authoritative
  voxel damage/removal deltas. Define a full-state resynchronization path for
  join-in-progress and recovery.
- Start with reliable delivery for session/lifecycle/control messages and
  sequenced snapshots where newer state supersedes stale state. Select the
  transport/library only after a small platform and latency spike.
- Add interest management so each client receives nearby/visible/relevant
  entities rather than the whole 400-ship world.

**Exit criteria:** protocol tests cover encode/decode, version rejection,
sequence handling, initial state, deltas, and resynchronization without
requiring graphics.

### 4. Add a dedicated server runtime

- Split startup from `main.cpp` into client and server entry points. The server
  loads authoritative configuration, voxel/equipment definitions, missions,
  and scripts, but never initializes GLFW, OpenGL, GLEW, `Viewer`, HUD, or
  audio playback.
- Build a `voxellancer-server` executable around simulation sessions, command
  intake, fixed-tick execution, and snapshot/event output.
- Keep network I/O separate from world mutation: queue validated commands for
  tick boundaries and emit snapshots after a completed tick.
- Define ownership and limits for sessions, players, AI ships, and content
  versions. Keep server gameplay scripts authoritative and prevent clients
  from directly mutating server state.
- Preserve single-player by running the same authoritative session locally.
  Prefer the same command/protocol path; an in-process local server can be
  added after the separate server runtime works.

**Exit criteria:** the server starts and runs the existing combat scenario in
CI on a machine with no display or audio device.

### 5. Convert the client to a presentation and input endpoint

- Build a `voxellancer-client` that owns GLFW/OpenGL, GLEW, `Viewer`, scenes,
  cameras, HUD, sound, and local visual effects.
- Convert local controls into sequenced, tick-stamped commands. Predict the
  local ship for responsiveness, then reconcile it to authoritative snapshots.
- Interpolate remote ships between received snapshots; apply server events to
  voxel rendering, sound, particles, and HUD. These effects must not alter
  authoritative state.
- Keep local single-player and multiplayer presentation on the same client
  path. A hosted single-player game may use the server runtime in-process.

**Exit criteria:** one client can connect to a local server, control a ship,
and display authoritative movement, firing, damage, voxel destruction, and
mission events.

### 6. Split build targets and validate multiplayer operation

- Replace the single source glob/executable with explicit targets such as
  `voxellancer-simulation`, `voxellancer-protocol`, `voxellancer-server`, and
  `voxellancer-client`. Link graphics/audio dependencies only to client
  targets; keep simulation and protocol tests headless.
- Retain the current test suite during migration, then add simulation,
  protocol, and local client/server integration tests. Cover join/leave,
  disconnect/reconnect, duplicate/late input, latency, packet loss, combat,
  voxel damage/splitting, and mission progress.
- Run the existing large-combat scenario through the headless server at both
  400 and 800 ships. Track p50/p95/p99 tick time, missed deadlines, backlog,
  candidate counts, bandwidth per client, and per-client entity counts.
- Require 400-ship 60 Hz runs to meet the agreed tick budget on target server
  hardware before enabling that capacity in production. Keep the 800-ship run
  as a stress test, not a capacity promise.

**Exit criteria:** client and server build independently, headless CI needs no
graphics/audio stack, integration tests pass, and the 400-ship server workload
meets the measured tick and bandwidth budgets.

## Main risks and decisions to resolve during the migration

- **World/global coupling:** `World::instance()` appears across simulation,
  input, gameplay, rendering, and scenario code. Replace it gradually; a
  wholesale removal would make changes hard to verify.
- **Voxel model/render coupling:** `WorldObject` and `VoxelCluster` combine
  authoritative physics/collision/voxel data with render data. Keep voxel
  damage authoritative while making GPU resource creation client-only.
- **Scripts and scenarios:** Lua bindings currently tunnel actions to the
  global world. Split authoritative mission/gameplay functions from
  presentation APIs and ensure scripts run in the correct process.
- **Player ownership:** camera, HUD, aiming, and direct weapon input currently
  live with the `World` player. Introduce explicit commands so clients cannot
  declare hits or overwrite authoritative transforms.
- **Replication volume:** voxel-level destruction can generate large updates.
  Prefer compact removal/damage events and support full-state resync rather
  than repeatedly sending full voxel maps.
- **Simulation overload:** choose how to report and handle missed server ticks.
  Never let a catch-up loop grow without bound or silently discard authoritative
  damage/movement time.
- **Transport and hosting:** select a transport after measuring expected
  player count, latency, platforms, and deployment. Keep the simulation
  independent of that choice. Start with a separate server executable; add an
  in-process host only if it can use the same authority and protocol seams.

This migration is not itself a performance optimization. Its first success
measure is a clean authority boundary and a headless 60 Hz server for the
intended 400-ship workload; throughput improvements should be claimed only
after profiling that server build.
