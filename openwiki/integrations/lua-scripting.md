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
  - id: openwiki-source-db6b6b178fe762317f2dc7ec
    resource: repo://src/scripting/elematelua/luawrapper.cpp
  - id: openwiki-source-45895c74eff883b308c7390a
    resource: repo://src/scripting/elematelua/luawrapperfunction.cpp
  - id: openwiki-source-66f9bd6a661e5ccf8ede8aa9
    resource: repo://src/scripting/gameplayscript.cpp
  - id: openwiki-source-c96022c1530e4d99a0b30fb4
    resource: repo://src/scripting/script.cpp
  - id: openwiki-source-4e1f6bcadcb43ac7f9ec748d
    resource: repo://src/scripting/scriptcallbackimpl.inl
  - id: openwiki-source-d87fc7d6ebd760a1eb6527c8
    resource: repo://src/scripting/scriptengine.cpp
  - id: openwiki-source-bd9ee3e5f4c4b40eba3d2469
    resource: repo://src/server/tcpserver.cpp
  - id: openwiki-source-d822b999e031aa3b7a0868ab
    resource: repo://src/world/world.cpp
  - id: openwiki-source-1e98e39d6f94af77288526a1
    resource: repo://test/scripting/testscripting.cpp
  - id: openwiki-source-6e5338326aab57aa16d18aaa
    resource: repo://test/scripting/testvectorextension.cpp
  - id: openwiki-source-3742844d97ced7569ebb10f5
    resource: repo://test/server/testtcpserver.cpp
generated: { by: "copilot", at: "2026-11-19T09:46:20.407Z" }
verified:
  - by: openwiki/0.7.2
    at: 2026-11-19T09:46:20.407Z
---

# Lua scenarios and mission scripting

Each C++ `Script` owns a `LuaWrapper` and its own Lua state. Construction
registers `setDebugStatus` and loads `data/scripts/vec3.lua`. `Script::start`
calls Lua `main` and changes the script from `Idle` to `Running`; `update`
dispatches `update(deltaSec)` only when that Lua function exists, and `stop`
marks the script `Stopped`. The `ScriptEngine` attached to a `World` starts
idle scripts when started, starts newly added scripts immediately if it is
already running, updates running scripts, and removes stopped scripts. Stopping
the engine pauses dispatch without changing each script's state; starting it
again resumes already-running scripts without calling `main` again.

The runtime links Lua 5.5. Native gameplay functions use the `Luaw` wrapper to
convert their arguments and return values between C++ and Lua. Lua load and call
errors are logged by `LuaWrapper` through the engine logger.

## Scenario startup

`GamePlay::loadScenario(0)` selects the demo `ScriptedScenario` with
`data/scripts/scenarios/demo.lua`. `BaseScenario::load` populates the world,
spawns scheduled objects, then starts that world's script engine. The scripted
scenario schedules the `mox` ship, assigns it as the player ship only when the
world has presentation, and loads a `GamePlayScript` into that same world's
engine. Scripts can run in headless worlds, but presentation-dependent APIs
need to handle the absence of a player ship and UI. Other scenario scripts
reside under `data/scripts/scenarios`; mission scripts reside under
`data/scripts/missions`.

## Bindings and script identity

`GamePlayScript` installs common, world-object, AI, squad, and external-mission
binding groups against its `ScriptEngine`. These expose timers and event
activation, world-object creation and transforms, AI tasks and factions, squads,
and mission start/result callbacks. `MissionScript` adds mission-only
operations such as `missionSucceed`, `missionFail`, and mission messages.
For example, `data/scripts/missions/killemall.lua` uses destruction callbacks
and timers to complete or fail a mission. The generated
[`Lua API reference`](../../doc/2_luaapi.md) can lag these registrations; use
the current binding registration and implementation when names or behavior
differ.

The script engine assigns increasing keys to registered `Scriptable` objects.
Bindings pass these keys between Lua and C++; lookup rejects nonpositive,
future, and removed keys, and an object cannot be registered with multiple
engines. `World` registers world objects, while the event poller and mission
system register their own scriptable objects. Timers and callback polls are
added to the owning world's event poller and tracked by the creating
`GamePlayScript`. When that script is destroyed, it removes still-live polls and
unregisters its still-valid local keys. Callback handles also check that the
originating script is still valid before calling into Lua.

## Server execution and network boundary

Each `World` owns its script engine, and `World::update` advances it as part of
the world update. Bindings resolve through that engine's world, so a gameplay
script using a server world's engine acts on that world rather than the global
client world. A scripting test checks server-world ship creation, callbacks,
and faction access against the owning world. In a headless build,
`playerShip()` returns key `0`, `showMessage` does nothing, and `playVoice`
returns `-1`.

Lua does not provide the network transport or automatically replicate arbitrary
Lua state. Replication is explicit C++ behavior: while a `TcpServer` exists, it
enables mission-state event recording, drains the events, and sends them to
connected clients. The protocol event carries the mission key, state, and
simulation tick; it does not transfer script memory or grant authority to a
client. A server test verifies that mission start and success events reach a
connected client.

## Native values and tests

The function wrapper checks unsigned arguments as Lua integers, rejecting
negative values and values outside the destination C++ type's range. An
`unsigned long` result is pushed as a Lua integer when representable; larger
values are pushed as a Lua number, so scripts should not assume exact
integer-valued round trips for that larger range. Floating-point values use
Lua's number type.

For bound functions, `glm::vec3` values use a Lua table with named `x`, `y`,
and `z` fields on both input and output. Unsigned integer arguments must be
nonnegative and fit the destination C++ type. An `unsigned long` result is
pushed as a Lua integer when representable, and otherwise as a Lua number, so
large values may not round-trip exactly. Bandit tests in
`test/scripting/testvectorextension.cpp` exercise vector construction and
arithmetic, while `test/scripting/testscripting.cpp` exercises gameplay
bindings, world-object access, and callbacks.

## Missions and cleanup

Each `Mission` loads a `MissionScript` into its world's script engine.
`MissionSystem::addMission` registers and starts it; `Mission::succeed` and
`Mission::fail` invoke the optional Lua `onSuccess` or `onFailure` callback and
stop the script. The mission system removes a mission once its state is no
longer `Running`; explicit removal also unregisters its scriptable identity.
`World::update` calls `MissionSystem::update`, which invokes `Mission::update`
for active missions; the current `Mission::update` implementation is empty.
`missionStart(name)` loads `data/scripts/missions/<name>.lua`. Mission start,
success, and failure are queued for replication only while the server enables
mission-state events. See [world simulation](../architecture/world-simulation.md).
