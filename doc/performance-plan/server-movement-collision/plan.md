# Server movement and collision optimization

## Goal and baseline

Reduce authoritative movement/collision CPU while preserving continuous,
stepped collision behavior and voxel damage. Baseline large-combat run
`1793966367071` measured:

- `movement_collision_detection`: 2.767 ms average, 7.214 ms p95.
- `world_logic`: 3.219 ms average.
- The world contains 1,420 entities and the simulation targets 60 Hz.

`Movement::perform` first traverses the world tree with `areGeodesNear` and,
when any candidate exists, traverses it again with `nearGeodes` to build the
set used for voxel collision checks. Each object movement can therefore repeat
the broadphase query.

## Plan

1. Preserve the current query shape, collision filter, sweep AABB, angular and
   translation substeps, and voxel-level narrow phase.
2. Refactor movement so one broadphase traversal both discovers candidates
   and supplies them to every substep. Keep the empty-candidate fast path and
   ensure recursively split long movements query their own segment bounds.
3. Avoid unrelated physics changes such as changing step size, skipping
   stationary-object collision checks, or approximating projectile collision.
4. Add or extend focused movement/collision regression coverage for clear
   movement, blocked movement, and a long/split movement; verify collision
   accumulation and final transform are unchanged.
5. The combined-query implementation passed all 157 tests, including the
   authoritative ship-collision reconciliation test, but did not meet the
   performance target. In two runs, movement/collision measured 2.888 and
   2.931 ms average (p95 7.895 and 8.042 ms), versus 2.601 ms average (p95
   7.241 ms) immediately before the experiment.
6. Reverted the change rather than retaining a measured regression or adding a
   new broadphase structure without evidence. The broadphase optimization is
   stopped as not fruitful in its tested form; movement/collision remains the
   original implementation.

## Validation command

```sh
cmake -DMODE=Run -DRUN=MultiplayerLargeCombat -P tools/agent-check.cmake
```
