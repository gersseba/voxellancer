# Client remote-replica update optimization

## Goal and baseline

Optimize client work without moving authoritative simulation to the client or
changing interpolation, prediction, hit effects, or replicated voxel state.
The baseline is large-combat run `1793966367071`:

- `client-1`: `client_replica_update` 8.280 ms average; 17.309 ms average frame.
- `client-2`: `client_replica_update` 7.683 ms average; 17.092 ms average frame.
- The per-frame counter reports about 1,293–1,300 remote content entities visited.

The measured scope is `GamePlayScene::updateRemoteVoxelProxies`. It currently
searches the sampled snapshot vector and proxy vector for every content entry,
then examines voxel state even when the network content has not changed.

## Plan

1. Trace every `TcpClient` mutation of entity contents: initial content,
   equipment updates, voxel damage/removal, entity removal, and resync. Record
   the affected entity IDs in a deduplicated local dirty list; do not add wire
   data or change authoritative replica ownership.
2. Expose a drain operation through `GamePlayRunning`. Ensure the first active
   frame performs a full content sync, and later frames reconcile only entities
   reported dirty by the client.
3. In `GamePlayScene`, build one per-frame entity-ID-to-snapshot index for
   sampled transforms and maintain an entity-ID-to-proxy index for stable
   proxy ownership. Use these indexes for content updates, effects, and
   stale-proxy cleanup instead of repeated linear searches.
4. Reconcile voxel/equipment content only for dirty IDs. Continue updating
   proxy transforms from interpolated snapshots every frame. Keep dirty IDs
   pending until their sampled snapshot exists, so an entity entering the
   interpolation window after its content packet is still created. Preserve
   cluster rebuilds for topology/material/equipment changes and incremental
   damage application for lower HP.
5. Add focused coverage to the server/client integration test for initial
   content, entering interest, and leaving interest; run `all_tests`.
6. Results: all 157 tests pass. Benchmark run `1793972823052` reduced replica
   update from 8.280 to 0.387 ms on client-1 (95.3%) and from 7.683 to 0.365
   ms on client-2 (95.2%). Frame p95 fell from 22.394 to 16.876 ms and 20.157
   to 16.874 ms, respectively. Deadline-miss counts remain near 49% because
   swap/vsync pacing makes frames hover around the strict 16.67 ms threshold;
   p95 and p99 nevertheless improved substantially.
7. Keep this change: both clients exceed the 20% target by a wide margin, with
   unchanged 1,800-interval benchmark completion and no integration-test
   regression.

The merged result was rechecked in run `1793974750041`: replica update averaged
0.365 ms on client-1 and 0.354 ms on client-2. Client-2 reported 1,797 of the
1,800 intervals; both clients completed successfully.

## Validation command

```sh
cmake -DMODE=Run -DRUN=MultiplayerLargeCombat -P tools/agent-check.cmake
```

The server remains authoritative; do not add client-side combat or AI updates.
