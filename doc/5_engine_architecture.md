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

### Standalone graphical game

Run `voxellancer.exe` without a server option to run the game's local
simulation. The graphical process loads game properties and assets, creates
the window and `Game`, and advances its `SimulationSession` from gameplay
updates.

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

`voxellancer.exe -local-singleplayer` still uses two processes internally, but
starts and owns the server automatically. Before loading the client properties
or initializing graphics, the game briefly binds an ephemeral loopback port to
choose a port, starts the matching sibling `voxellancer-server.exe` with
`--listen --listen-port <port> --scenario battle`, and retries the client
connection for up to ten seconds. It then initializes the graphical client
and attaches the already-connected `TcpClient`.

The client also loads the battle scenario locally to provide its existing
gameplay and camera presentation. While remote preview is active, it does not
advance that local simulation or draw its local world objects. It uses the
assigned remote ship to position the camera, draws remote replicas, and keeps
client-side effects and HUD active. The server's battle scenario is
authoritative; the server prefers its designated player ship when assigning
the first connecting client. The game stops the child server when it exits.

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
version 18 and a message-type byte. There is no version negotiation: peers with
different protocol versions reject each other's packets.

On connection, the server assigns a generation-protected entity ID for a ship,
sends that actor ID, then sends an initial snapshot and the content for newly
visible entities. Runtime pointers and `Handle<T>` values are process-local
and are never serialized.

The main message families are:

| Direction | Data |
|---|---|
| Client to server | Sequenced, tick-stamped input commands and snapshot-resync requests. |
| Server to client | Lobby player-count/world-start status; actor assignment; snapshot deltas and input acknowledgements; full entity voxel/equipment content and equipment-only updates; voxel damage/removal, shield-hit, weapon-fired, and mission-state events. |

Input carries quantized movement and aim axes, button state, a target entity
where applicable, a monotonically increasing sequence, and a requested
simulation tick. The server checks the assigned actor and command sequence,
tick range, and queue bounds. It acknowledges the result and applies accepted
state at a simulation tick boundary; it never accepts client transforms, hits,
or damage as authoritative facts.

Snapshots are emitted every two simulation ticks (15 Hz). Each client has a
baseline and receives entity additions/changes/removals relative to that
baseline. The server filters snapshots to entities within a 650-unit radius of
the controlled actor, while always including that actor. A snapshot entity
record is 65 bytes and carries a generation-safe ID, kind, transform,
velocities, shield/hull fractions, scale, maximum mass, and engine state.

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
bounds. Active shields intercept projectiles, absorb collision damage before it
reaches hull voxels, and permanently deplete after their configured hit capacity
(five by default). The shield showcase sets all three shields to 100 hits.
Active shields emit faint, shape-specific cyan shimmer particles while idle;
shield-hit events create expanding cyan ripples in both local play and remote
previews. The shield showcase scenario (F7) places the player in a fighter
between two friendly cruisers, one using each shield shape.

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
