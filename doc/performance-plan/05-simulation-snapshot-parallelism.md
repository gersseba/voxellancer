# Step 5: Evaluate snapshot-based simulation parallelism

## Goal

Only if the preceding algorithmic changes leave simulation CPU time dominant,
parallelize independent movement and AI work without allowing concurrent
mutation of the world tree or gameplay state.

## Baseline evidence

The [persisted baseline](../../benchmark-results/large-combat-vsync-baseline.json)
shows substantial CPU work in movement/collision and AI firing during its
slowest interval. The current
[`Mover::moveWorldObjects`](../../src/world/handler/mover.cpp) advances world
objects sequentially, and movement updates transforms and spatial data as it
goes. Those dependencies make direct parallel calls unsafe and order-sensitive.

## Planned change

First measure which update work is independent and identify all shared writes.
If justified, introduce an immutable per-tick snapshot of transforms and
collision bounds. Compute predicted movement and candidate pairs in parallel,
then synchronize, resolve contacts deterministically, and apply transforms,
spawns, and damage through a controlled mutation phase. Keep world-tree updates
and other shared state out of parallel jobs.

This is a larger architectural change. Do not combine it with the first
algorithm experiments, and do not assume that adding worker threads alone will
improve frame time.

## Assumed improvement

Conditional hypothesis: if at least 60% of the remaining hot CPU update work is
independent and 4 or more cores can be kept busy, reduce CPU simulation wall
time by **20-40%**. Confidence is low, and end-to-end FPS improvement may be
smaller because rendering and VSync also contribute to frame time. The baseline
does not yet quantify the parallel fraction, so this is a target to validate,
not a committed forecast.

## Validation

- Preserve deterministic update ordering or define and test the new ordering.
- Add stress tests for simultaneous movement, collision, death, spawn, and
  damage; run race-detection tooling where available.
- Compare simulation outputs and repeated benchmark distributions against the
  serial implementation, not just average FPS.
