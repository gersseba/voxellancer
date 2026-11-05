Build instructions
==================

## Requirements

- CMake 3.21 or newer
- Git
- A C++17 compiler and the platform's OpenGL/OpenMP development support
- [vcpkg](https://github.com/microsoft/vcpkg)

Initialize the required `glow` submodule after cloning:

```sh
git submodule update --init
```

The root `vcpkg.json` manifest pins the vcpkg registry baseline and resolves
GLEW 2.3.1, GLFW 3.5.1, GLM 1.0.3, Lua 5.5.1, and SFML 3.1.0 Audio. The first
CMake configure downloads and builds these dependencies. Bandit 1.1.4, used
only by the test target, is extracted from the compatibility archive in
`lib/lib.zip` when needed. The Linux helper `./lib/setup_libs.sh` can preinstall
the manifest dependencies after vcpkg is bootstrapped.

## Windows

Install Visual Studio Build Tools with the **Desktop development with C++**
workload and a Windows SDK. Then install vcpkg and set `VCPKG_ROOT` to its
directory:

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
& C:\vcpkg\bootstrap-vcpkg.bat
$env:VCPKG_ROOT = 'C:\vcpkg'
```

For future terminals, add `VCPKG_ROOT` to the user environment and restart VS
Code. Configure and build:

```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
cmake --build build --config Release --target voxellancer
```

Or run `.\tools\agent-check.ps1` to build and test. In VS Code, choose **Build
and Run Game**, **Build and Test**, or one of the client/server benchmark tasks
from **Terminal: Run Task**.

To run a finite performance benchmark from the repository root after building:

```powershell
.\build\agent-vcpkg-vs18-stable\Release\voxellancer.exe -benchmark-rendering
.\build\agent-vcpkg-vs18-stable\Release\voxellancer.exe -benchmark-combat
.\build\agent-vcpkg-vs18-stable\Release\voxellancer.exe -benchmark-large-combat
.\build\agent-vcpkg-vs18-stable\Release\voxellancer-server.exe --ticks 1800 --ships-per-team 400
```

The client large combat benchmark enables VSync and runs 400 AI ships per team
in reproducibly randomized, separated clusters for 1,800 measured frames after
120 warm-up frames. It reports the number and percentage of measured frames below 55 FPS, the longest
consecutive below-target run, and the worst FPS. Each benchmark writes a JSON
report, including per-frame timings, to `benchmark-results\`. Its
`timing_profile` includes CPU timings for update, draw submission, buffer-swap
wait, event polling, and world-update subsystems, plus GPU draw time when
supported by the graphics driver. GPU work can overlap CPU work, and
buffer-swap wait includes VSync, so these measurements should not be summed.
The aligned `time_series` arrays record frame FPS, world-object voxel counts,
ship/projectile counts, voxel-particle counts, and per-frame simulation timings
for comparing workload changes with slow frames. Particle-buffer capacity is
also recorded because the renderer submits allocated slots, including expired
particles. Client benchmark modes write their reports to `benchmark-results\`.

The headless server benchmark runs the same seeded 800-ship combat scenario for
1,800 fixed simulation ticks. It reports p50/p95/p99/max tick time, missed
60 Hz deadlines, entity and ship counts, and remaining simulation backlog as
JSON on stdout. These server tick metrics exclude rendering and are not directly
comparable to client frame times; use them to evaluate server simulation cost
against the 16.67 ms tick budget.

The older `test_performance` Bandit target remains separate from `all_tests`.
It currently runs a random-number microbenchmark; its three world/simulation
performance cases are skipped. Build and run it with:

```powershell
cmake --build build\agent-vcpkg-vs18-stable --target test_performance --config Release
.\build\agent-vcpkg-vs18-stable\Release\test_performance.exe
```

Use the client/server benchmark tasks above for the seeded gameplay workloads;
the legacy target does not measure either executable's end-to-end performance.

## Linux

Install the compiler, CMake, OpenGL, and OpenMP development packages for your
distribution. Install vcpkg and export its root before configuring:

```sh
git clone https://github.com/microsoft/vcpkg.git "$HOME/vcpkg"
"$HOME/vcpkg/bootstrap-vcpkg.sh"
export VCPKG_ROOT="$HOME/vcpkg"
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --target voxellancer
```

Use `cmake --build build --target all_tests` to build the tests. Run
`./build/all_tests` from the repository root to execute them.
