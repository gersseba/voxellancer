---
type: workflow
title: Data-defined content authoring
description: Explains INI property loading and how editable world-object, equipment, voxel-cluster, and script data becomes runtime content.
tags: [content, properties, ini, voxel, authoring]
sources:
  - id: openwiki-source-05e3ba2913d8b16b067f806a
    resource: repo://data/worldobjects/mox.ini
  - id: openwiki-source-c77ad3ab0ea096c2ad79f31f
    resource: repo://src/property/propertydirectory.cpp
  - id: openwiki-source-62e72948d6c6a59756eeb9ae
    resource: repo://src/property/propertymanager.cpp
  - id: openwiki-source-ed5754f5e6bd466a90c9d3a3
    resource: repo://src/resource/clustercache.cpp
  - id: openwiki-source-f59b1c47d9e37da164ed7197
    resource: repo://src/resource/worldobjectbuilder.cpp
generated: { by: "copilot", at: "2026-11-01T22:49:09.563Z" }
verified:
  - by: openwiki/0.7.2
    at: 2026-11-03T15:48:08.697Z
---

# Data-defined content authoring

Voxellancer keeps reusable game content in `data/` and loads it through the
property system. Startup loads `data/config.ini`, user controls, and
`data/voxels.ini`; it then scans the world-object and equipment directories.
`PropertyDirectory` loads every `.ini` file in a directory under the filename
basename prefix, so `data/worldobjects/mox.ini` supplies `mox.*` property groups.

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
`data/voxelcluster/<filename>`. The builder configures hardpoints, engine slots,
and shield slots from matching property groups and equips the first mountable
item in each configured slot.

`ClusterCache` loads a source voxel vector once per filename and clones it into
each new cluster or object; object filling uses color-coded clones. Edit CSV
clusters under `data/voxelcluster` for physical voxel layouts and their associated
INI files under `data/worldobjects` for object behavior and equipment.

## Related content

Lua scenario and mission scripts live in `data/scripts`; their runtime API and
lifecycle are documented in [Lua scripting](../integrations/lua-scripting.md).
Changing voxel arrangements affects the damage and splitting behaviors described
in [voxel ships and damage](../concepts/voxel-ships-and-damage.md).
