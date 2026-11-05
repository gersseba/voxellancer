---
type: integration
title: Lua scenarios and mission scripting
description: Describes the Lua 5.5 script lifecycle, gameplay bindings, scenario loading, scriptable world entities, and native-value conversions.
tags: [lua, scripting, scenarios, missions, bindings]
sources:
  - id: openwiki-source-d44494ef3e497fea81240ef8
    resource: repo://CMakeLists.txt
  - id: openwiki-source-11effaa43ec0e9d1206994a9
    resource: repo://src/gamestate/gameplay/gameplay.cpp
  - id: openwiki-source-3d8711b5a7a837fda632f7ec
    resource: repo://src/missions/mission.cpp
  - id: openwiki-source-81c3e3938ad438525c63890f
    resource: repo://src/missions/missionsystem.cpp
  - id: openwiki-source-8fe83e722ee9002df9b0f910
    resource: repo://src/scenarios/basescenario.cpp
  - id: openwiki-source-f0026bdbc7784ba60651ccd6
    resource: repo://src/scenarios/scriptedscenario.cpp
  - id: openwiki-source-45895c74eff883b308c7390a
    resource: repo://src/scripting/elematelua/luawrapperfunction.cpp
  - id: openwiki-source-66f9bd6a661e5ccf8ede8aa9
    resource: repo://src/scripting/gameplayscript.cpp
  - id: openwiki-source-c96022c1530e4d99a0b30fb4
    resource: repo://src/scripting/script.cpp
  - id: openwiki-source-bd9ee3e5f4c4b40eba3d2469
    resource: repo://src/server/tcpserver.cpp
  - id: openwiki-source-d822b999e031aa3b7a0868ab
    resource: repo://src/world/world.cpp
  - id: openwiki-source-1e98e39d6f94af77288526a1
    resource: repo://test/scripting/testscripting.cpp
  - id: openwiki-source-6e5338326aab57aa16d18aaa
    resource: repo://test/scripting/testvectorextension.cpp
generated: { by: "copilot", at: "2026-11-05T22:43:09.174Z" }
verified:
  - by: openwiki/0.7.2
    at: 2026-11-05T22:43:09.174Z
---

# Lua scenarios and mission scripting

Scripts are C++ objects backed by `LuaWrapper`. Construction registers
`setDebugStatus` and loads the shared `data/scripts/vec3.lua` helper. A script
starts by calling Lua `main`, moves from `Idle` to `Running`, and calls Lua
`update(deltaSec)` only when that function exists. The script engine starts newly
added scripts immediately when already running, otherwise starts idle scripts on
its own start; it updates running scripts and removes stopped scripts.

The runtime links Lua 5.5. Native gameplay functions use the `Luaw` wrapper to
convert their arguments and return values between C++ and Lua.

## Scenario startup

`GamePlay::loadScenario(0)` selects the graphical demo `ScriptedScenario` with
`data/scripts/scenarios/demo.lua`. `BaseScenario::load` populates the world,
spawns scheduled objects, and starts that world's script engine. The scripted
scenario creates and schedules the `mox` ship, assigns it as the player ship only
when the world has presentation, then loads a `GamePlayScript` into that same
world's script engine. The demo's player assignment is therefore not a
requirement for headless script execution. Other scenario scripts reside under
`data/scripts/scenarios`; mission scripts reside under `data/scripts/missions`.

## Bindings and script identity

`GamePlayScript` installs common, world-object, AI, squad, and external-mission
binding groups. The bindings receive both the Lua wrapper and the script engine,
which provides the bridge between Lua-facing APIs and world state.

The script engine assigns increasing keys to registered `Scriptable` objects and
sets their key; lookup rejects nonpositive, future, and removed keys. `World`
registers ships, and the event-poller and mission system register their respective
objects, so their bindings can refer to runtime objects by key. `GamePlayScript`
tracks locally created scriptables and event polls: destruction removes its live
polls and unregisters still-valid local keys.

## Server execution and network boundary

The script engine belongs to a `World`, and `World::update` advances it as part of
the world update. A gameplay script constructed with a server world's engine
therefore resolves gameplay APIs against that server world; a scripting test
checks that spawned ships and callbacks stay in the owning server world rather
than the global client world. A headless server world has no client presentation,
but its simulation can still run scripts attached to it.

Lua does not provide the network transport or automatically replicate arbitrary
Lua state. Replication is explicit C++ behavior: when enabled, `MissionSystem`
records mission state events, and `TcpServer` drains and sends those events to
connected clients. This transports the mission key, state, and simulation tick;
it does not transfer script memory or grant authority to a client.

## Native values and tests

The function wrapper checks unsigned arguments as Lua integers, rejecting
negative values and values outside the destination C++ type's range. An
`unsigned long` result is pushed as a Lua integer when representable; larger
values are pushed as a Lua number, so scripts should not assume exact
integer-valued round trips for that larger range. Floating-point values use
Lua's number type.

For these bound functions, `glm::vec3` values use a Lua table with named `x`,
`y`, and `z` fields on both input and output. Bandit tests in
`test/scripting/testvectorextension.cpp` exercise vector construction and
arithmetic, while `test/scripting/testscripting.cpp` exercises gameplay
bindings, world-object access, and callbacks.

## Missions and cleanup

Each `Mission` loads a `MissionScript` into its world's script engine.
`MissionSystem` registers and starts missions when added, updates them each world
frame, and drops missions once they are no longer running. Explicit removal also
unregisters the mission's scriptable identity. Mission start, success, and failure
are recorded for replication only when the server enables mission-state events;
keep Lua-exposed object lifetime changes aligned with these registration paths.
See [world simulation](../architecture/world-simulation.md).
