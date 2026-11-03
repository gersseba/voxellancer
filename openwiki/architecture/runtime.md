---
type: architecture
title: Application runtime and game states
description: Explains Voxellancer startup, frame dispatch, nested game states, rendering ownership, and current stereo-view behavior.
tags: [runtime, game-state, rendering, startup]
verified:
  - by: openwiki/0.7.2
    at: 2026-11-03T15:48:08.697Z
sources:
  - id: openwiki-source-a9076e43ad02acc175f927f9
    resource: repo://src/display/stereorenderinfo.cpp
  - id: openwiki-source-c1511177dbb2a9aad890cfcb
    resource: repo://src/display/viewer.cpp
  - id: openwiki-source-1a440e915107d39ccb794aed
    resource: repo://src/gamestate/game.cpp
  - id: openwiki-source-11effaa43ec0e9d1206994a9
    resource: repo://src/gamestate/gameplay/gameplay.cpp
  - id: openwiki-source-dd6600ebcef6778c27688c55
    resource: repo://src/gamestate/gameplay/running/gameplayrunning.cpp
  - id: openwiki-source-d364d949938a433276255c32
    resource: repo://src/main.cpp
generated: { by: "copilot", at: "2026-11-03T15:48:08.697Z" }
---

# Application runtime and game states

`main` owns process startup and shutdown. It parses command-line options, loads
the base configuration, user controls, and voxel properties, then initializes
GLFW, the OpenGL context, and GLEW. It loads property-defined world objects and
equipment before constructing `Game`. Paths such as `data/config.ini` are
relative to the repository root.

The `-fullscreen` option requests a fullscreen context. `-nointro` skips the
intro; otherwise the intro is shown only when `general.showIntro` is enabled.
Rendering is mono by default. `-stereo` switches the `Viewer` to its built-in
`StereoView` using `StereoRenderInfo::dummy()` placeholder calibration values.
This generic stereo path does not initialize an Oculus device or SDK.

## Frame loop

The GLFW loop obtains a delta from `glfwGetTime`, calls `Game::update`, draws
the active scene through `Game::draw`, swaps buffers, and polls events. `Game`
clamps the delta to `[0.0001, 0.05]` seconds before forwarding it into its
active state and updating the viewer. Resize events update both the OpenGL
viewport and the viewer viewport; input callbacks are forwarded to whichever
game-state input handler is active.

`Viewer` owns the current mono or stereo `View`. Its `draw` method establishes
back-face culling and clears depth before delegating the scene and camera to
that view.

## State hierarchy

`Game` is the root `GameState` and owns a `Viewer`, `GamePlay`, and `Intro`.
When intros are enabled, the intro's completion trigger transitions to
gameplay; otherwise gameplay is selected immediately. Nested `GameState`
instances delegate scene, camera, and input requests to their parent unless
they override them, which lets the root render the currently active leaf state.

`GamePlay` owns running and paused substates, normal/free-camera input handlers,
the gameplay scene, and sound. Entering gameplay activates sound and loads
scenario 0, the Lua-backed demo scenario. Gameplay updates its selected input
and sound listener, advances the current game state, then updates the scene.
The running substate advances `World`. Free-camera mode switches the camera and
input source, clears player movement, and controls HUD visibility through the
`vfx.drawhud` property.

## Shutdown and change boundaries

After the GLFW loop exits, `main` deletes `Game`, shuts down the context
provider, and terminates GLFW.

- Add runtime configuration or property-defined assets through the property
  loader and data files described in [content authoring](../workflows/content-authoring.md).
- Make simulation changes through the update path documented in
  [world simulation](world-simulation.md), rather than viewer rendering.
- Extend Lua scenarios through [Lua scripting](../integrations/lua-scripting.md);
  gameplay currently selects the demo script by default.
