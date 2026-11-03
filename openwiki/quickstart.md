---
type: quickstart
title: Voxellancer quickstart
description: Routes contributors to relevant architecture, content, scripting, and validation documentation and summarizes the current build workflow.
tags: [quickstart, navigation, development, gameplay]
verified:
  - by: openwiki/0.7.2
    at: 2026-11-03T15:48:08.697Z
sources:
  - id: openwiki-source-8037e2358a2c4f9b2c722a11
    resource: repo://AGENTS.md
  - id: openwiki-source-d44494ef3e497fea81240ef8
    resource: repo://CMakeLists.txt
  - id: openwiki-source-65ddf3f0d6898783c85c7e24
    resource: repo://doc/3_buildinstructions.md
  - id: openwiki-source-23775c3de52f3ab95a13cb8b
    resource: repo://README.md
  - id: openwiki-source-4b8e7dce368774e92d99ea30
    resource: repo://test/CMakeLists.txt
  - id: openwiki-source-2c1d10bd52d032cd62ce51a3
    resource: repo://tools/agent-check.ps1
  - id: openwiki-source-2d91624eff54e273af6563a8
    resource: repo://vcpkg.json
generated: { by: "copilot", at: "2026-11-03T15:48:08.697Z" }
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
| World updates, spawning, collision ordering, or lifecycle | [World simulation and object lifecycle](architecture/world-simulation.md) |
| Damage, structural breakage, voxel connectivity, or combat effects | [Voxel ships, collision, and damage](concepts/voxel-ships-and-damage.md) |
| Ships, equipment, voxel clusters, or other editable INI/CSV data | [Data-defined content authoring](workflows/content-authoring.md) |
| Lua scenarios, mission APIs, bindings, or scriptable object keys | [Lua scenarios and mission scripting](integrations/lua-scripting.md) |
| CMake configuration, dependencies, or the test harness | [Build and test workflow](development/build-and-test.md) |

## Source layout

- `src/` contains the engine and game runtime.
- `data/` contains content and assets loaded by name at runtime.
- `test/` contains Bandit tests grouped by subsystem.
- `tools/agent-check.ps1` configures, builds, and runs the game or aggregate
  test target.

## Build, test, or run

Install the C++ build prerequisites, bootstrap vcpkg, and set `VCPKG_ROOT` to
that checkout. The root `vcpkg.json` manifest declares runtime libraries and
pins a registry baseline; Bandit remains a separate bundled test dependency.
See [build and test](development/build-and-test.md) for platform setup.

From the repository root, initialize the `glow` submodule and run the test
harness:

```powershell
git submodule update --init
.\tools\agent-check.ps1 -Mode Test
```

Test mode builds `all_tests` and the game, then executes the test program. To
launch the game instead, use `.\tools\agent-check.ps1 -Mode Run`. The default
configuration is Release; pass `-Configuration Debug` for Debug builds.
In VS Code, choose **Build and Test** or **Build and Run Game** from
**Terminal: Run Task**; **Ctrl+Shift+B** runs the default test task.
