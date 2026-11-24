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
- [Weapons, shields, and damage reference](doc/6_weapons_and_shields_concept.md)
- [Dogfight AI and controlled shooting-skill comparisons](doc/7_dogfight_ai.md)
- [Agent development guide](AGENTS.md)

## Quick start for contributors

The project uses CMake and vcpkg for its runtime dependencies, and has a
Bandit-based `all_tests` target. Set up vcpkg as described in the
[build instructions](doc/3_buildinstructions.md), then run the build-and-test
harness from the repository root:

```sh
cmake -P tools/agent-check.cmake
```

The harness is a CMake script and works on Windows, macOS, and Linux. It finds
vcpkg in standard locations (setting `VCPKG_ROOT` is optional), configures a
build directory (`build/macos` on macOS, `build/agent-vcpkg-stable` elsewhere),
resolves the manifest dependencies, builds `all_tests`, and runs the tests.
See [AGENTS.md](AGENTS.md) for the development workflow and
[doc/5_engine_architecture.md](doc/5_engine_architecture.md) for where to look
when changing a subsystem.

The protocol has a focused test executable that does not link graphics, audio,
or networking libraries:

```powershell
cmake --build build/agent-vcpkg-vs18-stable --target protocol_tests --config Release
.\build\agent-vcpkg-vs18-stable\Release\protocol_tests.exe
```

