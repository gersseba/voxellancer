---
type: architecture
title: World simulation and object lifecycle
description: Describes the World-owned simulation subsystems, ordered combat pipeline, and safe object spawning and removal.
tags: [world, simulation, lifecycle, collision, damage]
sources:
  - id: openwiki-source-11effaa43ec0e9d1206994a9
    resource: repo://src/gamestate/gameplay/gameplay.cpp
  - id: openwiki-source-dd6600ebcef6778c27688c55
    resource: repo://src/gamestate/gameplay/running/gameplayrunning.cpp
  - id: openwiki-source-8fe83e722ee9002df9b0f910
    resource: repo://src/scenarios/basescenario.cpp
  - id: openwiki-source-4fe0c725f507a14bae578db4
    resource: repo://src/scenarios/battlescenario.cpp
  - id: openwiki-source-f3b131cfec992caceffcc7b6
    resource: repo://src/server/main.cpp
  - id: openwiki-source-5755e4066d8d2af940b39780
    resource: repo://src/simulation/simulationsession.cpp
  - id: openwiki-source-7b722948afd514b26fa2b16c
    resource: repo://src/utils/simulationtickrunner.h
  - id: openwiki-source-7baa5ea3495f51ad7d52ba5d
    resource: repo://src/world/god.cpp
  - id: openwiki-source-d822b999e031aa3b7a0868ab
    resource: repo://src/world/world.cpp
  - id: openwiki-source-16a156df6b8a42ef80dacc61
    resource: repo://src/world/worldlogic.cpp
  - id: openwiki-source-42eade458ade851bd08201c8
    resource: repo://src/worldobject/ship.cpp
  - id: openwiki-source-21c13e08029057e33ca84ebd
    resource: repo://src/worldobject/ship.h
  - id: openwiki-source-7d54c53d91c204a211ae0787
    resource: repo://src/worldobject/worldobject.cpp
  - id: openwiki-source-3742844d97ced7569ebb10f5
    resource: repo://test/server/testtcpserver.cpp
  - id: openwiki-source-ae23635257bfb1829e540daa
    resource: repo://test/simulation/testsimulationsession.cpp
generated: { by: "copilot", at: "2026-11-19T09:46:20.407Z" }
verified:
  - by: openwiki/0.7.2
    at: 2026-11-19T09:46:20.407Z
---

# World simulation and object lifecycle

`World` owns shared simulation services including `WorldLogic`, the spatial
`WorldTree`, lifecycle manager (`God`), script engine, faction matrix, event
poller, and mission system. The graphical client world also owns the player,
skybox, and particle engine; server mode has no client player or presentation
services. `World::instance(mode)` enforces that the active singleton keeps its
original client/server mode, while tests and tools can construct a `World`
directly.

`World::update` increments the simulation tick, optionally updates the client
player, then runs world logic, scripts, event polls, optional particles,
missions, and registered world objects. The order is shared by client and server
simulation, with presentation-only updates omitted in the headless server build.
Calling `World::clear` destroys its objects and rebuilds the world's simulation
services in place; it does not replace the global `World` singleton.

## Fixed-step and execution ownership

`SimulationSession` wraps a `World` with `SimulationTickRunner`. It advances at
30 Hz, applies queued input at tick boundaries, and runs at most five catch-up
ticks per advance; unprocessed time remains in the runner's accumulator. The
standalone server advances the session and polls `TcpServer` from the same
loop. The graphical running state advances its local session only when remote
preview is inactive; remote preview polls the TCP client and displays
replicated state instead. Render-frame timing therefore does not define the
authoritative simulation cadence. See
[client-server simulation](client-server-simulation.md) for wire and
replication details.

## Ordered mechanics pipeline

`WorldLogic` brackets its mechanics with lifecycle removal and spawning. Between
those boundaries it moves objects, accumulates voxel collisions, runs collision
hooks, derives damage and impulse impacts, applies impulse, and drains a
damage-forwarding loop. That loop removes destroyed voxels and forwards remaining
damage until no impacts remain. It then detects and splits disconnected voxel
clusters, schedules split-off objects, detects wrecks, and schedules wreckage and
garbage removals. The ordering is significant: structural changes follow damage,
and scheduled objects are made visible only at the lifecycle boundaries.

## Object registration and ownership

`God` queues spawn requests and removals. Spawning inserts an object into the
world tree, rejects it when collision detection finds an overlap, and otherwise
marks it spawned and registers it with `World`. Removal detaches its spatial geode,
unregisters it, and deletes it. A scheduled removal is de-duplicated and records
the `RemovalScheduled` state.

`World` maintains all world objects and a ship subset. It registers ships with
the script engine on addition and unregisters every object when removed. A
spawned object receives a generation-protected entity ID; removal releases that
ID so later reuse of its index cannot identify an older object. A
`WorldObject` combines a voxel cluster with physics, collision detection, content
metadata, components, and destruction state. Adding or removing a voxel updates
the physics and collision representations; losing the configured crucial voxel
or all cockpit voxels records the associated destruction state. `Ship` adds
character, board-computer, and squad logic. Its board computer updates each
simulation tick, while its character AI runs every six ticks (5 Hz at the
standard 30 Hz simulation rate) at a per-ship phase assigned when the ship
joins the world.

Event polls are scriptable objects too: the event poller registers a newly added
poll with the script engine, updates its polls each frame, and drops dead polls.

## Scenario lifecycle

`GamePlay::loadScenario` stops scenario audio, clears the previous scenario,
updates the HUD view, and selects a scenario implementation. Scenario 0 is the
Lua-backed demo; other IDs select built-in gameplay or benchmark scenarios,
with unknown IDs falling back to `BaseScenario`. On entry, gameplay loads its
configured initial scenario. Clearing a `BaseScenario` calls `World::clear`,
which removes that world's objects and reconstructs its simulation services
without deleting the global client world.

`BaseScenario` stores the `World` passed to its constructor. Loading populates
that world, flushes initial scheduled spawns, and starts its script engine.
During simulation, `WorldLogic` flushes scheduled removals and spawns at its
boundaries, so scenario population and mid-frame structural changes become
registered in controlled phases. The standalone battle server uses the
scenario's player ship as the preferred client actor, and both AI fleets include
that ship as a target even without client presentation.

Focused checks for tick accumulation, input-at-tick behavior, and generation
protected IDs are in `test/simulation`; battle actor assignment and server-mode
behavior are covered in `test/server`.

## Related work

Combat and structural changes are detailed in
[voxel ships and damage](../concepts/voxel-ships-and-damage.md). Lua object
registration and event APIs are described in
[Lua scripting](../integrations/lua-scripting.md). Application startup and
rendering ownership are described in [runtime](runtime.md).
