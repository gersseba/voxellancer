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
Task** contains these Windows runtime tasks:

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

## macOS

Install the Xcode Command Line Tools, CMake, an OpenMP-capable C++17 toolchain,
and a bootstrapped vcpkg checkout. Apple's default Clang needs additional
OpenMP support; use a toolchain configured for OpenMP (for example Homebrew
LLVM and `libomp`). Install the tools with
`brew install cmake vcpkg llvm libomp pkg-config`, clone vcpkg into `~/vcpkg`, and run its
`bootstrap-vcpkg.sh`. Initialize the `glow` submodule as described above. The
client requests an OpenGL 4.1 core-profile context on macOS.

In **Terminal: Run Task**, choose **Start Client with Local Server (macOS)**.
This task runs `tools/run-local-macos.sh` using Bash rather than PowerShell.
The helper defaults `VCPKG_ROOT` to `~/vcpkg`, locates Homebrew in its standard
Apple Silicon or Intel installation, and uses Homebrew LLVM and OpenMP without
requiring VS Code to inherit terminal exports. Existing `VCPKG_ROOT`, `CC`,
and `CXX` values override the defaults; existing `CMAKE_PREFIX_PATH` entries
are retained.
It configures a Release Makefiles
build in `build/macos`, builds both `voxellancer` and `voxellancer-server`,
and launches `voxellancer -fullscreen -local-singleplayer` from the repository
root.
On macOS, the game requests focus once loading finishes. Fullscreen windows
remain visible rather than automatically minimizing when focus changes.
The client starts its matching server on an available loopback port and stops
it on exit. Both macOS local-server tasks now open the contracts main menu,
not the large-combat benchmark.
Configuration or build errors stop the task before launch.
Choose **Play Contracts with Local Server (macOS)** instead to launch the
station/mission/shop progression slice. The helper also accepts
`-scenario contracts` when invoked directly with Bash.

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
