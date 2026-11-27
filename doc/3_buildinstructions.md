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
| `RUN` | `StartServer`, `StartClient` (default for `Run`), `LocalServer`, `LocalContracts`, `ShieldShowcase`, `AiComparison`, `WeaponRange`, `LocalLargeCombat`, `MultiplayerLargeCombat` |
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

In VS Code, **Terminal: Run Task > start game** builds the client and local
server and opens the main menu. Use the harness commands above for tests,
multiplayer, and benchmarks.

## Browser build

The browser version runs the single-player contracts simulation locally in
WebAssembly; it does not need a hosted game server. It requires a WebGL 2
browser, the Emscripten SDK, CMake, Git, and a bootstrapped vcpkg checkout.
Initialize `glow` as above and activate the Emscripten SDK.

In VS Code, **Terminal: Run Task > start browser game** builds the game, serves
it on `127.0.0.1:8000`, and opens a browser. Stop the task to stop the server.
It requires Python 3 and discovers activated SDKs in `~/emsdk` and `~/vcpkg`;
set `EMSDK` or `VCPKG_ROOT` in VS Code's environment for other locations.
On Windows it also requires Ninja on `PATH`. Port 8000 must be available.

Alternatively, set `VCPKG_ROOT` to the vcpkg checkout and build and serve
manually from the repository root on macOS or Linux:

```sh
emcmake cmake -S web -B build/web -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$EMSDK/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake" \
  -DVCPKG_TARGET_TRIPLET=wasm32-emscripten
cmake --build build/web --parallel
python3 -m http.server 8000 --directory build/web
```

Open `http://localhost:8000/voxellancer-web.html`. Serve the generated HTML,
JavaScript, WebAssembly, and data files together over HTTP; opening the HTML
directly as a file does not work. The initial configure downloads GLEW and
builds the web manifest's GLM and Lua dependencies. A static web host can serve
the same build directory; no gameplay backend is required.

For GitHub Pages, set the repository's **Settings > Pages > Build and
deployment > Source** to **GitHub Actions**. The `Deploy browser game` workflow
builds and deploys pushes to `master`; it can also be started manually on
`master` from the Actions tab. It publishes only the generated game files and
uses the game as the site's index page. The browser-build changes and workflow
must be committed to that branch before GitHub can deploy them.

The browser build is single-player only. Checkpoints, controls, and settings
use browser storage and can be lost when site data is cleared. Click the game
to focus it and allow audio. General/audio preferences require **Save settings**;
developer gun/effect configurations require **SAVE GUN** / **SAVE EFFECT**.
These buttons save only their own menu/group, not other unsaved edits.
Display timing is controlled by the browser;
audio uses distance attenuation without spatial panning. Performance depends
on the browser and graphics hardware.

### Effect lab and frame capture

Choose **Terminal: Run Task > start effect lab**, or build/serve directly:

```sh
python3 tools/run-web.py --effect-lab --group sphereshield --effect shield-hit
```

With an existing server, open
`http://127.0.0.1:8000/voxellancer-web.html?scenario=effect-lab&group=sphereshield&effect=shield-hit`.
This mode does not start the game world, local server, audio, HUD, or unrelated
effects. It renders a single event using the shared production C++ model and
renderer. **Space** pauses/resumes; **Right** steps; **R** restarts. The default
is authored effect settings; add `&saved=1` to load this browser site's saved
effect settings. The lab never saves configuration changes.

For automated analysis, first build the browser target, then run from the root:

```sh
node tools/capture-effects.mjs --group pulselaser --effect muzzle --fps 60 --frames 30 --out build/effect-captures/pulse-muzzle
node tools/capture-effects.mjs --group plasmamortar --effect impact --fps 60 --frames 60 --out build/effect-captures/blast-impact
node tools/capture-effects.mjs --group sphereshield --effect shield-hit --frames 90 --out build/effect-captures/shield-hit
```

Requirements: **Node.js 22+**, an existing browser build, and Chrome/Chromium.
Use `--browser <executable>` or `CHROME_PATH` if it is not in a standard location.
The tool serves the build on a private loopback port, launches its own headless
browser/profile, verifies readiness, and cleans both up afterwards. It does not
access your normal browser profile or modify saved game settings.

- Gun groups are the ten definition keys: `pulselaser`, `pulselasermk2`,
  `heavyautocannon`, `kineticrailgun`, `plasmamortar`, `scattercannon`, `gun`,
  `ioncannon`, `railgun`, `snowcanon`.
- Gun effects: `muzzle`, `projectile` (core and trailing geometry), `impact`.
- Shield groups: `hullshield`, `sphereshield`. Effects: `shield-idle`,
  `shield-hit`, `shield-break`, `shield-recharge`. Transient shield captures
  suppress idle/sweep decoration, but use the real shield event/state path.
- `--set pulselaser.muzzleTime=0.16` or `--set global.brightness=0.5` overrides
  schema fields with the same validation as the editor. Repeat `--set` as needed.
  `--settings <settings.ini>` loads saved effect groups first; overrides win.
  Group/field keys, valid ranges, and effective values are in the manifest.
- `--width 640 --height 640` sets image size; `--build <directory>` selects
  another browser build. Run `--help` for all options.

Each screenshot corresponds to exactly one fixed simulation step:
`time = frameIndex / fps`. Capture waits for rendering and writing each image
before advancing; slow machines do not skip animation frames. Playback is a
single event, not a loop. Select enough frames for the fade-out; idle effects
do not expire. All raw `frame-00000.png` images are retained.

The new output directory contains `index.html` (offline play/scrub),
`contact-sheet.png` (samples concentrated around visible frames and fade-out),
`manifest.json` (frame count/times, configuration, camera, renderer, box count,
visible pixels, bounds, RGB-difference peak/energy, SHA-256 per PNG), and logs.
`complete: true` is written only after every requested frame and report succeeds.
Failures exit nonzero and leave an error report; existing captures are never
overwritten. Compare captures with identical FPS, camera/size, and renderer;
pixel identity is not promised across different GPUs/browser versions.

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

Run `cmake -P tools/agent-check.cmake` to build and test. For local play, use
**Terminal: Run Task > start game** in VS Code. For benchmarks, run:

```powershell
cmake -DMODE=Run -DRUN=LocalLargeCombat -P tools/agent-check.cmake
cmake -DMODE=Run -DRUN=MultiplayerLargeCombat -P tools/agent-check.cmake
```

Both benchmarks use the same seeded 800-ship combat scenario. The server
advances exactly 1,800 simulation intervals after the target clients connect;
clients record rendered frames from the first started-world snapshot to the
final server tick. The two-client command launches a server waiting for two
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

In **Terminal: Run Task**, choose **start game**. It runs
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
it on exit. The game opens the main menu, not the large-combat benchmark.
Configuration or build errors stop the task before launch.
Choose **Contracts** in the menu to enter the station/mission/shop progression.

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
