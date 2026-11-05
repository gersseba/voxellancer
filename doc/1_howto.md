How to play
===========

You can fly around, fight and destroy other ships or do missions.

## Controls
* W,A,S,D - direction
* Left Mouse - fire
* Right Mouse - click to select target / hold to change direction
* Space - change to fight controls
* Q, E - roll
* R - Rocket
* P - Pause
* F1-F5 change scenarios

You can configure the controls as you want. The instructions are currently only displayed in the console.

* F10 start gamepad/joystick configuration
* F11 start keyboard configuration

only for debugging:
* T - show active leafs of worldtree
* F8 - reload shader and configs
* F9 - world status information
* 1-9 - show framebuffer
* F - toggle flycam

## Commandline parameters

	Usage: voxellancer [-fullscreen] [-stereo] [-benchmark-rendering | -benchmark-combat | -benchmark-large-combat]

	  -fullscreen  fullscreen mode
	  -stereo      stereo rendering
	  -nointro     skip the intro
	  -benchmark-rendering  run the fixed voxel-heavy rendering benchmark
	  -benchmark-combat     run the AI combat and collision benchmark
	  -benchmark-large-combat  run 800 ships with VSync and count frames below 55 FPS

## Performance benchmarks

The current gameplay FPS is shown in the top-left corner.

In VS Code, choose **Terminal: Run Task**, then select **Build and Run Rendering
Benchmark**, **Build and Run Combat Benchmark**, or **Build and Run Large Fleet
VSync Benchmark**. Each task builds the game before running the selected
benchmark.

You can also run the built executable from the repository root so it can load
the game data. For the default Visual Studio 2026 build directory:

```powershell
.\build\agent-vcpkg-vs18-stable\Release\voxellancer.exe -benchmark-rendering
.\build\agent-vcpkg-vs18-stable\Release\voxellancer.exe -benchmark-combat
.\build\agent-vcpkg-vs18-stable\Release\voxellancer.exe -benchmark-large-combat
```

The rendering and standard combat modes disable VSync. The large fleet mode
keeps VSync enabled and runs 400 AI ships per team, randomly but reproducibly
spaced within two opposing clusters. After 120 warm-up frames, the large fleet
mode measures 1,800 frames (nominally 30 seconds at 60 FPS); the other modes
measure 600 frames. Rendering draws a voxel tunnel without advancing world
simulation; the standard combat mode updates 24 AI ships while rendering.

All modes print average FPS, median/p95/p99 frame times, 1% low FPS, and
workload counts, then write those metrics and all measured frame times to a
unique JSON file in `benchmark-results\` at the repository root. The large
fleet run also reports how many measured frames fell below 55 FPS, the
percentage, longest consecutive below-target run, and worst FPS. The VS Code
task terminal prints the result file path when the run finishes.

The JSON `timing_profile` breaks each frame into CPU game-update, draw
submission, buffer-swap wait, event polling, and other work, with averages,
p95s, maxima, totals, and shares of measured frame time. Nested sections split
world updates into subsystems and world logic into movement/collision
detection, collision/damage processing, and spawn/split/cleanup. Where the
graphics driver supports timer queries, `gpu_draw` reports GPU execution time
separately. GPU work can overlap CPU work, while buffer-swap time includes
VSync waiting; compare these metrics rather than adding them together.

The nested `task_profile` adds CPU timings for world updates, firing and
line-of-fire queries, projectile movement and spawn collision checks, and
the rendering passes. GPU timer queries separately measure the scene and
rendering passes. Its `hierarchy` gives each task's parent and CPU/GPU
attribution; parent timings include their subtasks. `per_second` reports
average milliseconds per measured frame within each elapsed one-second bucket.
`three_second_comparison` uses the first three complete buckets as the baseline
and selects the contiguous three-bucket window with the lowest average FPS.
It reports the FPS percentage change and only task metrics changing by at least
0.25 ms/frame and 10% between the windows; a zero baseline uses the absolute
threshold, since its percentage change is undefined. Stable metrics remain in
the per-second data but are omitted from this comparison. A zero means no work
was recorded (or the rounded value is below 0.0001 ms); `null` means a GPU
timing or percentage change was unavailable.

The `time_series` arrays are indexed by measured frame and align with
`frame_times_ms`. They include FPS, world-object voxel totals, ship/bullet/rocket
counts, voxel-particle counts, and per-frame update/collision timings. The
`world_voxels` total includes voxels owned by world objects; active particles
and particle-buffer capacity are reported separately. The particle renderer
submits the allocated buffer capacity, so this value can remain high after
particles expire. Comparing these series can show whether a slowdown tracks
with projectile population, voxel count, simulation work, or rendering.
The target-profiling series also record primary- and per-gun candidate counts,
target switches and assignments, weapon aim checks, line-of-fire queries, and
scheduled bullets. Candidate counts are collected when target selection runs;
they are not counts of every target handle checked for validity during list
maintenance.

### 800-ship VSync baseline and optimization plan

The persisted [large-combat baseline](../benchmark-results/large-combat-vsync-baseline.json)
was captured on November 4, 2026, on an AMD Radeon RX 5700 XT. It contains 1,800
measured frames after 120 warm-up frames. Average FPS was 45.44; p95/p99 frame
times were 35.95/81.64 ms; 66.89% of frames fell below 55 FPS, with a longest
below-target streak of 868 frames. The run started with 800 ships and ended
with 91.

The first three measured seconds averaged 48.65 FPS. The slowest contiguous
three-second interval starts at second 7 and averaged 13.42 FPS, a 72.42%
decrease from baseline. In that interval, selected CPU task averages were:

| Task | Worst-window CPU ms/frame |
|---|---:|
| Movement and voxel collision detection | 19.154 |
| Projectile movement and collision *(nested)* | 14.686 |
| AI target scan and firing | 17.250 |
| Bullet line-of-fire queries *(nested)* | 7.275 |
| Collision processing and damage | 1.515 |
| Spawn, split, and cleanup | 8.420 |

Parent and child metrics overlap and must not be added. These estimates are
from one run; repeat benchmarks under comparable conditions before drawing
conclusions about small changes. The planned steps are hypotheses, not promised
FPS gains. Their CPU-time estimates overlap, so they are not additive:

1. [Early-out line-of-fire queries](performance-plan/01-line-of-fire-early-out.md)
2. [Bound and stagger AI target evaluation](performance-plan/02-target-budgeting.md)
3. [Reuse movement broadphase results](performance-plan/03-movement-broadphase.md)
4. [Sweep projectiles with conservative collision proxies](performance-plan/04-projectile-sweeps-and-collision-lod.md)
5. [Evaluate snapshot-based simulation parallelism](performance-plan/05-simulation-snapshot-parallelism.md)
