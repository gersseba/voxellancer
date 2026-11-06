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

Run `.\tools\agent-check.ps1` to build and test. In VS Code, **Terminal: Run
Task** contains four runtime tasks:

- **Start Server** — start a battle server on port 27801.
- **Start Client** — start a graphical multiplayer client.
- **Start Local Client with Integrated Server (Large Combat Benchmark)** —
  run one graphical client with an integrated authoritative benchmark server.
- **Start Server and Two Clients (Large Combat Benchmark)** — run the
  coordinated two-client multiplayer benchmark.

Both benchmark tasks use the same seeded 800-ship combat scenario. The server
advances exactly 1,800 simulation intervals after the target clients connect;
clients record rendered frames from the first started-world snapshot to the
final server tick. The two-client task launches a server waiting for two
players, then opens two graphical
clients side-by-side. Reports share a run ID and identify each role/client
explicitly in `benchmark-results\`.

Server reports include tick and network-poll timing, snapshot state
construction, interest filtering, snapshot delta construction and encoding,
packet queue work, per-client bytes, and visible entity counts. Client reports
include frame timing, game update/draw, network polling, snapshot sampling,
input prediction, replica updates, and GPU draw timings when timer queries are
available. Both include per-frame/tick time series and p50/p95/p99/max
summaries. Timings can be nested and overlap; do not sum them. Server tick time
excludes rendering and is not directly comparable to client frame time.

Use `.\tools\agent-check.ps1 -Mode Test` to build and execute `all_tests`.

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
