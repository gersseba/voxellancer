---
type: concept
title: Voxel ships, collision, and damage
description: Explains voxel-cluster composition, damage propagation, structural splitting, and the mechanics tests that protect them.
tags: [voxel, ships, collision, damage, splitting]
sources:
  - id: openwiki-source-f24660fa12f0640c1e45b927
    resource: repo://src/voxel/voxelcluster.cpp
  - id: openwiki-source-f7e5fede52610a1d712e4564
    resource: repo://src/world/handler/damager.cpp
  - id: openwiki-source-ed9e87671f939df658b627c4
    resource: repo://src/world/handler/splitdetector.cpp
  - id: openwiki-source-e87ba6349c61cb4b332a392c
    resource: repo://src/world/handler/splitter.cpp
generated: { by: "copilot", at: "2026-11-01T22:49:09.563Z" }
verified:
  - by: openwiki/0.7.2
    at: 2026-11-03T15:48:08.697Z
---

# Voxel ships, collision, and damage

A `WorldObject` is a `VoxelCluster`: voxels are stored by integer cell coordinate,
with bounds and render data maintained as voxels change. Cluster ownership is
explicit—removing a voxel updates bounds and render data, then deletes that voxel.
`WorldObject` additionally synchronizes each voxel insertion/removal with physics
and collision detection, so a damaged ship's rendered, physical, and collision
shapes remain aligned.

## From impact to removed voxels

World collision processing turns impacts into `DamageImpact` values. `Damager`
first allows the target's components (including shields) to compensate damage.
Surviving damage reduces voxel hit points; a voxel at or below zero HP becomes a
dead voxel, is recorded as a modification of its owner, and yields both normal and
damped deadly impacts. The world logic forwards damage and destruction hooks until
the current forwarded-impact list is empty, then removes the destroyed voxels.

The `general.playerShipUndestroyable` property can bypass damage directed at the
current player ship. Component behavior therefore affects both the amount of damage
and whether structural processing sees a removed voxel.

## Structural splitting

After damage, `SplitDetector` examines only modified objects with more than one
voxel. It flood-fills each occupied group in the object's bounded voxel grid using
face and edge/corner neighbors. The group containing the crucial voxel stays with
the original object; without a crucial voxel, the largest group stays. Every other
group becomes split data.

`Splitter` creates a new `WorldObject` for each split-off, clones its voxels,
and removes the source voxels. World lifecycle processing subsequently schedules
those new objects for spawning. This keeps extraction separate from spatial
registration and collision validation.

## Tests and extension points

Focused Bandit suites in `test/voxelcluster`, `test/damageforwarder`, and
`test/splitting` cover the main data-structure, forwarding, and connectivity
boundaries. Add mechanics coverage beside those suites when changing damage or
grouping rules, and use [world simulation](../architecture/world-simulation.md)
for the enclosing update order.
