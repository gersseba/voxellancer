---
type: workflow
title: Data-defined content authoring
description: Explains INI property loading and how editable world-object, equipment, voxel-cluster, and script data becomes runtime content.
tags: [content, properties, ini, voxel, authoring]
sources:
  - id: openwiki-source-05e3ba2913d8b16b067f806a
    resource: repo://data/worldobjects/mox.ini
  - id: openwiki-source-d364d949938a433276255c32
    resource: repo://src/main.cpp
  - id: openwiki-source-c77ad3ab0ea096c2ad79f31f
    resource: repo://src/property/propertydirectory.cpp
  - id: openwiki-source-62e72948d6c6a59756eeb9ae
    resource: repo://src/property/propertymanager.cpp
  - id: openwiki-source-ed5754f5e6bd466a90c9d3a3
    resource: repo://src/resource/clustercache.cpp
  - id: openwiki-source-f59b1c47d9e37da164ed7197
    resource: repo://src/resource/worldobjectbuilder.cpp
  - id: openwiki-source-e4211f98d6018a8c0ff3ef1d
    resource: repo://src/resource/worldobjectbuilder.h
  - id: openwiki-source-8fe83e722ee9002df9b0f910
    resource: repo://src/scenarios/basescenario.cpp
  - id: openwiki-source-f3b131cfec992caceffcc7b6
    resource: repo://src/server/main.cpp
  - id: openwiki-source-1e98e39d6f94af77288526a1
    resource: repo://test/scripting/testscripting.cpp
generated: { by: "copilot", at: "2026-11-05T22:43:09.174Z" }
verified:
  - by: openwiki/0.7.2
    at: 2026-11-05T22:43:09.174Z
---

# Data-defined content authoring

Voxellancer keeps reusable game content in `data/` and loads it through the
property system. Graphical startup loads configuration, controls, voxel
definitions, and the world-object and equipment directories. The headless
server also loads `data/config.ini`, `data/voxels.ini`, and those same content
directories before creating its server-mode world. `PropertyDirectory` loads
each `.ini` file under the filename basename prefix, so
`data/worldobjects/mox.ini` supplies `mox.*` property groups.

## INI properties

`PropertyManager` recognizes typed values including numbers, booleans, strings,
vectors, input mappings, and string lists. INI section headings form property
groups, and entries are stored as `<prefix>.<section>.<key>`. Invalid or
unsupported typed values are warned about; an unreadable property file is fatal
and raises an exception.

World-object definitions use a `[general]` section to choose a type and voxel
cluster. For example, `mox.ini` declares a ship backed by `mox.csv`, then names
mountable shields, weapons, and engines through slot and group sections.

## Building objects from content

`WorldObjectBuilder(name)` dispatches on `<name>.general.type` to create bullets,
rockets, ships, or generic world objects. It reads scale and the cluster filename,
then asks `ClusterCache` to fill the object from
`data/voxelcluster/<filename>`. The builder configures hardpoints and engine
slots from indexed `hardpointGroupN` and `engineslotGroupN` properties, and
shield slots from `shieldslotN` groups. Each slot accepts a `mountable` list;
the builder equips its first entry by default. For example, `mox.ini` maps a
shield slot, weapon groups, and an engine group to equipment definitions.

`ClusterCache` loads a source voxel vector once per filename and clones it into
each new cluster or object; object filling uses color-coded clones. Edit CSV
clusters under `data/voxelcluster` for physical voxel layouts and their associated
INI files under `data/worldobjects` for object behavior and equipment.

## Server content and world ownership

World-object data is shared simulation input, not client-only presentation.
`voxellancer-server` loads the same world-object and equipment INI directories,
then scenarios build objects for their own `World`. `BaseScenario` supplies a
world-bound `WorldObjectBuilder`; building a ship or rocket without an explicit
world throws instead of silently assigning it to the global client world. The
server tests construct `BattleScenario` with a server-mode world and verify that
its ships belong to that world. Keep scenario-created ships and equipment tied
to the scenario's world when adding or changing content.

## Related content

Lua scenario and mission scripts live in `data/scripts`; their runtime API and
lifecycle are documented in [Lua scripting](../integrations/lua-scripting.md).
Changing voxel arrangements affects the damage and splitting behaviors described
in [voxel ships and damage](../concepts/voxel-ships-and-damage.md).
