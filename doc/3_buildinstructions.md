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
and Run Game** or **Build and Test** from **Terminal: Run Task**.

To run a finite performance benchmark from the repository root after building:

```powershell
.\build\Release\voxellancer.exe -benchmark-rendering
.\build\Release\voxellancer.exe -benchmark-combat
.\build\Release\voxellancer.exe -benchmark-large-combat
```

The large combat benchmark enables VSync and runs 400 AI ships per team in
reproducibly randomized, separated clusters for 3,600 measured frames. It
reports the number and percentage of measured frames below 55 FPS, the longest
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
particles.

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
