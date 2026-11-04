# Step 1: Early-out line-of-fire queries

## Goal

Reduce the CPU cost of bullet line-of-fire checks without changing which
objects block a shot.

## Baseline evidence

The [persisted baseline](../../benchmark-results/large-combat-vsync-baseline.json)
records 7.275 ms/frame for bullet line-of-fire queries in its slowest
three-second interval. The enclosing AI target-scan-and-firing metric is
17.250 ms/frame and includes this work.

[`Gun::isBulletPathClear`](../../src/equipment/weapons/gun.cpp) only needs to
know whether a blocking result exists. It currently iterates the complete set
returned by
[`WorldTreeQuery::intersectingWorldObjects`](../../src/worldtree/worldtreequery.cpp),
which collects intersecting objects into an `unordered_set`.

## Planned change

Add a query form that visits intersecting objects and can stop once the existing
line-of-fire rejection rules identify a blocker. Do not stop at any geometric
intersection: preserve the current owner and friendly-fire semantics. Avoid
building a result set for this boolean decision.

## Assumed improvement

Hypothesis: reduce the line-of-fire metric by **25-50%**, or about
**1.8-3.6 ms/frame** in the baseline's worst window. Confidence is medium-low
because the current profile does not separate world-tree traversal, voxel
tests, and set construction. This estimate overlaps the enclosing AI metric.

## Validation

- Add or extend tests for owner, friendly, enemy, and non-ship intersections,
  including multiple blockers and no-blocker paths.
- Run at least three comparable large-combat benchmarks before and after.
- Compare the median of per-run average timings and worst-window line-of-fire
  timing; also verify firing rate and damage outcomes.
