# Engine and client/server architecture

This document describes the current implementation, including its limitations.
The networking path is a local TCP prototype, not a production internet
multiplayer service.

## Executables and ownership

| Target | Responsibility |
|---|---|
| `voxellancer` | Graphical game: OpenGL, input, HUD, audio, presentation, and local or remote-preview gameplay. |
| `voxellancer-server` | Headless authoritative simulation and optional loopback TCP listener. |
| `voxellancer-client` | Headless transport/integration probe; it is not the graphical game client. |
| `voxellancer-protocol` | Shared versioned packet encoding, entity identity, snapshots, inputs, and events. |

`SimulationSession` advances a `World` without owning a renderer. The server
build compiles out client-only scene, device-input, audio, and GPU voxel-render
code, while reusing the authoritative world, AI, collision, equipment,
scenario, mission, and script systems. This is a build boundary around shared
simulation code, not a complete data-model split: `WorldObject` and voxel
content still serve simulation and client presentation roles in the graphical
build.

## Startup paths

### Main menu and graphical startup

Normal startup opens `MainMenu` and automatically owns a local contracts
server. `-multiplayer-client` uses the same menu while connecting to port
27801 instead. Socket connection attempts run in a worker; ownership of the
connected `TcpClient` transfers to the graphical thread through a future.
All polling, prediction, input, and presentation remain on the graphical
thread. Closing during setup cancels further connection attempts and joins
the worker before stopping the owned server.

`MainMenuState` enables Start only for a ready, not-yet-entered session, and
Resume only for a ready active session. Readiness requires lobby world-start,
the assigned actor snapshot, and contract status, not merely a TCP connection.
Disconnects return to the menu. Settings remains accessible while disconnected.
Menu input suppresses gameplay device polling and sends neutral remote input;
it never pauses the authoritative simulation. Start enters the prepared station
session; it does not deploy until the party confirms a destination on the map.

The client loads an empty local scenario and one non-simulated presentation
ship for the existing camera/HUD attachment. Server snapshots move that ship;
remote replicas supply visible world geometry. Developer scenario switching
and the old P pause are disabled in this menu-driven remote session.

`UiFont` renders the generated Voxellancer Flight atlas in menu, HUD, loading,
and intro screens. It preserves case, batches strings as triangles, restores
the surrounding GL state, and releases context-bound resources on rebuild.
The checked atlas is 8x10 cells; retained `FontSize` enums are layout metrics,
not voxel geometry. No runtime font dependency was added.

### Separate local server and preview

Start the server and then the graphical game with `-server-preview`:

```powershell
.\build\agent-vcpkg-vs18-stable\Release\voxellancer-server.exe --listen --listen-port 27800
.\build\agent-vcpkg-vs18-stable\Release\voxellancer.exe -server-preview
```

The headless probe can connect instead of the graphical game:

```powershell
.\build\agent-vcpkg-vs18-stable\Release\voxellancer-client.exe --port 27800
```

Both current client paths connect to loopback; the server listener also binds
to loopback.

### One-command local singleplayer

`voxellancer.exe -local-singleplayer` uses the main-menu startup described
above. It chooses an ephemeral loopback port, starts the matching sibling
`voxellancer-server.exe` with `--scenario contracts`, and retries connection
for up to ten seconds without blocking the menu. Failures are logged and shown
in the bottom status line. The game stops the child server when it exits.
Large-combat benchmarks retain their synchronous pre-graphics connection path,
benchmark scenario, and reporting behavior; saved menu preferences are not
loaded in benchmark mode.

### Multiplayer lobby and player binding

Run `voxellancer-server --listen-port 27800 --target-players 2`, then connect
two graphical clients with `-server-preview`. With a target-player count set,
the server constructs an empty `World` and polls only the listener while
waiting; it does not load/populate the scenario or advance the simulation.
Clients receive lobby status packets and show `WAITING FOR PLAYERS n/2`.
Once full, the server creates the selected scenario, creates enough player
ships, assigns one unique ship entity to each connection, sends the world-start
status and initial snapshots, and begins its normal simulation loop. The
default scenario in lobby mode is battle; explicitly choosing
`--scenario benchmark` selects the seeded fleet scenario. Non-lobby standalone
and `--listen` paths retain the previous immediate scenario setup.

