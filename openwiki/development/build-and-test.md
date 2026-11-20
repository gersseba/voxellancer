---
type: development-guide
title: Build and test workflow
description: Documents the CMake and vcpkg native dependency model, platform prerequisites, Bandit test target, and repository build/test commands.
tags: [build, cmake, vcpkg, testing, native-dependencies]
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

# Build and test workflow

Voxellancer uses CMake 3.21 or newer, a C++17 compiler, and vcpkg for its
third-party native libraries. The root [`vcpkg.json`](../../vcpkg.json) pins a
registry baseline and declares GLM, Lua, and SFML Network; its default `client`
feature adds GLEW, GLFW3, and SFML Audio. CMake independently exposes
`VOXELLANCER_BUILD_CLIENT`, `VOXELLANCER_BUILD_SERVER`, and
`VOXELLANCER_BUILD_TESTS`, all enabled by default. The graphical game needs
OpenGL, GLEW, GLFW, and SFML Audio; the headless server shares simulation and
protocol code without those client graphics/audio dependencies. Client or
server builds still require Lua and OpenMP.

Bandit 1.1.4 is the exception: it remains in `lib/lib.zip`, not in the vcpkg
manifest. When tests are enabled, CMake extracts its headers if needed and
requires them to configure the tests.

## Prerequisites and setup

Install CMake, Git, a C++17 compiler, and the platform development packages
listed in [`doc/3_buildinstructions.md`](../../doc/3_buildinstructions.md).
The full graphical build requires OpenGL and OpenMP support; the headless
server still requires OpenMP. Bootstrap vcpkg and set `VCPKG_ROOT` to its root.
The PowerShell harness requires that environment variable and a bootstrapped
vcpkg toolchain; it does not install vcpkg or the compiler for you. Setup
commands for Windows and Linux are in
[`doc/3_buildinstructions.md`](../../doc/3_buildinstructions.md). The optional
Linux helper `lib/setup_libs.sh` preinstalls the manifest dependencies.

Initialize the `glow` submodule after cloning:

```sh
git submodule update --init
```

## Build, test, or run

From the repository root, the default Windows validation command is:

```powershell
.\tools\agent-check.ps1
```

The script selects an installed Visual Studio generator when available,
configures a vcpkg build directory, and defaults to Release. Pass
`-Configuration Debug` for Debug or `-BuildDirectory <path>` to choose another
build directory. `-Mode Test` builds and runs `all_tests`; `-Mode Run` builds
and launches the selected runtime target. With no Run selector, it starts the
multiplayer client.

The default client-enabled test configuration defines `all_tests`, which
recursively collects Bandit tests. `protocol_tests` is a separate target, and
`server_tests` is added when the server is enabled. With the client disabled,
`all_tests` is not configured; a headless server build can still build and run
`protocol_tests` and `server_tests`.

For a headless vcpkg configuration, disable the manifest's default client
feature and turn off the CMake client target:

```powershell
cmake -S . -B build/server-headless `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON `
  -DVOXELLANCER_BUILD_CLIENT=OFF
cmake --build build/server-headless --target voxellancer-server server_tests protocol_tests
```

In VS Code, the runtime tasks invoke `agent-check.ps1`: **Start Server**,
**Start Client**, **Start Shield Showcase Scenario**, **Start Local Client with
Integrated Server (Large Combat Benchmark)**, and **Start Server and Two
Clients (Large Combat Benchmark)**. The task definitions use `powershell.exe`.

For manual builds, CMake can be configured with
`-DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"`.
Build `voxellancer` for the graphical game, `voxellancer-server` for the
headless server, `protocol_tests` for protocol encoding, or `server_tests` for
server integration. Follow the platform-specific CMake commands in the build
instructions; the PowerShell harness is the documented Windows workflow.

## Test organization

`test/CMakeLists.txt` recursively collects test `.cpp` files into the Bandit
`all_tests` executable when the graphical client is enabled. The standalone
`protocol_tests` target covers wire encoding, while `server_tests` covers the
TCP simulation path when the server is enabled. Optional subsystem targets are
controlled by `OPTION_CREATE_SUB_TEST`. Place focused coverage beside the
corresponding subsystem; for example, voxel mechanics tests live in
`test/voxelcluster`, `test/damageforwarder`, and `test/splitting`. See
[voxel ships and damage](../concepts/voxel-ships-and-damage.md) for the
mechanics these tests protect.

## Client and server benchmarks

The harness's `Run` mode supports server/client launch, an integrated local
large-combat run, and a server with two large-combat clients:

```powershell
.\tools\agent-check.ps1 -Mode Run -Benchmark StartServer
.\tools\agent-check.ps1 -Mode Run -Benchmark StartClient
.\tools\agent-check.ps1 -Mode Run -Benchmark LocalLargeCombat
.\tools\agent-check.ps1 -Mode Run -Benchmark MultiplayerLargeCombat
```

The large-combat modes use a seeded 800-ship scenario and 1,800 server
simulation intervals. The integrated mode starts one local server/client pair;
the multiplayer mode starts a server lobby and two clients. Reports are written
under `benchmark-results/`; measurements are diagnostic and are not capacity
guarantees. `-SquadSize` is supported only with `MultiplayerLargeCombat`.
