---
type: architecture
title: Application runtime and game states
description: Explains Voxellancer startup, frame dispatch, nested game states, rendering ownership, and current stereo-view behavior.
tags: [runtime, game-state, rendering, startup]
sources:
  - id: openwiki-source-68180d68a9748362b67ca1a4
    resource: repo://.vscode/tasks.json
  - id: openwiki-source-18f8ead36ee6202ab5e3f24e
    resource: repo://src/client/localserverprocess.cpp
  - id: openwiki-source-a9076e43ad02acc175f927f9
    resource: repo://src/display/stereorenderinfo.cpp
  - id: openwiki-source-c1511177dbb2a9aad890cfcb
    resource: repo://src/display/viewer.cpp
  - id: openwiki-source-a62414eaa8b8051f79fdaef8
    resource: repo://src/etc/cli/commandlineparser.cpp
  - id: openwiki-source-1a440e915107d39ccb794aed
    resource: repo://src/gamestate/game.cpp
  - id: openwiki-source-11effaa43ec0e9d1206994a9
    resource: repo://src/gamestate/gameplay/gameplay.cpp
  - id: openwiki-source-c9d05f4c55af65127be86b1f
    resource: repo://src/gamestate/gameplay/gameplay.h
  - id: openwiki-source-dd6600ebcef6778c27688c55
    resource: repo://src/gamestate/gameplay/running/gameplayrunning.cpp
  - id: openwiki-source-d364d949938a433276255c32
    resource: repo://src/main.cpp
  - id: openwiki-source-7b722948afd514b26fa2b16c
    resource: repo://src/utils/simulationtickrunner.h
  - id: openwiki-source-2c1d10bd52d032cd62ce51a3
    resource: repo://tools/agent-check.ps1
generated: { by: "copilot", at: "2026-11-19T09:46:20.407Z" }
verified:
  - by: openwiki/0.7.2
    at: 2026-11-19T09:46:20.407Z
---

# Application runtime and game states

`main` owns graphical process startup and shutdown. It parses command-line
options and rejects incompatible scenario, benchmark, and remote-simulation
modes. For `-local-singleplayer`, it first chooses a loopback port, starts the
matching sibling server, and connects a `TcpClient`; failures to find/start the
server, an early server exit, or a connection timeout are reported before the
graphical setup continues. It then loads the base configuration, user controls,
voxel properties, world objects, and equipment, initializes GLFW/OpenGL/GLEW,
constructs `Game`, and enters the frame loop. Runtime data paths such as
`data/config.ini` are relative to the repository root.

With no remote-simulation option, `voxellancer` runs its standalone local
simulation. `-local-singleplayer` attaches the preconnected local server and
selects the battle scenario. `-server-preview` connects a preview client to
loopback port 27800, while `-multiplayer-client` connects to port 27801 and
starts in the large-combat scenario. These modes are mutually exclusive.
`-scenario shield-showcase` selects the shield showcase and cannot be combined
with remote simulation or benchmark options. `-benchmark-large-combat` is
supported only with local-singleplayer or multiplayer-client modes.

`-fullscreen` requests a fullscreen context. `-nointro` skips the intro;
otherwise it is shown only when `general.showIntro` is enabled and no benchmark
or remote-client mode suppresses it. Rendering is mono by default. `-stereo`
switches the `Viewer` to its built-in `StereoView` using
`StereoRenderInfo::dummy()` placeholder calibration values. This generic stereo
path does not initialize an Oculus device or SDK.

## Frame loop

The GLFW loop obtains a delta from `glfwGetTime`, passes it to `Game::update`,
draws the active scene through `Game::draw`, swaps buffers, and polls events.
`Game` forwards that delta through the active state and updates the viewer; the
world simulation uses its own fixed 30 Hz tick runner rather than relying on a
clamped render-frame delta. Resize events update both the OpenGL viewport and
the viewer viewport; input callbacks are forwarded to whichever game-state
input handler is active.

`Viewer` owns the current mono or stereo `View`. Its `draw` method establishes
back-face culling and clears depth before delegating the scene and camera to
that view. `-stereo` creates the built-in `StereoView`; its placeholder
calibration values are for the generic rendering path rather than device
tracking.

## State hierarchy

`Game` is the root `GameState` and owns a `Viewer`, `GamePlay`, and `Intro`.
When intros are enabled, the intro's completion trigger transitions to
gameplay; otherwise gameplay is selected immediately. Nested `GameState`
instances delegate scene, camera, and input requests to their parent unless
they override them, which lets the root render the currently active leaf state.

`GamePlay` owns running and paused substates, normal/free-camera and
lobby-waiting input handlers, the gameplay scene, and sound. Entering gameplay
activates sound and loads its selected initial scenario; the default is
scenario 0, the Lua-backed demo. The standalone client advances its local
`SimulationSession`. With remote preview active, the running state polls the
TCP client, samples/interpolates replicated snapshots, and predicts the
controlled actor for presentation without advancing the local world. While a
multiplayer lobby has not started, gameplay uses the waiting input handler.
Free-camera mode switches camera and input source, clears player movement, and
controls HUD visibility through `vfx.drawhud`. See
[client-server simulation](client-server-simulation.md) for authority,
replication, and lobby flow.

The VS Code runtime tasks invoke `tools/agent-check.ps1`: **Start Server** and
**Start Client** launch the normal multiplayer pair, **Start Shield Showcase
Scenario** launches the authored showcase, and the two large-combat tasks run
either one integrated local client/server or a server with two clients. The
build and validation workflow is detailed in
[build and test](../development/build-and-test.md).

## Shutdown and change boundaries

After the GLFW loop exits, `main` deletes `Game`, shuts down the context
provider, and terminates GLFW.

- Add runtime configuration or property-defined assets through the property
  loader and data files described in [content authoring](../workflows/content-authoring.md).
- Make simulation changes through the update path documented in
  [world simulation](world-simulation.md), rather than viewer rendering.
- Extend Lua scenarios through [Lua scripting](../integrations/lua-scripting.md);
  gameplay currently selects the demo script by default.
