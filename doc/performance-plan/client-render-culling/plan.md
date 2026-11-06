# Client remote-object render culling

## Goal and baseline

Reduce client draw submissions for remote voxel proxies without changing the
replica data needed for networking, interpolation, or effects. Baseline
large-combat run `1793966367071` measured:

- `world_object_draw`: 2.070 ms average on `client-1`, 1.715 ms on `client-2`.
- About 1,300 remote content entities are visited per frame.
- The benchmark intentionally places a free camera to view the large combat
  scene, so many or all ships may be in view.

## Plan

1. Confirm available world-space proxy bounds and camera view-projection
   conventions. Prefer a conservative frustum-sphere test over an arbitrary
   draw-distance cutoff; false negatives must not hide visible ships.
2. Cull only draw submissions for proxies outside the camera frustum. Do not
   remove snapshots, content, collision state, or hit events.
3. Add a focused test for spheres inside, outside, and intersecting each
   frustum plane if the helper can be isolated cleanly. Otherwise validate the
   conservative geometry manually and run the complete test suite.
4. Run the two-player large-combat benchmark and compare each client's
   `world_object_draw` and frame p95/deadline misses. Keep the change only if it
   culls a meaningful number of clusters and improves draw CPU by at least 10%
   without visual correctness regressions.
5. If the benchmark camera includes nearly every ship and the draw phase does
   not improve, remove the experimental culling code and document this as an
   unproductive optimization for this workload.

## Validation command

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\agent-check.ps1 -Mode Run -Benchmark MultiplayerLargeCombat
```

## Results

- A tested six-plane frustum sphere check was applied only to remote proxy draw
  submissions; replica state and update behavior were untouched. Its geometry
  tests passed as part of a 158-test run.
- The two-client benchmark `1793973535882` reported zero remote proxies culled
  on either client. Client `world_object_draw` averaged 2.394/2.382 ms, versus
  2.222/2.216 ms in the preceding 1,500-unit-radius run; frame p95 remained
  essentially unchanged at 17.042/17.045 ms. Thus the check adds work without
  reducing submissions in this camera setup.
- Following the stop criterion, the experimental culling implementation,
  counters, and unit test were removed. The measurements remain summarized
  here; intermediate report files were removed when retaining only the final
  merged benchmark. No render-culling change is retained.
