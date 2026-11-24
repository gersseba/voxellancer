# Server interest and snapshot work optimization

## Goal and baseline

Reduce snapshot CPU and unnecessary per-client state while keeping snapshots
authoritative and each client's actor-centered interest rules intact. Baseline
large-combat run `1793966367071` measured:

- `server_poll`: 3.769 ms average, 12.708 ms p95.
- Snapshot build: 2.327 ms average on 600 sampled intervals; delta: 1.005 ms.
- Each client received about 40 MB. Of 1,568,534 considered entity entries,
  1,567,086 passed the current interest filter (0.092% culled).
- Large-combat startup currently uses a 1,500-unit interest radius; ordinary
  server startup uses 650 units.

The current send path materializes a full world snapshot separately for each
connection before applying that connection's radius and delta baseline.

## Plan

1. First remove duplicate work without changing visibility: materialize the
   authoritative entity snapshot once per broadcast interval and pass the
   immutable state to each connection's existing interest filter, delta
   builder, and encoder. Keep per-connection baselines, actor inclusion,
   sequence numbers, removals, content sends, and counters correct.
2. Add/extend server tests with two clients at different actor positions. Verify
   each receives only entities inside its own interest radius, actor state is
   always present, and entities leaving range are removed.
3. Re-run the benchmark and verify snapshot-build time falls while client
   entity counts, snapshot ordering, and authoritative state remain correct.
4. Then test a smaller large-combat benchmark radius (starting at 1,000 units)
   as an explicit area-of-interest experiment. Do not change the ordinary
   server's 650-unit default. Compare visible entities and bytes/client, and
   reject the radius change if it hides important combat participants or
   undermines the benchmark's intended shared battle view.
5. Keep the radius experiment only if it reduces server snapshot work or
   per-client bytes by at least 20% while preserving the controlled actors and
   useful combat visibility. Otherwise retain the 1,500-unit benchmark radius
   and keep only the behavior-preserving shared-snapshot optimization.

## Validation command

```sh
cmake -DMODE=Run -DRUN=MultiplayerLargeCombat -P tools/agent-check.cmake
```

## Results

- Shared snapshot construction is implemented for lobby start and each
  snapshot broadcast. Per-client interest filtering, delta baselines, content,
  equipment, and sequence state remain independent.
- The two-client test now places actors 1,000 units apart and verifies each
  client receives its own actor and nearby entity, but not the other player's
  entities or a distant entity. The existing single-client interest test
  continues to cover entities leaving range.
- `all_tests` passed (157 tests).
- With the original 1,500-unit radius, benchmark `1793973280874` measured
  `server_snapshot_build` at 1.348 ms average, down from the 2.327 ms baseline
  (42% lower). `server_poll` was 3.402 ms average. Client traffic remained
  about 40 MB per client.
- The 1,000-unit radius experiment (`1793973366866`) reduced average traffic
  to about 35 MB per client (12.8% lower) and the final visible entity count
  from 1,368/1,369 to 1,209/1,220. Snapshot delta time fell from 1.019 ms to
  0.843 ms, but neither snapshot-build time nor traffic met the 20% keep
  threshold, while the reduced radius removed about 12% of the visible
  entities. `server_poll` also fell 19.4%, but total world and ship counts
  differed between runs, so that end-to-end change is not isolated to the
  radius. The experiment was reverted; both large-combat startup paths retain
  the 1,500-unit radius and ordinary servers retain 650 units.
- The final merged benchmark `1793974750041` confirmed the shared-build result:
  `server_snapshot_build` averaged 1.184 ms, `server_snapshot_delta` 0.870 ms,
  and `server_poll` 2.783 ms. Each client received about 40.0 MB, consistent
  with preserving the original 1,500-unit visibility range.
