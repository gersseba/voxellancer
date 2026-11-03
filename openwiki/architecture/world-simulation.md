---
type: architecture
title: World simulation and object lifecycle
description: Describes the World-owned simulation subsystems, ordered combat pipeline, and safe object spawning and removal.
tags: [world, simulation, lifecycle, collision, damage]
sources:
  - id: openwiki-source-11effaa43ec0e9d1206994a9
    resource: repo://src/gamestate/gameplay/gameplay.cpp
  - id: openwiki-source-8fe83e722ee9002df9b0f910
    resource: repo://src/scenarios/basescenario.cpp
  - id: openwiki-source-7baa5ea3495f51ad7d52ba5d
    resource: repo://src/world/god.cpp
  - id: openwiki-source-d822b999e031aa3b7a0868ab
    resource: repo://src/world/world.cpp
  - id: openwiki-source-16a156df6b8a42ef80dacc61
    resource: repo://src/world/worldlogic.cpp
  - id: openwiki-source-42eade458ade851bd08201c8
    resource: repo://src/worldobject/ship.cpp
  - id: openwiki-source-7d54c53d91c204a211ae0787
    resource: repo://src/worldobject/worldobject.cpp
generated: { by: "copilot", at: "2026-11-03T15:48:08.697Z" }
verified:
  - by: openwiki/0.7.2
    at: 2026-11-03T15:48:08.697Z
---

# World simulation and object lifecycle

`World` is a lazy singleton that owns the player, script engine, skybox, world
logic, spatial `WorldTree`, lifecycle manager (`God`), particle engine, faction
matrix, event poller, and mission system. Each world update records the delta,
updates the player, runs world logic, scripts, event polls, particles, missions,
and then every registered world object.

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
`WorldObject` combines a voxel cluster with physics, collision detection, content
metadata, components, and destruction state. Adding or removing a voxel updates
the physics and collision representations; losing the configured crucial voxel or
all cockpit voxels records the associated destruction state. `Ship` adds character,
board-computer, and squad logic updates.

Event polls are scriptable objects too: the event poller registers a newly added
poll with the script engine, updates its polls each frame, and drops dead polls.

## Scenario lifecycle

`GamePlay::loadScenario` stops scenario audio, clears the previous scenario,
updates the HUD view, and selects a scenario implementation. Scenario 0 is the
Lua-backed demo; IDs 1-5 select the built-in game, battle, frozen-game, pirate,
and friendly-fire scenarios, while other IDs select `BaseScenario`. On entry,
gameplay loads scenario 0.

`BaseScenario::load` attaches to the current singleton `World`, populates it,
flushes the initial scheduled spawns, and starts the world script engine. During
simulation, `WorldLogic` flushes scheduled removals and spawns at its boundaries,
so scenario population and mid-frame structural changes become registered in
controlled phases.

## Related work

Combat and structural changes are detailed in
[voxel ships and damage](../concepts/voxel-ships-and-damage.md). Lua object
registration and event APIs are described in
[Lua scripting](../integrations/lua-scripting.md).
