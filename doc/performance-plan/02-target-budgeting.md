# Step 2: Stagger context-aware AI target evaluation

## Goal

Avoid asking every ship and weapon to evaluate every available target on every
world update.

## Baseline evidence

The project baseline reports **17.250 ms/frame** for AI target scan and firing
in the slowest three-second interval, including **7.275 ms/frame** of
line-of-fire queries. The branch control run,
[large-combat-vsync-context-targeting-control.json](../../benchmark-results/large-combat-vsync-context-targeting-control.json),
recorded 45.81 average FPS, 72.39% of frames below 55 FPS, and 89 surviving
ships. In the control's worst three-second interval, AI target selection and
firing took **16.232 ms/frame**, including **6.790 ms/frame** for line-of-fire
queries.

## Implementation

- Fighters keep a primary target and reevaluate the candidate list every
  0.5 seconds, staggered across ships. They retain the current target while it
  remains viable; an aligned in-range alternative must be at least 25% closer
  to replace an in-range target. When the current target is out of range, an
  in-range aligned target or a materially closer approach target can replace
  it. If a target is destroyed, the fighter pauses its combat state until its
  next scheduled scan rather than triggering a fleet-wide scan in the same
  frame. Fighter guns receive only the primary target.
- Cruisers refresh their movement/rocket primary every 1 second. They keep it
  unless it is no longer viable or a materially closer/rocket-range target is
  found. Their guns independently cache a target per hardpoint and refresh it
  every 0.75 seconds with a staggered phase. Gun selection uses bullet range
  and that hardpoint's aim cone; firing readiness and line-of-fire checks
  remain authoritative each frame.
- Refresh phases are derived from quantized world positions rather than object
  addresses, so the benchmark schedule is stable across launches. Each
  subsequent refresh interval varies deterministically by up to 15% around its
  configured interval. This keeps the average cadence approximately unchanged
  while preventing every ship or hardpoint from repeating on a fixed,
  synchronized period. The variation does not consume gameplay random values.
- Benchmark runs seed gameplay randomness with `20261105` and record it as
  `random_seed`; the large-combat ship layout continues to use its separate
  `placement_seed` of `20261104`. The global gameplay RNG is now reproducible
  for the same benchmark setup, although wall-clock frame deltas and hardware
  scheduling can still make performance and combat progression vary.
- `CruiserFightTask` now invokes the base target-list maintenance and selection
  path; it previously relied on an unset primary target.
- Benchmark JSON records per-frame candidate, refresh, switch, assignment,
  aim-check, line-of-fire, and scheduled-bullet counts.

## How target selection scales

The large-combat scenario builds 400 `basicship` fighters per team. The
`FightTask` factory selects fighter behavior for ships below 10,000 voxels, so
these ships each receive the 400 opposing ships as their candidate list.
`FighterFightTask` initially retains the first live candidate. On each
scheduled primary scan, it evaluates the candidates by distance and alignment:
when its current target is within bullet range, a replacement must also be
within range and aligned within 45 degrees, and at least 25% closer. If the
current target is out of range, an aligned in-range target or a materially
closer approach target can replace it. A destroyed target is cleared during
the scheduled maintenance pass, rather than prompting an immediate fleet-wide
rescan. Movement, rockets, and bullets all follow the same primary target.

The fighter passes that single focused target to `BoardComputer`, instead of
its full candidate list. Each gun/hardpoint caches its selection and checks
that focused target for bullet range and its own aim cone only at its scheduled
refresh, making fighter gun candidate work O(1) per hardpoint refresh. Firing
readiness, aim, and line-of-fire checks still occur along the firing path each
frame; target caching does not remove those checks.

Candidate count is still a direct multiplier for primary scans: a scan over
`N` live candidates is O(N), and all ships' work adds up across the frame
according to how many scans are due. For ships using the multi-target gun
path, each hardpoint independently scans up to `N` candidates at refresh,
which is O(N) per hardpoint and can dominate when both target lists and gun
counts grow. The benchmark candidate counters report aggregate candidates
actually considered per frame; they should be read alongside refresh counts
and AI timing, not as a standalone cost measurement.

The intervals and 25% hysteresis are initial behavior choices, not universal
ship-balance values. The existing large-combat scenario remains the only stress
test; its ship mix and target rules can be adjusted later without creating
separate benchmarks.

## Benchmark results

Two repeated runs with the final targeting behavior were compared with the
branch control. Average FPS was **47.85** versus **45.81** (**+4.5%**), but the
below-55-FPS share averaged **83.17%** versus **72.39%** in the control. P95
frame time also rose from **31.66 ms** to **34.70 ms** on average. The
worst-window AI target-selection/firing metric averaged **5.81 ms/frame**, down
from **16.23 ms/frame** (**-64.2%**). Nested line-of-fire queries averaged
**2.67 ms/frame**, down from **6.79 ms/frame** (**-60.6%**). The change reduces
AI firing work, but the end-to-end frame-rate result is mixed rather than a
clear benchmark win.

The earlier final runs are
[1793871212887](../../benchmark-results/large-combat-vsync-1793871212887.json)
and
[1793871265568](../../benchmark-results/large-combat-vsync-1793871265568.json);
the control is
[large-combat-vsync-context-targeting-control.json](../../benchmark-results/large-combat-vsync-context-targeting-control.json).
In the last earlier run, target selection evaluated an average of 2,907
primary-target candidates and 3.64 per-gun candidates per frame, with 25.75
weapon aim checks, 17.96 line-of-fire queries, and 13.39 scheduled bullets per
frame.

Combat outcomes also changed: the final runs ended with 140 and 130 ships alive
(versus 89 in the control), and 3,259 and 3,855 voxel collision pairs (versus
22,353). These results do not establish equivalent combat lethality; the
reduced AI cost should be weighed against the slower destruction/collision
progression and reviewed when tuning target cadence.

## Seeded follow-up runs

The simulation RNG is now seeded with `20261105` for benchmark modes, and the
large-combat layout retains its separate fixed `20261104` placement seed. The
seed is written to each result's `random_seed` field. Two runs after this
change recorded 49.08 and 49.12 average FPS, 32.55 and 33.06 ms p95 frame time,
and 56.28% and 54.83% of frames below 55 FPS. They averaged 2,751 and 2,782
primary-target candidates and 3.54 and 3.46 gun-target candidates per frame:
[1793874188116](../../benchmark-results/large-combat-vsync-1793874188116.json)
and
[1793874231031](../../benchmark-results/large-combat-vsync-1793874231031.json).

The fixed seed makes the random stream start consistently, but these runs are
not bit-for-bit identical: they ended with 115 versus 122 ships and 6,863
versus 11,613 collision pairs. The benchmark feeds measured wall-clock frame
deltas into simulation updates; different frame timing changes when combat
events occur and therefore how many conditional random draws are consumed.
Fixed-step simulation would improve state-level repeatability, but would also
change benchmark simulation timing and workload, so it remains a separate
option rather than part of this target-cadence change.
