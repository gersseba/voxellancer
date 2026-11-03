---
type: development-guide
title: Build and test workflow
description: Documents the CMake and vcpkg native dependency model, platform prerequisites, Bandit test target, and repository build/test commands.
tags: [build, cmake, vcpkg, testing, native-dependencies]
verified:
  - by: openwiki/0.7.2
    at: 2026-11-03T15:48:08.697Z
sources:
  - id: openwiki-source-68180d68a9748362b67ca1a4
    resource: repo://.vscode/tasks.json
  - id: openwiki-source-d44494ef3e497fea81240ef8
    resource: repo://CMakeLists.txt
  - id: openwiki-source-65ddf3f0d6898783c85c7e24
    resource: repo://doc/3_buildinstructions.md
  - id: openwiki-source-4b8e7dce368774e92d99ea30
    resource: repo://test/CMakeLists.txt
  - id: openwiki-source-2c1d10bd52d032cd62ce51a3
    resource: repo://tools/agent-check.ps1
  - id: openwiki-source-2d91624eff54e273af6563a8
    resource: repo://vcpkg.json
generated: { by: "copilot", at: "2026-11-03T15:48:08.697Z" }
---

# Build and test workflow

Voxellancer uses CMake 3.21 or newer, a C++17 compiler, and vcpkg for its
third-party native libraries. The root [`vcpkg.json`](../../vcpkg.json) pins a
registry baseline and declares GLEW, GLFW3, GLM, Lua, and SFML Audio. The current
baseline resolves GLEW 2.3.1, GLFW 3.5.1, GLM 1.0.3, Lua 5.5.1, and SFML 3.1.0
Audio. CMake also discovers platform OpenGL and OpenMP development support.
vcpkg builds or obtains libraries for the selected platform and triplet.

Bandit 1.1.4 is the exception: it remains in `lib/lib.zip`, not in the vcpkg
manifest. CMake extracts its headers when the expected Bandit directory is
missing, then uses them to build the `all_tests` target.

## How the tools fit together

The Maven comparison is an analogy, not a one-tool replacement: `vcpkg.json`
plays the dependency-declaration role, `CMakeLists.txt` defines the native
project and build graph, and the compiler/toolchain produces platform-specific
executables. The [`agent-check.ps1`](../../tools/agent-check.ps1) script
coordinates configure, build, and run/test steps. Unlike a Java project with a
single Maven lifecycle, dependency resolution, native compilation, and the
repository's convenience workflow are separate tools.

## Prerequisites and setup

Install CMake, Git, a C++17 compiler, and the platform's OpenGL/OpenMP
development requirements. Bootstrap a vcpkg checkout and set `VCPKG_ROOT` to
its root. The harness requires that environment variable and passes vcpkg's
CMake toolchain file to configuration; it does not install vcpkg or the
compiler for you. Setup commands for Windows and Linux are in
[`doc/3_buildinstructions.md`](../../doc/3_buildinstructions.md). The optional
Linux helper `lib/setup_libs.sh` preinstalls the manifest dependencies.

Initialize the `glow` submodule after cloning:

```sh
git submodule update --init
```

## Build, test, or run

From the repository root on Windows, the default harness command is:

```powershell
.\tools\agent-check.ps1
```

The script selects an installed Visual Studio generator when available,
configures a vcpkg build directory, and defaults to Release. `-Configuration
Debug` selects Debug; `-BuildDirectory <path>` chooses another build directory.
The modes behave as follows:

- `-Mode Test` builds both `all_tests` and `voxellancer`, then executes the
  `all_tests` program.
- `-Mode Run` builds `voxellancer` and launches the game.

The harness validates an existing build directory's generator and vcpkg
toolchain before reusing it. In VS Code, **Build and Test** is the default
build task; **Build and Run Game** selects the run mode. Both tasks invoke the
same PowerShell harness.

For manual builds, CMake can be configured with
`-DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"`.
Then build `voxellancer` for the game or `all_tests` for the test executable.
On Linux, follow the direct CMake commands in the build instructions; the
PowerShell harness and Windows validation steps are not Linux test evidence.

## Test organization

`test/CMakeLists.txt` recursively collects test `.cpp` files into the Bandit
`all_tests` executable and excludes the standalone performance test source.
Optional subsystem test targets are controlled by `OPTION_CREATE_SUB_TEST`.
The harness runs the combined executable after building it. Place focused
coverage beside the corresponding subsystem; for example, voxel mechanics
tests live in `test/voxelcluster`, `test/damageforwarder`, and `test/splitting`.
See [voxel ships and damage](../concepts/voxel-ships-and-damage.md) for the
mechanics these tests protect.