## Server simulation loop

The server loads the shared configuration, voxel definitions, world-object
properties, equipment, and projectiles, then creates a server-mode `World`, a
`SimulationSession`, and the selected scenario. Without `--listen`, it advances
an exact number of ticks for a headless benchmark and prints tick-time
percentiles, missed deadlines, entity counts, and backlog as JSON. With
`--listen`, it binds a non-blocking TCP listener and repeatedly:

1. measures elapsed time using a monotonic clock;
2. advances the simulation session;
3. accepts connections, receives commands, and emits acknowledgements, events,
   and snapshots;
4. sleeps briefly before the next iteration.

In lobby mode, listener acceptance and lobby status delivery run before any
scenario population; `SimulationSession::advance` is not called until the
target player count has connected and actor assignment is complete.
`SimulationTickRunner` advances authoritative state at 30 Hz. It runs at most
five catch-up ticks per `advance` call and retains any remaining accumulator as
backlog rather than skipping simulation time. The server loop currently runs
simulation and network polling on the same thread.

## TCP protocol and data flow

The transport is an SFML TCP stream. TCP provides reliable, ordered delivery;
there is no separate unreliable snapshot channel. Each application frame has a
four-byte little-endian payload length followed by a packet with protocol
version 29 and a message-type byte. There is no version negotiation: peers with
different protocol versions reject each other's packets.

On connection, the server assigns a generation-protected entity ID for a ship,
sends that actor ID, then sends an initial snapshot and the content for newly
visible entities. Runtime pointers and `Handle<T>` values are process-local
and are never serialized.

The main message families are:

| Direction | Data |
|---|---|
| Client to server | Sequenced, tick-stamped input commands, snapshot-resync requests, and contract actions (ready, purchases, personal invulnerability preference). |
| Server to client | Lobby player-count/world-start status; actor assignment; snapshot deltas and input acknowledgements; full entity voxel/equipment content and equipment-only updates; voxel damage/removal, shield-hit, weapon-fired, mission-state events, and personal contract status. |

Contract actions are 22 bytes, including the selected catalog weapon and mount
cell for fitting, a route-node byte (255 outside node selection), and a reward
choice byte (zero outside reward claims). Byte 21 selects a shield build for
BuyShield (1--3; zero retains the default Mk2 purchase); other actions require
zero there. New Run is a separate action.
Contract status is 399 bytes: the original 40-byte prefix
(including invulnerability at byte 39), personal offer ID, six inventory stack
counts, mount count, reserved byte, and 32 padded four-byte mount records,
followed at byte 183 by current/selected node, visited-node mask, node count,
and 17 padded eight-byte route records (kind, level, row, lane, outgoing mask).
A 40-byte section at byte 326 carries the run number, view entity, frozen summary duration,
earned credits, attack/kill/base/weapon counts, personal upgrade stacks and
reward offers, alive/claimed flags, and living/pending-reward counts.
The final 33 bytes, starting at byte 366, carry shield-build ID followed by eight
floats: current/capacity/regeneration/recharge-wait for shields, then the same
four weapon-energy values. Decoding rejects non-finite/negative resources and
reserves above capacity. These are personal authoritative HUD values, not
client-simulated resources; entity snapshots retain their existing shield fraction.
Inventory is bounded to 64 stored guns; at most 32 mounts are replicated.
Decoding rejects invalid weapon IDs, duplicate mount cells, excessive inventory,
backward/cyclic route edges, invalid selections, and nonzero reserved/padding
bytes. Input remains 52 bytes and entity snapshots
66 bytes. Both peers must be rebuilt together.
The graphical client loads
`general.playerShipUndestroyable` from saved settings and synchronizes it on
connection and when changed. ContractRun applies protection only to the
connection's assigned pilot. These sequenced preference actions are valid at
the station and during missions, even across a mission-number transition.
Damager skips both shield and voxel damage for protected objects; enemies
remain damageable. Protection defaults on for this prototype and is not an
anti-cheat policy for competitive play.

