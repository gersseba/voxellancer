# Voxellancer

Voxellancer is a prototype space-combat game built around voxel ships. Fly a
ship, fight other ships, and play scripted missions in a small set of scenarios.
Ships are made from editable voxel clusters: combat can damage and remove
individual voxels, changing a ship as it fights.

The game and engine are written in C++ and use OpenGL for rendering. Lua scripts
define gameplay scenarios and missions, while INI properties and CSV voxel
clusters describe ships and equipment.

## Documentation

- [Engine architecture](doc/5_engine_architecture.md)
- [How to play](doc/1_howto.md)
- [Build instructions](doc/3_buildinstructions.md)
- [Lua API reference](doc/2_luaapi.md)
- [Agent development guide](AGENTS.md)

## Quick start for contributors

The project uses CMake and vcpkg for its runtime dependencies, and has a
Bandit-based `all_tests` target. Set up vcpkg as described in the
[build instructions](doc/3_buildinstructions.md), then run the build-and-test
harness from the repository root:

```powershell
.\tools\agent-check.ps1
```

The first run configures a Visual Studio build under
`build/agent-vcpkg-vs<major>-stable` when Build Tools are installed, resolves the
manifest dependencies, builds `all_tests`, and runs the tests.
See [AGENTS.md](AGENTS.md) for the development workflow and
[doc/5_engine_architecture.md](doc/5_engine_architecture.md) for where to look
when changing a subsystem.

The protocol has a focused test executable that does not link graphics, audio,
or networking libraries:

```powershell
cmake --build build/agent-vcpkg-vs18-stable --target protocol_tests --config Release
.\build\agent-vcpkg-vs18-stable\Release\protocol_tests.exe
```

In VS Code, **Terminal: Run Task** contains runtime tasks to start the server or
client, start the **Shield Showcase Scenario**, or run either large-combat
benchmark. The shield showcase task builds and launches the graphical client
directly in the showcase.
Run `.\tools\agent-check.ps1 -Mode Test` to build and execute the test suite.

## Headless simulation server

The server executable runs without creating a window or initializing audio.
Start a normal battle server with:

```powershell
cmake --build build/agent-vcpkg-vs18-stable --target voxellancer-server --config Release
.\build\agent-vcpkg-vs18-stable\Release\voxellancer-server.exe --scenario battle --listen-port 27801
```

For a server-only configuration without graphical/audio dependencies, configure
vcpkg with its optional client feature disabled:

```powershell
cmake -S . -B build/server-headless `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON `
  -DVOXELLANCER_BUILD_CLIENT=OFF
```

This builds the server, `server_tests`, and `protocol_tests` without client
packages. With the client disabled, `all_tests` is not configured. The standard
configuration continues to build the game, server, network client, and full
test suite.

`--target-players` accepts values from 1 to 16. With this option the server
waits in a lobby before creating the selected scenario and assigning player
ships. Without a target it starts the scenario immediately. Normal server
operation prints per-client byte and visibility counters once per second.
The version 18 protocol defines generation-protected entity IDs, tick-stamped
input commands, versioned snapshot deltas with entity kind and shield/hull
fractions, uniform scale, and maximum mass, full voxel content, initial
equipment slot/key state for newly visible entities, stable ship-definition
keys, authoritative voxel damage/removal and shield-absorption events, and
mission state notifications, and lobby player counts/world-start state.
Shield-hit events include ship-local impact coordinates and projectile color
for accurate client effects.
Entity-content definition keys and equipment keys are limited to 63 bytes.
Snapshot records are 65 bytes, using a compressed quaternion, 8-bit shield and
hull fractions, a 32-bit scale and maximum-mass value, and three signed
8-bit engine-state axes. Equipment records
are limited to 256 slots per entity, and
clients cap aggregate equipment state at 16,384 slots; subsequent equipment
changes use equipment-only updates. Reliable weapon-fired events identify the
firing entity, simulation tick, and hardpoint; the graphical preview uses them
for at most 32 transient muzzle flashes per frame. Shield-hit events identify
the impacted voxel and absorbed damage; the preview renders at most 32
transient shield sparks per frame. Mission start, success, and failure
notifications are delivered to the remote preview HUD.
Replicated engine state is interpolated and shown as a bounded thrust indicator
for the controlled remote ship.
A two-player lobby can also be started manually:

