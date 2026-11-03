---
type: integration
title: Lua scenarios and mission scripting
description: Describes the Lua 5.5 script lifecycle, gameplay bindings, scenario loading, scriptable world entities, and native-value conversions.
tags: [lua, scripting, scenarios, missions, bindings]
verified:
  - by: openwiki/0.7.2
    at: 2026-11-03T15:48:08.697Z
sources:
  - id: openwiki-source-d44494ef3e497fea81240ef8
    resource: repo://CMakeLists.txt
  - id: openwiki-source-81c3e3938ad438525c63890f
    resource: repo://src/missions/missionsystem.cpp
  - id: openwiki-source-f0026bdbc7784ba60651ccd6
    resource: repo://src/scenarios/scriptedscenario.cpp
  - id: openwiki-source-45895c74eff883b308c7390a
    resource: repo://src/scripting/elematelua/luawrapperfunction.cpp
  - id: openwiki-source-66f9bd6a661e5ccf8ede8aa9
    resource: repo://src/scripting/gameplayscript.cpp
  - id: openwiki-source-c96022c1530e4d99a0b30fb4
    resource: repo://src/scripting/script.cpp
  - id: openwiki-source-1e98e39d6f94af77288526a1
    resource: repo://test/scripting/testscripting.cpp
  - id: openwiki-source-6e5338326aab57aa16d18aaa
    resource: repo://test/scripting/testvectorextension.cpp
generated: { by: "copilot", at: "2026-11-03T15:48:08.697Z" }
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

`GamePlay::loadScenario(0)` selects `ScriptedScenario` with
`data/scripts/scenarios/demo.lua`. The scripted scenario constructs and loads a
`GamePlayScript`, creates the player's `mox` ship, schedules it for world spawn,
assigns it to the player, and adds the script to the world script engine. Other
scenario scripts reside under `data/scripts/scenarios`; mission scripts reside
under `data/scripts/missions`.

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

`MissionSystem` owns active missions, starts them when added, updates them each
world frame, and drops missions once they are no longer running. Removing a
mission also unregisters its scriptable identity. Keep Lua-exposed object lifetime
changes aligned with these registration paths; see
[world simulation](../architecture/world-simulation.md).