Input carries quantized movement and aim axes, button state, a target entity
where applicable, a monotonically increasing sequence, and a requested
simulation tick. The server checks the assigned actor and command sequence,
tick range, and queue bounds. It acknowledges the result and applies accepted
state at a simulation tick boundary; it never accepts client transforms, hits,
or damage as authoritative facts.

The 52-byte input command separates ship steering (`aimX`, `aimY`, `roll`)
from primary gun convergence (`firePoint`, three floats at bytes 40--51).
The fire point is an offset in ship-relative axes, measured in world units
independently of mesh scale. The client sends changed aim while firing, even
without a new snapshot. For manual fire, the server transforms the point using
its current ship pose. When lined up with the lead marker, the client instead
sets `FireWithLead` alongside `FirePrimary` and sends the cue's generational
target ID. The server resolves that target and computes one intercept per gun
from current simulation state; missing or ineligible targets do not fall back
to manual fire. Hardpoint aiming limits, cooldowns, line-of-fire checks, and
collisions remain authoritative.
Non-finite, zero, and more-than-10,000-unit fire points are rejected.

`WorldObjectComponents` owns the shared weapon-energy reserve. Ship properties
`weaponEnergyCapacity`, `weaponEnergyRegeneration`, and `weaponEnergyDelay`
opt into a reactor; absent/zero capacity retains legacy unlimited firing.
Contract pilots opt in; NPCs and existing benchmark/scenario ships do not.
Weapons load optional `general.energyCost` (legacy default zero), check
affordability before spawning, and spend once per successful trigger, including
a complete scatter volley. Reactor-equipped ships rotate starting hardpoints
after energy-spending requests to avoid starving later mounts, even across
empty firing attempts. Input is consumed before
world updates on the authoritative 30 Hz tick; recharge accounts for the
portion of an update remaining after the recovery delay.

Shield properties optionally define `general.rechargeDelay` (default zero).
Positive absorbed damage restarts it; continuous-HP shields recover after
depletion, while legacy registered-hit shields preserve their depletion rule.
Explicit `shieldslotN.equipped` chooses a validated mountable default; omitting
it preserves the prior first-mountable fallback. This prevents the expanded
contract catalog from changing the starting shield through sorting.
ContractRun validates authored shield/weapon catalogs, applies personal capacity
bonuses, and refills both reserves at bases without reviving dead pilots.
`StationOutfitter` renders the shield-build shop and independent flight meters;
neither presentation nor client firing requests can grant resources.

Snapshots are emitted every two simulation ticks (15 Hz). Each client has a
baseline and receives entity additions/changes/removals relative to that
baseline. The server filters snapshots to entities within a 650-unit radius of
the controlled actor, while always including that actor. A snapshot entity
record is 66 bytes and carries a generation-safe ID, kind, transform,
velocities, shield/hull fractions, scale, maximum mass, and engine state.
The final byte is a canonical hostile-contact boolean. For contracts,
TcpServer derives it from ContractRun's authoritative enemy IDs, never from a
client-side asset-name guess or "every other ship is hostile". GamePlayScene
projects those contacts into enemy brackets/labels or screen-edge cues without
creating local simulation ships. Removal/interest filtering removes their
markers with the corresponding replica. This is contract identification,
not a general replicated faction/target-lock system.

Newly visible entities receive a separate content packet with a stable
definition key, voxel coordinates/color/density/current and maximum HP, and
equipment slot kind/group/cell/key. Later equipment changes use equipment-only
updates. Damage and removal events update the client's existing voxel
baseline; shield-hit events include ship-local impact coordinates and
projectile color for accurate presentation. Weapon and mission events carry
their authoritative event data for presentation. If a client cannot apply a
snapshot delta against its baseline, it requests a full resynchronization. The
server resets that client's baseline and sends a fresh full snapshot and
content.

