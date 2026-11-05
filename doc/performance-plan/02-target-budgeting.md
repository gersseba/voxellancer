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
- `CruiserFightTask` now invokes the base target-list maintenance and selection
  path; it previously relied on an unset primary target.
- Benchmark JSON records per-frame candidate, refresh, switch, assignment,
  aim-check, line-of-fire, and scheduled-bullet counts.

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

The final runs are
[1793871212887](../../benchmark-results/large-combat-vsync-1793871212887.json)
and
[1793871265568](../../benchmark-results/large-combat-vsync-1793871265568.json);
the control is
[large-combat-vsync-context-targeting-control.json](../../benchmark-results/large-combat-vsync-context-targeting-control.json).
In the last run, target selection evaluated an average of 2,907 primary-target
candidates and 3.64 per-gun candidates per frame, with 25.75 weapon aim checks,
17.96 line-of-fire queries, and 13.39 scheduled bullets per frame.

Combat outcomes also changed: the final runs ended with 140 and 130 ships alive
(versus 89 in the control), and 3,259 and 3,855 voxel collision pairs (versus
22,353). These results do not establish equivalent combat lethality; the
reduced AI cost should be weighed against the slower destruction/collision
progression and reviewed when tuning target cadence.
