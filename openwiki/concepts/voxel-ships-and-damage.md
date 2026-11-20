---
type: concept
title: Voxel ships, collision, and damage
description: Explains voxel-cluster composition, damage propagation, structural splitting, and the mechanics tests that protect them.
tags: [voxel, ships, collision, damage, splitting]
sources:
  - id: openwiki-source-73209b501dbd294eb50ebb1b
    resource: repo://data/worldobjects/basicship.ini
  - id: openwiki-source-81c84cf80afa3e3814d0ea15
    resource: repo://src/equipment/shield.cpp
  - id: openwiki-source-f59b1c47d9e37da164ed7197
    resource: repo://src/resource/worldobjectbuilder.cpp
  - id: openwiki-source-b32fdfef4d9b74807d5e47a1
    resource: repo://src/scenarios/shieldshowcasescenario.cpp
  - id: openwiki-source-bd9ee3e5f4c4b40eba3d2469
    resource: repo://src/server/tcpserver.cpp
  - id: openwiki-source-5755e4066d8d2af940b39780
    resource: repo://src/simulation/simulationsession.cpp
  - id: openwiki-source-f24660fa12f0640c1e45b927
    resource: repo://src/voxel/voxelcluster.cpp
  - id: openwiki-source-f7e5fede52610a1d712e4564
    resource: repo://src/world/handler/damager.cpp
  - id: openwiki-source-3aa1116faa79d5bcbae4005d
    resource: repo://src/world/handler/mover.cpp
  - id: openwiki-source-ed9e87671f939df658b627c4
    resource: repo://src/world/handler/splitdetector.cpp
  - id: openwiki-source-e87ba6349c61cb4b332a392c
    resource: repo://src/world/handler/splitter.cpp
  - id: openwiki-source-d822b999e031aa3b7a0868ab
    resource: repo://src/world/world.cpp
  - id: openwiki-source-89530116c908dec0f6883227
    resource: repo://src/worldobject/worldobjectcomponents.cpp
  - id: openwiki-source-bd4b1081566fcaeb3305fe3d
    resource: repo://test/equipment/testshield.cpp
generated: { by: "copilot", at: "2026-11-19T09:46:20.407Z" }
verified:
  - by: openwiki/0.7.2
    at: 2026-11-19T09:46:20.407Z
---

# Voxel ships, collision, and damage

A `WorldObject` is a `VoxelCluster`: voxels are stored by integer cell coordinate,
with bounds updated and client render data invalidated as voxels change. Cluster
ownership is explicit: removing a voxel updates bounds, invalidates render data
when present, and deletes that voxel. `WorldObject` additionally synchronizes
each voxel insertion/removal with physics and collision detection, so a damaged
ship's rendered, physical, and collision shapes remain aligned.

In server play, these mechanics execute in the authoritative world. The server
replicates voxel damage, removal, and shield-hit events for entities in a
connection's current snapshot baseline; clients do not decide collision or
structural outcomes. Repeated damage updates for one voxel are coalesced to its
latest HP before transmission. See
[client-server simulation](../architecture/client-server-simulation.md) for the
replication path.

## Shields

Ship definitions select `shield_type=HULL`, `SPHERE`, or `NONE`; the builder
rejects unknown values. A hull shield tests the swept projectile segment against
exposed voxels (a voxel is exposed when at least one of its six face-neighbors
is absent). A sphere shield tests against the ship's bounding sphere expanded
by the projectile radius. `NONE` skips shield interception.

Projectile interception registers a discrete shield hit. A shield starts with a
five-hit capacity; each registered hit consumes an equal share of its maximum
HP, and reaching the hit limit permanently depletes it. The capacity can be
overridden, as the shield showcase does. Separately, collision and other damage
is passed through equipped shields before reaching hull voxels. This damage
reduces shield HP and the shield can regenerate until its discrete hit capacity
is exhausted. Tests cover each geometry, depletion, damage absorption, and
loading the configured ship shield types.

## From impact to removed voxels

World collision processing turns impacts into `DamageImpact` values. `Damager`
first allows the target's components (including shields) to compensate damage.
Surviving damage reduces voxel hit points; a voxel at or below zero HP becomes a
dead voxel, is recorded as a modification of its owner, and yields both normal and
damped deadly impacts. The world logic forwards damage and destruction hooks until
the current forwarded-impact list is empty, then removes the destroyed voxels.

The `general.playerShipUndestroyable` property bypasses damage only when a
presentation-enabled world has a current player ship and the impact targets that
ship. Headless and server-mode worlds have no client player for this guard, so
server-side protection must be modeled explicitly rather than relying on this
client-player property. Component behavior otherwise affects both the amount of
damage and whether structural processing sees a removed voxel.

## Structural splitting

After damage, `SplitDetector` examines only modified objects with more than one
voxel. It flood-fills each occupied group in the object's bounded voxel grid using
face and edge neighbors (18-connected adjacency; corner-only contact does not
join groups). The group containing the crucial voxel stays with the original
object; without a crucial voxel, the largest group stays. Every other group
becomes split data.

`Splitter` creates a new `WorldObject` for each split-off, clones its voxels,
and removes the source voxels. World lifecycle processing subsequently schedules
those new objects for spawning. This keeps extraction separate from spatial
registration and collision validation.

## Tests and extension points

Focused Bandit suites in `test/equipment/testshield.cpp`, `test/voxelcluster`,
`test/damageforwarder`, and `test/splitting` cover shield geometry and
depletion, voxel data changes, damage forwarding, and connectivity boundaries.
Server-side damage and shield-event replication are exercised in
`test/server/testtcpserver.cpp`. Add mechanics coverage beside those suites when
changing damage or grouping rules, and use
[world simulation](../architecture/world-simulation.md) for the enclosing
update order.
