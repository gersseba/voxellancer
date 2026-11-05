# Plan 6: Move authoritative simulation to a headless server

## Current status

The core client/server migration is implemented. `voxellancer-server` runs the
authoritative simulation without graphics or audio, `voxellancer` provides the
graphical client and retains the standalone local mode, and protocol v16
replicates input, state, voxel/equipment content, and gameplay events over
loopback TCP. `voxellancer.exe -local-singleplayer` starts the sibling server
and connects before initializing the graphical client.

The complete process, message, ownership, prediction, and presentation flow is
documented in the [engine architecture guide](../5_engine_architecture.md).
The migration phases later in this file are retained as a design record; only
the validation and refactoring work listed under
[remaining work](#remaining-work) is outstanding.

Development benchmarks establish observations, not capacity guarantees. The
latest 800-ship client run measured 47.23 average FPS over 1,800 measured frames
with VSync enabled. The corresponding 1,800-tick headless server run measured
p50/p95/p99 tick times of 1.572/8.271/10.017 ms, a 48.242 ms maximum, and two
ticks over the 16.67 ms budget. Client frame time includes rendering and cannot
be compared directly with server tick time. Neither run establishes target
hardware or networked multiplayer capacity.

## Architecture and remaining boundaries

- The graphical executable owns GLFW/OpenGL, audio, input, HUD, scenes, client
  prediction, and remote presentation. In local singleplayer it starts a
  separate headless server process and loads the battle scenario locally as a
  camera/presentation anchor; local simulation is paused while remote preview
  is active.
- The server owns `World` simulation systems, scenario/mission/script state,
  entity identity, command validation, fixed-tick execution, and authoritative
  combat outcomes. Its listener currently binds only to loopback.
- `SimulationSession` advances at 60 Hz with bounded catch-up work and retains
  excess time as backlog. The TCP server polls on the same thread, accepts up
  to 16 clients, sends a per-client snapshot every three simulation ticks, and
  filters entities to a 650-unit interest radius.
- The client and server share `WorldObject`/voxel model code, but the server
  build omits GPU voxel-render data and presentation systems. Some client
  gameplay/input code still relies on `World::instance()` and on a local
  `Player`; completing that client-side ownership cleanup is a larger refactor.
- The current protocol is reliable, ordered TCP with versioned length-prefixed
  messages. There is no authentication, encryption, internet-facing listener,
  transport negotiation, or production hosting policy.

## Implementation status and concise remaining plan

The migration has been implemented in incremental testable slices:

1. **Fixed-step authority:** `SimulationSession` advances at 60 Hz, bounds
   catch-up work, queues sequenced input for tick boundaries, and supports a
   headless benchmark. Ship AI steering is staggered at 10 Hz, while fighter
   and per-hardpoint target refreshes use deterministic integer-tick schedules.
   Target-fire and fighter-evasion randomness uses a reproducible stream
   seeded from each ship's stable entity ID.
2. **Identity and replication foundation:** entities receive generation-safe
   IDs; versioned input acknowledgements, snapshot deltas, full-baseline
   resynchronization, voxel content with current/maximum HP, initial equipment
   slot/key content, stable ship-definition keys, equipment-only updates,
   weapon-fired, shield-absorption, and mission-state events, coalesced
   per-voxel HP updates, and voxel-removal event transport are tested. Damage,
   initial/dynamic equipment transfer, weapon firing, shield absorption,
   mission notifications, and voxel splitting are covered through the TCP
   client; split
   fragments receive full content and their removed source cells are replicated.
3. **Local transport:** a loopback TCP server and headless client exercise
   actor assignment, snapshot updates, future-tick input scheduling, late-input
   rejection, reconnect, and disconnect cleanup. A two-client integration test
   verifies distinct actor assignment, per-client input acknowledgements,
   control isolation, and that one client's disconnect does not remove the
   other client's actor.
4. **Interest and presentation preview:** snapshots use a per-client 650-unit
   proximity radius, and `-server-preview` interpolates render-only voxel
   proxies, then extrapolates velocity for at most three ticks while waiting
   for the next authoritative snapshot. New snapshots reconcile that
   presentation state. The controlled actor additionally predicts movement by
   replaying tick-stamped input against replicated engine power and maximum
   mass; each authoritative snapshot resets that state before unresolved
   commands are replayed. The preview forwards movement, rotation, and primary
   fire. HP-only updates preserve existing proxy geometry instead of rebuilding
   the cluster. The preview HUD displays the controlled actor's equipment keys
   for up to six slots and reports any additional slots. Render proxies restore
   mounted engine/weapon voxel color and emissiveness from local asset
   properties, including after equipment-only updates.
   Reliable weapon-fired events drive render-only muzzle flashes capped at 32
   per frame; voxel-damage events drive render-only hit sparks capped at 64,
   and shield-hit events drive distinct shield sparks capped at 32 per frame.
   The local simulation pauses while the remote preview is
   connected. TCP coverage verifies primary fire reaches the server and its
   projectile and weapon-fired event are replicated, and that damage events
   include the authoritative voxel health update. `voxellancer.exe
   -local-singleplayer` starts a sibling server with the battle scenario on an
   available loopback port before client initialization, then owns its process
   lifetime and follows the assigned actor with the presentation camera.
5. **Compact protocol and shared build:** snapshot entities use 65-byte records
   with smallest-three quaternion encoding, 8-bit shield and hull fractions,
   transform scale, maximum mass for local movement prediction, and quantized
   three-axis engine state for remote presentation. The client
   interpolates scale for voxel proxies and accounts for it when positioning
   weapon-fire flashes.
   The protocol is a separate static library linked by the game, server,
   client, and test targets. Protocol-only tests need no graphics, audio, or
   networking library.
6. **Headless build boundary:** server sources omit game-state, device input,
   GPU rendering, and audio implementations. `World` omits client presentation
   ownership and updates; `VoxelCluster` omits GPU render data. The server core
   is a reusable static library and has a dedicated TCP integration-test
   target. It links a small GL-free logging/change-observer support library
   instead of Glow/GlowUtils and does not link OpenGL, GLEW, GLFW, or SFML
   Audio. Client dependencies are optional in CMake and the vcpkg manifest.
7. **Scenario boundary:** benchmark and gameplay scenarios accept an explicit
   `World&`; scenario population uses world-bound builders, factions, scripts,
   missions, and player presentation guards. A test loads `BattleScenario`
   into a standalone server `World` without mutating the client singleton, and
   a TCP integration test exercises a standalone server world.
   World objects retain their owner; `God` routes rejected spawns through it,
   `WorldLogic` injects it into movement and damage handlers, AI target
   schedules and weapon/projectile lifecycle use it, and engines gate
   presentation through the owning world. AI query paths, faction relations,
   rocket board computers, and fired-projectile builders also use that owner.
   `ScriptEngine`, `EventPoller`, and
   `MissionSystem` now retain their owning world; scriptables unregister from
   their owning engine, and gameplay Lua bindings route object creation,
   spawning/removal, faction access, timers, event polls, and mission creation
   through that world. Mission construction also requires an explicit world.
   Rocket and split-rocket construction, including builder-backed range
   queries, also require the owning world instead of falling back to the
   singleton. Explicit-world builders bind created objects before
   setup, and standalone server-world tests verify spawning, timer callbacks,
   scenario population, and scriptable cleanup do not mutate the client singleton.
   Scenario clear/reset rebuilds scripts, missions, factions, handlers, and
   entities in the owned world without deleting the client singleton; simulation
   ticks and entity-ID generations remain monotonic across the reset. Voxel
   debris, explosion, and engine-trail particles use their creator's world for
   presentation. Presentation APIs remain guarded. Scenario loading now passes
   an explicit world from `GamePlay`; `GamePlayRunning` also receives that
   world explicitly when constructing its `SimulationSession`. Remaining
   singleton access is limited to application entry points and client input or
   presentation paths; the server simulation and shared simulation directories
   do not resolve the global world.

On the development machine, a 600-tick headless run with 200 ships per team
(400 initially) reported p50/p95/p99 tick times of 0.803/3.186/3.511 ms,
with no missed deadlines and 284 ships remaining. A 600-tick stress run with
400 ships per team (800 initially) reported 1.495/8.954/13.814 ms, one missed
deadline, and 454 ships remaining. These are development measurements, not
capacity commitments. A 5-second loopback sample with 100 ships per team
had 280-338 visible entities and averaged 235,828 application bytes/s over
four post-initial one-second intervals (239,992, 211,222, 220,852, and 271,246
bytes). Two-client actor/input isolation is now covered by integration tests,
and a 6-second, two-client loopback smoke sample at 100 ships per team measured
about 114 KB/s and 97 KB/s per client across five successive one-second
reports, with visible entity counts varying from 49 to 107. These are
development-machine observations, not capacity evidence; target-hardware
performance remains unmeasured.

## Remaining work

The fixed-tick session, protocol, server/client executables, local singleplayer
path, replicated presentation, headless builds, and focused integration tests
are implemented. The following work is still required before making
multiplayer or capacity claims:

1. **Network-load validation:** exercise delayed and out-of-order input,
   reconnects, multi-client combat, snapshot resynchronization, queue
   saturation, and disconnect cleanup under controlled latency and sustained
   load. Record per-client bandwidth and visible-entity counts.
2. **Target-hardware capacity evidence:** repeat 400-ship runs on intended
   server hardware and require p95 tick time below 12 ms without sustained
   deadline misses. Keep 800 ships as a stress test, not a capacity promise.
   The latest development-machine run met the p95 target but had a 48.242 ms
   maximum and two missed deadlines.
3. **Production hosting and transport:** before accepting non-local clients,
   choose and implement authentication, encryption, address binding,
   reconnect/session policy, and a transport/overload policy. The present
   listener is loopback-only TCP and is not suitable for internet exposure.
4. **Client ownership cleanup:** decouple the camera/controller from the
   local `World`/`Player` and reduce remaining global-world and voxel
   render/simulation coupling. This is a separate, incremental refactor; it
   must preserve the standalone game and headless server boundary.

Keep the server-only build and headless tests as acceptance checks throughout
these follow-up changes. Do not treat development-machine measurements as
deployment limits.

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

## Original migration phases

Phases 0-5 below are an implementation record, not a list of remaining work.
Their behavior and current boundaries are summarized above and in the
[engine architecture guide](../5_engine_architecture.md). Phase 6's build and
integration-test criteria are in place; its target-hardware capacity gate
remains part of the validation work listed above.

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

**Implemented:** client and server build independently, headless CI does not
require graphics/audio packages, and protocol/server integration tests pass.
**Still required:** demonstrate the 400-ship tick and bandwidth budgets on
intended server hardware.

## Remaining risks

- **Client ownership:** application and presentation code still contains local
  `World`/`Player` dependencies, and local singleplayer creates a local
  presentation scenario alongside the authoritative server scenario.
- **Shared voxel model:** the server build avoids GPU rendering state, but
  `WorldObject`/voxel data are not yet separate simulation and presentation
  models.
- **Replication load:** initial entity content includes voxel/equipment data;
  limits and delta/resync behavior need stress testing with multiple clients.
- **Overload behavior:** catch-up work is bounded and backlog is retained, but
  there is no production policy for sustained overload or service recovery.
- **Remote hosting:** TCP is currently bound to loopback and has no
  authentication or encryption. Do not expose it beyond a trusted local
  machine until hosting and security are designed.

The migration establishes an authority boundary and a headless fixed-tick
runtime; it is not itself a performance optimization. Make performance or
capacity claims only after repeatable profiling on the intended hardware and
network conditions.
