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

## Build, test, and run harness

`tools/agent-check.cmake` is a CMake script, so it runs identically on
Windows, macOS, and Linux with only CMake installed. From the repository root:

```sh
cmake -P tools/agent-check.cmake
```

This configures the build directory, resolves the manifest dependencies,
builds `all_tests`, runs it from the repository root, and fails with the test
exit code. Options are passed as `-D<NAME>=<value>` before `-P`:

| Option | Values |
| --- | --- |
| `MODE` | `Test` (default) or `Run` |
| `RUN` | `StartServer`, `StartClient` (default for `Run`), `LocalServer`, `LocalContracts`, `ShieldShowcase`, `AiComparison`, `LocalLargeCombat`, `MultiplayerLargeCombat` |
| `CONFIG` | `Release` (default) or `Debug` |
| `BUILD_DIR` | build directory; defaults to `build/macos` on macOS and `build/agent-vcpkg-stable` elsewhere (an existing `build/agent-vcpkg-vs<N>-stable` is reused on Windows) |
| `GENERATOR` | CMake generator for a new build directory |
| `VCPKG_ROOT` | vcpkg checkout, overriding discovery |
| `SQUAD_SIZE` | 1–400, `MultiplayerLargeCombat` only |
| `ARGS` / `TEST_ARGS` | extra `;`-separated game or `all_tests` arguments, e.g. `-DTEST_ARGS=--only=Shield` |

vcpkg is located from `-DVCPKG_ROOT`, the `VCPKG_ROOT` or
`VCPKG_INSTALLATION_ROOT` environment variables, the toolchain of an existing
build directory, `~/vcpkg` (`%USERPROFILE%\vcpkg`), `C:\vcpkg`, `C:\src\vcpkg`,
`/opt/vcpkg`, or the directory of a `vcpkg` executable on `PATH`. Setting
`VCPKG_ROOT` is therefore optional for the standard locations. On a first macOS
configure, Homebrew LLVM and `libomp` are used when present unless `CC`/`CXX`
are set. An existing build directory configured with another generator or
vcpkg toolchain is rejected rather than reconfigured.

Start the server before its clients:

```sh
cmake -DMODE=Run -DRUN=StartServer -P tools/agent-check.cmake
cmake -DMODE=Run -DRUN=StartClient -P tools/agent-check.cmake
```

`StartServer` opens a two-pilot contracts lobby on port 27801. `StartClient`
connects a graphical client; a second concurrent `StartClient` uses the
`client-2` identity. `MultiplayerLargeCombat` starts the benchmark server, waits
until it listens, starts both clients, and validates the three reports.
`tools/agent-check.ps1` remains as a compatibility wrapper for the previous
PowerShell parameters.

In VS Code, **Terminal: Run Task** offers the same entries on every platform:
**Run Tests**, **Start Server**, **Start Client**, **Start Client with Local
Server**, **Play Contracts with Local Server**, **AI Dogfight Comparison**,
**Start Shield Showcase Scenario**, and both large-combat benchmarks.

## Windows

Install Visual Studio Build Tools with the **Desktop development with C++**
workload and a Windows SDK. Then install vcpkg (the harness finds
`C:\vcpkg` automatically; set `VCPKG_ROOT` for other locations):

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
& C:\vcpkg\bootstrap-vcpkg.bat
$env:VCPKG_ROOT = 'C:\vcpkg'
```

For a manual configure and build:

```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
cmake --build build --config Release --target voxellancer
```

Run `cmake -P tools/agent-check.cmake` to build and test. In VS Code,
**Terminal: Run Task** contains these runtime tasks:

- **Start Server** — start a two-pilot contracts lobby on port 27801.
- **Start Client** — start a graphical multiplayer client.
- **Start Shield Showcase Scenario** — start the shield demonstration.
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
explicitly in `benchmark-results/`.

Server reports include tick and network-poll timing, snapshot state
construction, interest filtering, snapshot delta construction and encoding,
packet queue work, per-client bytes, and visible entity counts. Client reports
include frame timing, game update/draw, network polling, snapshot sampling,
input prediction, replica updates, and GPU draw timings when timer queries are
available. Both include per-frame/tick time series and p50/p95/p99/max
summaries. Timings can be nested and overlap; do not sum them. Server tick time
excludes rendering and is not directly comparable to client frame time.

## macOS

Install the Xcode Command Line Tools, CMake, an OpenMP-capable C++17 toolchain,
and a bootstrapped vcpkg checkout. Apple's default Clang needs additional
OpenMP support; use a toolchain configured for OpenMP (for example Homebrew
LLVM and `libomp`). Install the tools with
`brew install cmake vcpkg llvm libomp pkg-config`, clone vcpkg into `~/vcpkg`, and run its
`bootstrap-vcpkg.sh`. Initialize the `glow` submodule as described above. The
client requests an OpenGL 4.1 core-profile context on macOS.

In **Terminal: Run Task**, choose **Start Client with Local Server**. It runs
`cmake -DMODE=Run -DRUN=LocalServer -P tools/agent-check.cmake`, which finds
`~/vcpkg` and Homebrew LLVM/OpenMP (Apple Silicon or Intel) without requiring
VS Code to inherit terminal exports. Existing `VCPKG_ROOT`, `CC`, and `CXX`
values override the defaults.
It configures a Release build in `build/macos`, builds both `voxellancer` and
`voxellancer-server`, and launches `voxellancer -fullscreen -local-singleplayer`
from the repository root. `tools/run-local-macos.sh` is a thin wrapper for the
same command.
On macOS, the game requests focus once loading finishes. Fullscreen windows
remain visible rather than automatically minimizing when focus changes.
The client starts its matching server on an available loopback port and stops
it on exit. Both macOS local-server tasks now open the contracts main menu,
not the large-combat benchmark.
Configuration or build errors stop the task before launch.
Choose **Play Contracts with Local Server** instead to launch the
station/mission/shop progression slice (`-DRUN=LocalContracts`).

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

Run `cmake -P tools/agent-check.cmake` to build and execute the tests, or
build `all_tests` manually and run it from the repository root.
