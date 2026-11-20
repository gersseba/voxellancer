---
type: quickstart
title: Voxellancer quickstart
description: Routes contributors to relevant architecture, content, scripting, and validation documentation and summarizes the current build workflow.
tags: [quickstart, navigation, development, gameplay]
sources:
  - id: openwiki-source-68180d68a9748362b67ca1a4
    resource: repo://.vscode/tasks.json
  - id: openwiki-source-d44494ef3e497fea81240ef8
    resource: repo://CMakeLists.txt
  - id: openwiki-source-23775c3de52f3ab95a13cb8b
    resource: repo://README.md
  - id: openwiki-source-4b8e7dce368774e92d99ea30
    resource: repo://test/CMakeLists.txt
  - id: openwiki-source-2c1d10bd52d032cd62ce51a3
    resource: repo://tools/agent-check.ps1
  - id: openwiki-source-2d91624eff54e273af6563a8
    resource: repo://vcpkg.json
generated: { by: "copilot", at: "2026-11-19T09:46:20.407Z" }
verified:
  - by: openwiki/0.7.2
    at: 2026-11-19T09:46:20.407Z
---

# Voxellancer quickstart

Voxellancer is a C++ OpenGL prototype space-combat game built around editable
voxel ships. Lua scripts define scenarios and missions, while INI properties
and CSV voxel clusters describe ships and equipment. Runtime data paths are
relative to the repository root.

## Find the right guide

| If you need to change... | Start here |
|---|---|
| Startup, input, game states, cameras, or rendering | [Application runtime and game states](architecture/runtime.md) |
| Client/server startup, authority, TCP protocol, replication, or prediction | [Client-server simulation](architecture/client-server-simulation.md) |
| World updates, spawning, collision ordering, or lifecycle | [World simulation and object lifecycle](architecture/world-simulation.md) |
| Damage, structural breakage, voxel connectivity, or combat effects | [Voxel ships, collision, and damage](concepts/voxel-ships-and-damage.md) |
| Ships, equipment, voxel clusters, or other editable INI/CSV data | [Data-defined content authoring](workflows/content-authoring.md) |
| Lua scenarios, mission APIs, bindings, or scriptable object keys | [Lua scenarios and mission scripting](integrations/lua-scripting.md) |
| CMake configuration, dependencies, or the test harness | [Build and test workflow](development/build-and-test.md) |

## Source layout

- `src/` contains the engine and game runtime.
- `data/` contains content and assets loaded by name at runtime.
- `test/` contains Bandit tests grouped by subsystem.
- CMake defines separate graphical client, headless server, and test targets.
- `tools/agent-check.ps1` configures, builds, and runs the game or aggregate
  test target.

## Build, test, or run

Install the C++ build prerequisites, bootstrap vcpkg, and set `VCPKG_ROOT` to
that checkout. The root `vcpkg.json` manifest pins a registry baseline; Bandit
is a separate bundled test dependency. See
[build and test](development/build-and-test.md) for platform setup.

On Windows, from the repository root, initialize the `glow` submodule and run
the test harness:

```powershell
git submodule update --init
.\tools\agent-check.ps1 -Mode Test
```

Test mode builds and executes `all_tests`; it does not separately build the
graphical game. The harness defaults to Release; pass `-Configuration Debug`
for Debug builds. `-Mode Run` starts the multiplayer client by default and
accepts selectors for server/client startup, the shield showcase, and local or
multiplayer large-combat runs.

VS Code provides five Windows runtime tasks: **Start Server**, **Start
Client**, **Start Shield Showcase Scenario**, **Start Local Client with
Integrated Server (Large Combat Benchmark)**, and **Start Server and Two
Clients (Large Combat Benchmark)**. These are runtime tasks, not build/test
tasks. See [build and test](development/build-and-test.md) for manual target
commands, headless builds, and benchmark details.
