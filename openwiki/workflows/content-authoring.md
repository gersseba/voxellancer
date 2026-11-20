---
type: workflow
title: Data-defined content authoring
description: Explains INI property loading and how editable world-object, equipment, voxel-cluster, and script data becomes runtime content.
tags: [content, properties, ini, voxel, authoring]
sources:
  - id: openwiki-source-35bd28a41bcfa5335465d092
    resource: repo://data/equipment/shields/thinshield.ini
  - id: openwiki-source-05e3ba2913d8b16b067f806a
    resource: repo://data/worldobjects/mox.ini
  - id: openwiki-source-d364d949938a433276255c32
    resource: repo://src/main.cpp
  - id: openwiki-source-042d34128dd3110958321569
    resource: repo://src/property/propertycollection.inl
  - id: openwiki-source-c77ad3ab0ea096c2ad79f31f
    resource: repo://src/property/propertydirectory.cpp
  - id: openwiki-source-62e72948d6c6a59756eeb9ae
    resource: repo://src/property/propertymanager.cpp
  - id: openwiki-source-ed5754f5e6bd466a90c9d3a3
    resource: repo://src/resource/clustercache.cpp
  - id: openwiki-source-4e54ad39628129720e79bf5b
    resource: repo://src/resource/clusterloader.cpp
  - id: openwiki-source-ecba07e7ff1617238fcf1460
    resource: repo://src/resource/colorcoder.cpp
  - id: openwiki-source-42346af5c16e3281a0739574
    resource: repo://src/resource/shieldbuilder.cpp
  - id: openwiki-source-f59b1c47d9e37da164ed7197
    resource: repo://src/resource/worldobjectbuilder.cpp
  - id: openwiki-source-8fe83e722ee9002df9b0f910
    resource: repo://src/scenarios/basescenario.cpp
  - id: openwiki-source-f3b131cfec992caceffcc7b6
    resource: repo://src/server/main.cpp
  - id: openwiki-source-7699a5d553f90bde4dd73218
    resource: repo://src/utils/directoryreader.cpp
  - id: openwiki-source-bd4b1081566fcaeb3305fe3d
    resource: repo://test/equipment/testshield.cpp
  - id: openwiki-source-f6d73f7f5f0aa6b26caea361
    resource: repo://test/loader/testclusterloader.cpp
  - id: openwiki-source-bfc735ecd358e18b58f4789b
    resource: repo://test/property/testproperty.cpp
generated: { by: "copilot", at: "2026-11-19T09:46:20.407Z" }
verified:
  - by: openwiki/0.7.2
    at: 2026-11-19T09:46:20.407Z
---

# Data-defined content authoring

Reusable simulation content lives under `data/` and is loaded by both the
graphical client and headless server. Startup loads `data/config.ini`,
`data/voxels.ini`, and the world-object and equipment directories;
`PropertyDirectory` scans regular files directly inside the requested directory
and loads `.ini` files. It prefixes each file's properties with its basename, so
[`mox.ini`](../../data/worldobjects/mox.ini) defines keys such as
`mox.general.type`.

## INI property model

Section headings become groups and each `key=value` entry is stored as
`<prefix>.<section>.<key>`. `PropertyManager` has typed collections for
numbers, booleans, strings, vectors, input mappings, and string lists. The
property system does not define a schema for each content file: the builder
determines which properties are required and whether they have defaults.
Explicit defaults are used for optional properties such as
`general.scale` (1.0); unreadable files log a fatal error and throw.

## Voxel clusters and world objects

`WorldObjectBuilder` dispatches on `<name>.general.type`, which can be
`bullet`, `rocket`, `ship`, or `other`. The object points to a cluster filename
in `general.voxelcluster`; the builder loads it from
`data/voxelcluster/<filename>`. `general.scale` defaults to 1.0. Missing cluster
files fail during loading rather than producing a usable empty object, and an
unknown object type is fatal.

`ClusterLoader` reads CSV clusters and ZOX files. A CSV begins with its
dimensions, followed by color-coded cells; black cells are empty. `ClusterCache`
loads each source vector once per filename and clones voxels into each object or
cluster. For world objects, the clones pass through `ColorCoder`, which turns
recognized encoded colors into engine-slot, hardpoint, cockpit, fuel, and
crucial voxels. Their prefixes are configured in `data/voxels.ini`.

Ship definitions must set `general.ship_type` to `fighter` or `cruiser`, and
`general.shield_type` to `HULL`, `SPHERE`, or `NONE`; unrecognized values throw
`std::invalid_argument`. These are separate settings: the former classifies the
ship, while the latter selects shield behavior. A ship with
`shield_type=NONE` can still define shield equipment slots, but disables
projectile interception geometry. A missing crucial-voxel or cockpit marker
produces a warning during ship construction, not a build failure.

## Equipment slots and shields

Colored hardpoint and engine-slot voxels carry group indexes. The matching
`hardpointGroupN` properties define `direction`, `fieldOfAim`, and the
`mountable` weapon list; `engineslotGroupN` defines `direction` and mountable
engines. The builder installs the first mountable entry in each nonempty slot.
Shield slots are property groups named `shieldslot0`, `shieldslot1`, and so on;
number them contiguously from zero because construction stops at the first
missing group. Each group can list mountable shields.

The [`mox.ini`](../../data/worldobjects/mox.ini) definition demonstrates the
full path: it names a CSV cluster, selects fighter and spherical-shield
behavior, maps weapon and engine groups, and mounts `thinshield`. A shield
definition such as [`thinshield.ini`](../../data/equipment/shields/thinshield.ini)
provides `general.hp` and `general.regeneration`; `ShieldBuilder` uses those
values to initialize the shield's maximum and current HP.

## Loading, validation, and world ownership

The client loads world-object, engine, shield, weapon, and projectile INI
directories. Server startup loads the same simulation data before creating its
server-mode `World`. Ship and rocket construction requires an explicit `World`;
`BaseScenario`'s builder helper supplies the scenario's own world instead of
falling back to the global client world.

The focused tests cover CSV and ZOX parsing in
[`testclusterloader.cpp`](../../test/loader/testclusterloader.cpp), shield
geometry and loading ship shield types from definitions in
[`testshield.cpp`](../../test/equipment/testshield.cpp). The property tests in
[`testproperty.cpp`](../../test/property/testproperty.cpp) exercise typed
properties, defaults, and file reloads. These tests and the builders are the
authority for required fields and invalid-definition behavior; sample assets
alone do not define a schema.

Lua scenario and mission scripts live in `data/scripts`; their runtime API and
lifecycle are documented in [Lua scripting](../integrations/lua-scripting.md).
Changing voxel arrangements affects the damage and splitting behaviors described
in [voxel ships and damage](../concepts/voxel-ships-and-damage.md).