In VS Code, choose **Terminal: Run Task > start game** to build and launch the
game and its local server. The scenarios are available from the main menu.
Tests, multiplayer, and benchmarks remain available through the command-line
harness; see the [build instructions](doc/3_buildinstructions.md#build-test-and-run-harness).

### Compare dogfight AIs

The original AI remains the default. Choose **Scenarios > AI Dogfight Comparison**
from the main menu, or launch a built client from the repository root:

```sh
./build/macos/voxellancer -scenario ai-comparison -nointro
```

Sixteen controlled duels compare flight behavior with identical shooting
skills, then independently vary lead, tracking, and accuracy through
0, 0.5, and 1. Every contrast has mirrored starts.
**1–9 / 0** selects pairs 1–10; **comma / period** cycles all 16 pairs;
**F** toggles free camera.
The scoreboard shows the experiment, both skill settings, shots, hits,
wins, and remaining hull; unresolved fights time out at
120 simulated seconds, not automatic wins. For a fast, graphics-free evaluation:

```sh
./build/macos/voxellancer-server --scenario ai-comparison
```

Both modes print per-pair results and a final summary. See the
[comparison guide](doc/7_dogfight_ai.md) for conditions and limitations.
Normal NPCs share the three `[aiShooting]` defaults in
[data/config.ini](data/config.ini), independently of their flight archetype.

### Weapon test range

Choose **Scenarios > Weapon Test Range**, or:

```sh
cmake -DMODE=Run -DRUN=WeaponRange -P tools/agent-check.cmake
```

Fly a single-gun fighter with unlimited energy against an unshielded, stationary
C306, the largest available ship. **1–6** select the contract guns in catalog
order; **7** selects the legacy gun, **8** the ion cannon, and **9** toggles the
legacy railgun and snow cannon. The selection appears briefly on screen.
**0** resets the fighter and target. This standalone local test does not use a
server or alter a contract run; rocket launchers are not included.

## Station contracts (first progression slice)

Normal graphical startup opens the main menu and automatically starts a local
contracts server. In VS Code, **start game** opens this menu;
`-local-singleplayer -scenario contracts` remains supported.

- **Contracts** enters or returns to the prepared contracts session.
- **Scenarios** opens the scripted demo, legacy fleet, battle, frozen scene,
  pirates, friendly fire, shield showcase, 800-ship combat, AI comparison,
  and weapon range. Selecting one starts it afresh.
- **Resume** returns to the current Contracts or scenario session.
- **Settings** changes VSync, antialiasing, the flight HUD, and invulnerability.
  Changes apply immediately and are saved in the user configuration directory.
  Invulnerability defaults to **On** for now and protects your own pilot's
  shields and hull on the server, including co-op. Other pilots and enemies
  are not affected by your toggle. Turn it off for normal combat damage.
- **Exit** closes the game.

Contracts is disabled until the server has assigned a pilot and sent the
initial world and contract state. Scenarios and Settings remain available while
connecting or disconnected. Use arrows or Tab and Enter, or the mouse, to
navigate. Press **Esc** during gameplay to open the menu, and again to resume.
Local scenarios pause in the menu; the online simulation continues in the menu
and during scenarios, but remote flight and firing input are neutralized.
Connection loss returns an active Contracts session to the menu, without
interrupting a local scenario. Restart the game after a connection failure to
reconnect. The contract run remains live when switching modes and is not reset.

Menu, HUD, loading, and intro text use the generated **Voxellancer Flight**
font, rendered as textured triangles instead of voxel letters. Font originals
and atlas details are in [data/ui/fonts](data/ui/fonts/README.md).
Direct scenario launch options, benchmarks, and the developer server preview
still bypass the main menu. The 800-ship scenario is not a benchmark report.

At the starting base, click **HANGAR** or press **Enter / H** to open the
branching sector map. Select a connected node with the mouse or arrow keys,
then click **TRAVEL** or press **Enter**. In co-op, everyone confirms the same
shared destination; changing it clears the party's readiness. Only the server
can advance the route. The cursor is already available.

The map has six steps after the starting base, with branching and merging
paths, randomized intermediate node types/attack variants, and a final blockade attack:

| Node | Behavior |
|---|---|
| Attack 1: Raider Patrol | Light patrol in open space; 100-credit reward option. |
| Attack 2: Debris Crossfire | Autocannon/scatter ambush in a dense wreck field; 150-credit option. |
| Attack 3: Fortified Blockade | Shielded pulse/plasma defenders behind an asteroid belt; 200-credit option. |
| Base | Shop, equipment fitting, and a full shield refill. |
| ? Encounter | Investigate a derelict cache, a distress call, or a decoy ambush. Save the friendly wingman during Distress Call for +75 CR on the credit choice. |

Each sortie opens with the encounter name and its objective across the HUD for
five seconds. Arenas use one of five procedurally generated cover layouts,
each produced by its own placement algorithm from destructible asteroids and
wreckage (hull plates, girders, torn hull sections, derelict hulks):

| Layout | Arrangement |
|---|---|
| `none` | Open space. |
| `asteroidfield` | 10–14 mixed-size rocks scattered through the whole arena. |
| `asteroidline` | A bowed, slanted belt of rocks across the approach with one or two fly-through gaps. |
| `debriscluster` | A tight knot of 7–10 wreckage pieces near mid-lane. |
| `debrischunk` | One huge derelict hulk ringed by a few shed fragments. |

Each level lists the layouts that fit its theme, and one is picked at random
per sortie; the pieces themselves are regenerated every time. Hostiles start beyond weapon range and hold position, tracking you,
until a pilot closes to 1.5× the base Pulse Repeater range (900 units) or a
shot hits one of them; then the whole squad attacks. A distress wingman waits
the same way.

Enemy counts scale with surviving pilots. Clear every hostile to open
**Salvage**: each surviving pilot chooses exactly one personal reward using
**1 / 2 / 3**, the mouse, or **Left / Right + Enter**:

- The attack's credits, for later station purchases.
- A disclosed random gun, added to inventory for fitting at a base.
- A run-only upgrade: **+15% gun damage** or **+20% shield capacity** per
  stack, added against base values. Capacity upgrades preserve the current
  charge fraction; they are not a free recharge. Bonuses also apply to later
  weapon refits and purchased shields.

The party returns to the **map** once all surviving pilots have chosen, not
automatically to a base. Rewards cannot be rerolled or claimed twice, and dead
pilots receive no salvage. Hull damage and inventory
persist along your chosen path; only reaching another base refreshes its shop
offer and fully refills shields (normal passive regeneration still applies).
Visited nodes cannot be replayed or revisited, and disabled encounters never
block all playable paths. Completing the final attack and its reward choices
opens the victory debrief. Losing every ship opens the defeat debrief.
**New Run / Enter** starts again without reconnecting or restarting the game.
Every connected co-op pilot must confirm. It resets ships, money, inventory,
equipment, run bonuses, statistics, and the map; saved settings are retained.

The station overview has hover-highlighted **Armory (A/E)**, **Market (M)**,
**Bar (B)** and **Hangar (H)** sections. The bar's gambling is not implemented.
Each section has its own illustrated background; artwork details are in
[data/ui/station](data/ui/station/README.md).
In the market, each visit offers **one personal random gun**, costing 100–200
credits. Click **BUY** or press **Enter** to store it; buying does not equip it.
The offer stays sold until reaching a different base. Select a stored weapon
and click **SELL** or press **S** to sell one for half its catalog price,
rounded down. Fitted weapons cannot be sold. The inventory lists only owned
weapons and holds up to 64 stored weapons, in addition to fitted weapons.

Open the **Armory** to see the hollow-ship preview. Only numbered
weapon mounts are selectable. Click a mount, choose a stored weapon, compare
its combat statistics and energy cost, then click **FIT WEAPON** or press **Enter**. The previous
weapon returns to inventory. Use **Left/Right** to select mounts, **Up/Down**
to select stored weapons, and **Q/R** to rotate the preview. **Esc**
returns to station services. Purchases and fitting wait for server confirmation.
**N** opens **Shields / Energy**. Choose Balanced Mk2 (300 HP, 30 HP/s,
2 s recovery, 100 CR), Bulwark (450 HP, 20 HP/s, 5 s, 150 CR), or Flux
(150 HP, 50 HP/s, 1 s, 150 CR), then **Enter** to buy and equip. The Starter
shield has 200 HP, 20 HP/s and 2 s recovery.
Recovery begins after the last shield hit, even when depleted. Replacing a
shield has no refund; switching builds costs the full price. Run capacity
upgrades apply to every build. Purchases and fitting clear your ready state.
Press **Enter** from station services to return to the route map.
**Esc** returns from that map to the base while you are docked.

The six weapons include pulse repeaters, an autocannon, railgun, plasma mortar,
and scatter cannon. Cards show reference velocity, range, and damage against
shields, armor, and normal blocks; plasma/scatter values are shared shot/volley
totals, not damage per affected block. See the [damage reference](doc/6_weapons_and_shields_concept.md)
and [icon provenance](data/ui/weapons/README.md).

The starter ship has five weapon mounts, with two Pulse I guns fitted initially.
Pulse I is the balance baseline: 10 shield and 10 hull damage per shot, 10
energy, one shot every 0.5 s. Both guns can fire nonstop, and an identical
starter shield (200 HP) breaks after about five seconds of that fire.
Contract ships share **100 weapon energy** across their guns. Energy regenerates
at **40/s**, including while firing. Pulse I/II, autocannon, railgun, mortar,
and scatter cost respectively **10/12/8/30/25/16** per trigger; scatter pays
once for its entire volley. Heavier loadouts drain the reserve faster than it
recharges, and insufficient energy prevents firing. Shield recharge uses no weapon
energy. Flight HUD bars show both reserves and recharge timers; base visits
refill both without repairing the hull. Shield purchases and reactors are
personal, server-authoritative, and reset on New Run.
Mission enemies have orange-red brackets and an **ENEMY** label with distance
in world units (**U**). Off-screen contacts appear at the screen edge; enemies
behind you are explicitly labelled **ENEMY BEHIND**. Co-op pilots are not
marked as enemies. These indicators follow the Flight HUD setting.
**Tab** or **HIDE - TAB** hides the panel and returns the cursor to flight control;
**Tab** opens it again. **O** also restores a closed/minimized panel.
Hiding the station panel does not launch a mission: the on-screen reminder
still offers **Enter** for route/travel controls, or shows party readiness.

For two-player co-op on the same computer, run these in three terminals from
the repository root after building both executables:

```sh
./build/macos/voxellancer-server --listen-port 27801 --scenario contracts --target-players 2
./build/macos/voxellancer -multiplayer-client
./build/macos/voxellancer -multiplayer-client
```

Contracts support one to four pilots with personal credits/upgrades and shared
objectives. The station is currently a safe service menu, not a physical docking
location. Bases refill shields and energy and offer paid repairs for surviving
hull blocks. Destroyed blocks stay lost. **There are no revives**:
a destroyed pilot stays connected and spectates a surviving teammate for the
rest of the run, including base visits and later attacks. Only surviving
pilots choose rewards and confirm travel. A disconnect does not abort the
survivors' attack. Solo death or a party wipe ends the run; everyone can return
on the next fresh run. The debrief records attacks, party kills, bases, time,
and personal earned credits, salvaged weapons, and upgrades.
Progress lasts for the current run only. There are no disk saves, physical loot drops,
companions, random-encounter events, or remote/internet hosting yet.
Shop settings live in `data/config.ini` under `[contracts]`, including the
engagement rule (`baseWeapon`, `engageRangeFactor`). Attacks are authored under
`[contractlevel1]` through `[contractlevel4]` and encounters under
`[contractsignal1]` through `[contractsignal3]`; each sets its announced
`objective`, enemy `formation` (1–3), and the allowed cover `layouts`
(comma-separated). Piece generators live in `src/procedural/covergenerator.cpp`
and layout algorithms in `src/procedural/coverlayout.cpp`.

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
ships. Without a target it starts the scenario immediately, except contracts,
which default to a one-pilot lobby and accept at most four pilots. Normal server
operation prints per-client byte and visibility counters once per second.
The version 31 protocol defines generation-protected entity IDs, tick-stamped
input commands, versioned snapshot deltas with entity kind and shield/hull
fractions, uniform scale, and maximum mass, full voxel content, initial
equipment slot/key state for newly visible entities, stable ship-definition
keys, authoritative voxel damage/removal and shield-absorption events, and
mission state notifications, and lobby player counts/world-start state.
Shield-hit events include ship-local impact coordinates and projectile color
for accurate client effects.
Entity-content definition keys and equipment keys are limited to 63 bytes.
Snapshot records are 66 bytes, using a compressed quaternion, 8-bit shield and
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
`benchmark-results/`, all sharing a run ID. Client frame recording begins on
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

This option, or normal startup without a server option, starts the matching
`voxellancer-server` beside the game, selects contracts, chooses an available
loopback port, and connects asynchronously while the main menu is visible.
The server process is stopped when the game exits. Large-combat benchmarks
retain their dedicated startup and scenario.

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
Each connection receives entities within 650 world units of its assigned ship
(2200 in contracts, whose hostiles start beyond weapon range);
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