The implementation applies explicit bounds to clients, frame sizes,
replicated voxel/equipment state, event queues, and outgoing data. The server
currently accepts at most 16 clients. These are prototype limits, not tuned
deployment capacities.

## Client prediction and presentation

The graphical `GamePlayRunning` state polls the `TcpClient` once per rendered
update. It applies snapshot deltas and queues, decodes, and validates events.
It buffers input acknowledgements and server event streams for gameplay
presentation.

Remote entities are sampled between snapshots using position/velocity
interpolation and quaternion interpolation. The render timeline is buffered a
few ticks behind the newest snapshot; when it runs beyond the latest snapshot,
the interpolator extrapolates position and orientation for at most three ticks.
The controlled ship is predicted separately for up to eight ticks by replaying
pending inputs from the newest authoritative snapshot using replicated engine
power, mass, and local damping properties. Acknowledgements retire or reject
pending commands, and each new authoritative snapshot resets the prediction
base. This lightweight predictor does not simulate collisions or declare
combat outcomes.

`GamePlayScene` constructs render-only voxel proxies from replicated content,
updates their transforms and voxel HP, and resolves engine/weapon visuals from
the local equipment definitions. Server events drive bounded presentation
effects. These proxies and effects do not change authoritative simulation
state.

Each ship's `[general]` INI section selects `shield_type=HULL`, `SPHERE`, or
`NONE`. Hull shields cover exposed voxels; sphere shields enclose the ship's
bounds. Active shields intercept projectiles and absorb collision damage before
it reaches hull voxels. Profiled weapons spend shield HP continuously; legacy
unprofiled shots use the configured hit capacity (five by default) and permanently
deplete it. The shield showcase sets all three shields to 100 legacy hits.
Active shields emit faint, shape-specific cyan shimmer particles while idle;
shield-hit events create expanding cyan ripples in both local play and remote
previews. The shield showcase scenario (F7) places the player in a fighter
between two friendly cruisers, one using each shield shape.

### Profiled weapons and station outfitting

The six catalog weapons bind one projectile INI to each gun. `WeaponProfile`
loads speed, range, material-specific HP budgets, mass, penetration, and splash/
pellet bounds. `Gun` emits deterministic scatter volleys, inherits launch-ship
velocity, and applies physical recoil. Optional hardpoint-group `.equipped`
chooses the initial compatible weapon; omitted values retain legacy selection.
Armor uses voxel prefix `0x000A`, 150 HP, and density 3; its server-side material
flag is copied when voxels split. Clients need only replicated appearance/HP.

`WorldLogic` resolves legacy movement/collisions first, then `Mover` sweeps
profiled projectiles in stable entity order. These shots bypass legacy discrete
projectile collisions. Exact voxel contacts and shields spend one continuous
budget; shield overflow reaches hull once. Physical impacts use reference-speed
calibration once, transfer momentum, and resweep the remaining tick after slowing.
Traversal is bounded to six penetrating voxels, twelve exposed plasma recipients,
eight scatter pellets, and 32 physical sweep steps per projectile per tick. Plasma snapshots
eligible surfaces before removing voxels. Resolved impacts reuse `Damager` and
the existing removal/splitting/replication lifecycle without legacy neighbor
propagation. Unprofiled weapons retain fixed-hit legacy shield behavior.
See the [weapon reference](6_weapons_and_shields_concept.md) for values and limits.

`ContractRun` owns personal credits, one random offer per station visit, inventory,
and mount swaps. Purchases never equip automatically. Fitting validates the
owning actor, phase, sequence, inventory, and exact mount cell before replacing
equipment and returning the previous gun to storage. Credits, inventory, and
equipment and route progress remain session-only. `StationOutfitter` presents replicated ship
geometry as a hollow preview, exposes only mount selection, and waits for the
submitted action's acknowledgement before unlocking purchase/fitting controls.
It never changes authoritative equipment optimistically.

### Branching sector route

