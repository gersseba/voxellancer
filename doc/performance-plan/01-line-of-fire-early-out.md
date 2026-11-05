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

## Implementation and benchmark result

Implemented `WorldTreeQuery::anyIntersectingWorldObject`, which evaluates the
existing voxel intersection test and stops traversal when the predicate accepts
a blocker. The query no longer constructs a result `unordered_set`, and its
recursive callback is passed by reference rather than copied at each tree
level. `Gun::isBulletPathClear` now uses the predicate while preserving the
owner and friendly-ship rejection rules. A world-tree test verifies both
short-circuiting and a full no-match traversal.

The Release build and test harness passed: **98 tests passed, 3 skipped**.
Two 1,800-frame runs were captured after 120 warm-up frames. Compared with the
persisted baseline, their line-of-fire worst-window CPU timings were 6.651 and
6.255 ms/frame, versus 7.275 ms/frame in the baseline: a mean reduction of
**11.3%** (0.822 ms/frame). The enclosing target-scan-and-firing timing averaged
16.561 ms/frame across the two runs, versus 17.250 ms/frame in the baseline
(4.0% lower). These are single-run baseline and two post-change samples, so
combat divergence and benchmark variability prevent a strong causal claim.

| Run | Average FPS | p95 frame ms | Below 55 FPS | Final ships | Worst 3s line-of-fire CPU ms/frame |
|---|---:|---:|---:|---:|---:|
| Baseline | 45.44 | 35.95 | 66.89% | 91 | 7.275 |
| Early-out 1 | 45.47 | 32.35 | 82.67% | 174 | 6.651 |
| Early-out 2 | 45.90 | 33.35 | 71.17% | 101 | 6.255 |

Overall average FPS was effectively unchanged relative to the baseline. The
task-level result is promising but modest; keep the optimization because it
preserves query semantics and avoids materializing a set, while treating its
end-to-end frame-rate effect as unproven. More samples with matched combat
activity would improve confidence.

Benchmark artifacts:

- [Baseline](../../benchmark-results/large-combat-vsync-baseline.json)
- [Early-out run 1](../../benchmark-results/large-combat-vsync-line-query-1.json)
- [Early-out run 2](../../benchmark-results/large-combat-vsync-line-query-2.json)

Alternatives for later discussion: order candidates toward likely blockers or
use a cheaper conservative visibility test before voxel checks. Both can change
query ordering or introduce false positives, so they need separate correctness
and combat-outcome validation.