```powershell
.\build\agent-vcpkg-vs18-stable\Release\voxellancer-server.exe --scenario battle --listen-port 27801 --target-players 2
.\build\agent-vcpkg-vs18-stable\Release\voxellancer.exe -multiplayer-client
.\build\agent-vcpkg-vs18-stable\Release\voxellancer.exe -multiplayer-client -benchmark-client-id client-2
```

While a lobby is waiting, clients show a centered waiting indicator and do not
send gameplay input. Once full, the server creates the world and assigns one
ship to each connection.

The large-combat benchmark runs a seeded 800-ship battle for exactly 1,800
server simulation intervals. The integrated task starts the authoritative server as a child of
the graphical executable. The multiplayer task starts the server first, opens
two visible clients side-by-side, and begins the benchmark after both join.
Each run writes a server report and distinct `client-1`/`client-2` reports to
`benchmark-results\`, all sharing a run ID. Client frame recording begins on
the first started-world snapshot, so the client's observed tick span may be
slightly shorter than the server's full run.

Server reports identify simulation tick, network poll, snapshot construction,
interest filtering, delta building, encoding, and packet-queue costs; they also
include per-client bytes and visible-entity counts. Client reports identify
rendered frame phases, network polling, snapshot sampling, input prediction,
replica updates, and GPU timings when supported. Reports include p50/p95/p99
and maximum timings plus aligned time series. Nested timings overlap and must
not be summed; server tick times and client frame times describe different
work.

For local singleplayer through the authoritative server, launch only the
graphical executable:

```powershell
.\build\agent-vcpkg-vs18-stable\Release\voxellancer.exe -local-singleplayer
```

This option starts the matching `voxellancer-server` beside the game, selects
the battle scenario, chooses an available loopback port, and connects the
client before graphics and gameplay initialization. The server process is
stopped when the game exits. Without this option, `voxellancer.exe` continues
to run its existing standalone local simulation.

The client executable is a headless transport/integration probe; the graphical
game can show opt-in interpolated, render-only voxel proxies, capped
event-driven muzzle flashes and damage sparks, and a position overlay without
applying remote state to its local simulation. Proxy transforms interpolate
server-replicated position, orientation, and scale, then extrapolate velocity
for at most three ticks until a new snapshot reconciles the preview. The
controlled actor also predicts movement from buffered input and replicated
engine/mass state, then resets to authoritative snapshots and replays
unresolved commands. Its HUD lists up to six authoritative equipment slots and
the number of remaining slots. In this mode, movement, rotation, and
primary-fire input are sent to the assigned server actor, and local simulation
ticks are paused while connected. Start a local server, then run
`voxellancer.exe -server-preview`.
Gameplay script engines, mission systems, and Lua world/faction/event bindings
are scoped to their owning `World`; standalone-world script spawning, timer
callbacks, scenario population, and scriptable cleanup are covered by the full
test suite. Scenario clear/reset now rebuilds the simulation state in its
owning world without deleting the global client world. Voxel debris, explosion,
and engine-trail effects also use their creator's presentation world.
Each connection receives entities within 650 world units of its assigned ship;
entering and leaving that radius creates snapshot additions and removals. This
initial proximity rule is a prototype, not camera visibility or team-aware
relevance. Voxel-removal events are sent reliably over TCP only for entities
already in that connection's snapshot baseline; this includes both destroyed
voxels and cells transferred to split-off entities. The client applies the
removals to its replicated voxel content and exposes them for presentation
effects. Nonlethal damage is sent as a fixed-size voxel HP update and applied
to the client's current replicated content; repeated updates for one voxel are
coalesced to the latest HP before delivery. Newly visible entities and full
resynchronization receive current voxel cells, color, density, current HP,
maximum HP, and their initial equipment slots and equipment keys. Clients
receive equipment-only updates when a slot changes, and request a full baseline
if a snapshot delta cannot be applied. The server is for local work, not public
deployment: it has no authentication or encryption.
