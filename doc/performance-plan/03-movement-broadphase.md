# Step 3: Reuse movement broadphase results

## Goal

Avoid redundant world-tree traversal during collision-aware movement while
preserving the set of possible colliders.

## Baseline evidence

The [persisted baseline](../../benchmark-results/large-combat-vsync-baseline.json)
records **19.154 ms/frame** for movement and voxel collision detection in its
slowest three-second interval. Projectile movement is a nested
**14.686 ms/frame**, so the parent value must not be treated as an independent
cost.

[`Movement::perform`](../../src/physics/movement.cpp) first calls
`areGeodesNear()` for the phase AABB. When it finds a nearby geode,
`performStepped()` queries the same broadphase again through `nearGeodes()` to
build the candidate set.

## Planned change

Use one traversal per movement interval to determine whether candidates exist
and collect them for narrow-phase checks. Prefer reusable vector/scratch
storage and a visitor over allocating an `unordered_set` per query when
possible. Preserve the current collision filter, world-tree hint, and candidate
coverage for each split movement interval.

## Assumed improvement

Hypothesis: save **0.2-0.8 ms/frame** in the baseline's worst window by
eliminating redundant traversal and candidate-container overhead. Confidence is
low because broadphase time is not measured separately and most movement time
is nested projectile work.

## Validation

- Add counters/timings for broadphase traversals, visited nodes, candidate
  geodes, and narrow-phase checks.
- Verify identical candidate coverage and collision outcomes in movement and
  split-movement tests.
- Compare repeated benchmark runs; keep this change only if traversal counts or
  timings improve without missed collisions.