`ContractRun` generates a 17-node forward-only graph: a starting base, five
three-lane rows, and a final attack. Intermediate attack variants and the
base/signal lanes vary per run. Adjacent-lane connections branch and merge;
each usable node retains a playable continuation despite disabled encounters.
All pilots share the graph, destination, and visited mask.

Station, Mission, Reward, Route, Complete, and Defeat are distinct authoritative phases.
`SelectNode` accepts only an implemented, connected destination; choosing
a different node clears every pilot's ready flag. The historical `mission`
field is now a monotonic route revision, advanced on changed selections,
travel, and mission resolution. This prevents a delayed ready action from
confirming a different destination. Rejected new-sequence actions still receive
an acknowledgement so a stale choice cannot leave the UI locked. Protection preferences remain independent
of that revision. All surviving pilots must ready before travel; shopping/fitting is
restricted to Station. Player movement/firing is disabled outside Mission on
both server and presentation client.

`ContractLevel` loads the three attack definitions from `[contractlevel1..3]`
in the existing configuration. Their ship definitions, per-pilot enemy counts,
reward, formation/layout, and debris count drive deployment. Patrols use
light pulse ships; crossfire uses autocannon/scatter ships; blockades use
shielded pulse/plasma ships. Destructible voxel barriers use the normal world
damage/replication lifecycle. Only enemy IDs count toward victory.

Winning removes encounter entities, marks the selected node visited, and enters
Reward. Each living pilot claims exactly one personal credit, gun, or run-upgrade
offer; unchosen options are forfeited. Offers stay fixed, rejected requests are
acknowledged, full inventories cannot claim guns, and a claimed reward cannot
be paid again. Only after every living pilot chooses does the party enter Route,
or Complete after the terminal attack. It does not reroll
the shop or fully refill shields. Entering a Base node does both and opens
station services; paid repair restores HP on surviving blocks without restoring
destroyed hardware. The server quotes and charges one credit per 100 missing HP,
rounded up, and refreshes the repaired ship's replicated voxel content.
Payload upgrades scale each newly fired
projectile's finite impact budget; shields derive upgraded capacity from their
authored base HP without compounding, retaining their current charge fraction.
The two upgrade offers alternate after a random first offer. Bonuses are personal,
bounded to six stacks per type, and never mutate shared equipment properties.

TCP connection actor IDs remain stable participant identities. `ContractRun`
separately owns each participant's current ship. Inputs and acknowledgements
are routed through that ownership; clients cannot control the teammate they
spectate. Losing one ship keeps its connection alive and changes its view
entity to a survivor. Dead pilots cannot shop, fit, claim salvage, or confirm
travel, and base visits never respawn them. Losing every ship enters Defeat;
disconnecting a pilot removes only their ship and does not abort survivors.
Snapshot interest follows the view entity, or the origin when none remain;
an established empty world is not treated as an incomplete connection.

Complete/Defeat freeze summary statistics and allow unanimous New Run votes,
including dead pilots. Restart removes the old world and creates fresh ships
with new entity generations, inventory, economy, bonuses, stats, and graph,
preserving only connections, protection preferences, and monotonically
increasing action sequences/route revisions. Camera ownership changes clear
pending client prediction state; spectators never predict another pilot's inputs.
Signal nodes offer safe salvage or an authored combat encounter. There
is no event resolver or cross-restart save system yet.

## Remaining boundaries and validation

- The graphical game still constructs a local `World` and `Player`; remote
  preview pauses local simulation and uses the local player as a camera anchor.
  A larger refactor could separate camera/controller state from `World` and
  avoid constructing a duplicate local scenario for local singleplayer.
- `WorldObject`/voxel structures still combine simulation and presentation
  concerns in the client build. Further separation should preserve the current
  headless server build and proceed behind focused tests.
- Networking is loopback-only, unauthenticated, unencrypted, and TCP-based.
  Remote hosting requires a deliberate transport, security, reconnect, and
  overload design; this prototype should not be exposed as an internet server.
- The large-combat benchmark records server simulation, snapshot/network work,
  and per-client rendering/replication separately. These development-machine
  measurements are diagnostic leads, not target-hardware capacity guarantees.
