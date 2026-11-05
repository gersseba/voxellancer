---
type: architecture
title: Client-server simulation and replication
description: Explains how Voxellancer runs an authoritative simulation server, connects the graphical client over TCP, and presents replicated state with interpolation and bounded prediction.
tags: [architecture, networking, simulation, client, server]
verified:
  - by: openwiki/0.7.2
    at: 2026-11-05T22:43:09.174Z
sources:
  - id: openwiki-source-8926abec0d4cf04126788739
    resource: repo://src/client/inputpredictor.cpp
  - id: openwiki-source-18f8ead36ee6202ab5e3f24e
    resource: repo://src/client/localserverprocess.cpp
  - id: openwiki-source-2ce52da9cc930020be899541
    resource: repo://src/client/snapshotinterpolator.cpp
  - id: openwiki-source-cf58f241ea68f7f08c05fa0f
    resource: repo://src/client/tcpclient.cpp
  - id: openwiki-source-dd6600ebcef6778c27688c55
    resource: repo://src/gamestate/gameplay/running/gameplayrunning.cpp
  - id: openwiki-source-d364d949938a433276255c32
    resource: repo://src/main.cpp
  - id: openwiki-source-36328e859be5e981e9b4d833
    resource: repo://src/protocol/serverprotocol.h
  - id: openwiki-source-f3b131cfec992caceffcc7b6
    resource: repo://src/server/main.cpp
  - id: openwiki-source-bd9ee3e5f4c4b40eba3d2469
    resource: repo://src/server/tcpserver.cpp
  - id: openwiki-source-1d45275644c29e0703c39d51
    resource: repo://src/server/tcpserver.h
  - id: openwiki-source-5755e4066d8d2af940b39780
    resource: repo://src/simulation/simulationsession.cpp
  - id: openwiki-source-7b722948afd514b26fa2b16c
    resource: repo://src/utils/simulationtickrunner.h
generated: { by: "copilot", at: "2026-11-05T22:43:09.174Z" }
---

# Client-server simulation and replication

Voxellancer separates authoritative gameplay state from graphical presentation.
`voxellancer-server` owns the simulation and accepts client input; `voxellancer`
renders the game, samples player input, and displays the server's replicated
world. The detailed world update order is covered in
[world simulation](world-simulation.md), while this page follows data between
the processes.

## Runtime boundary

The standalone server creates a headless `World::Mode::Server`, loads the
requested scenario and content, then advances a `SimulationSession` while
polling its TCP listener. Simulation advancement and network polling currently
run on the same server thread.

Local singleplayer is launched through the normal graphical executable with
`-local-singleplayer`. Before initializing GLFW, it chooses a local port, starts
the matching sibling `voxellancer-server` with `--listen --listen-port <port>
--scenario battle`, and connects a client to that port. It is one user-facing
executable but two processes internally. The server assigns the battle
scenario's player ship to the client; the graphical process remains responsible
for rendering and input.

## Authoritative simulation

`SimulationTickRunner` advances at a fixed 60 Hz. A single advance runs at most
five catch-up ticks; any remaining elapsed time stays in the accumulator for a
later advance. The server applies tick-stamped input commands at tick boundaries
and then updates its world. Collision, damage, weapon effects, missions, object
lifecycle, and resulting transforms are server-owned; clients send input intent,
not authoritative transforms or damage outcomes.

The graphical client schedules remote input four simulation ticks ahead. The
server validates command sequencing and timing, applies the latest command for
its actor, and acknowledges the result. Input is therefore ordered against
simulation ticks rather than render-frame timing.

## TCP protocol and replication flow

The current transport uses SFML TCP sockets. The server binds to the local host,
and the normal standalone server allows up to 16 clients. Each TCP frame has a
four-byte little-endian payload length followed by a protocol-version byte and
message-type byte. The current protocol version is 16; a version mismatch is
rejected rather than negotiated. TCP supplies reliable, ordered delivery for all
messages; there is no separate UDP snapshot channel.

At 60 simulation ticks per second, the server sends snapshots every three ticks
(20 Hz). Each client receives entities within a 650-unit interest radius, plus
its assigned actor. Snapshots are baseline deltas. When an entity first becomes
visible, the server also sends its voxel and equipment content; later equipment
changes and combat, voxel, shield, and mission events travel as separate typed
messages.

If a client cannot apply a snapshot, it can request a resynchronization. The
server clears that connection's snapshot and equipment baselines and sends a
fresh baseline. While waiting for it, the client discards queued deltas and
state-dependent events from the old baseline, then resumes processing after
the new baseline is applied.

## Client presentation and prediction

The client interpolates position, orientation, velocity, and other replicated
fields between snapshots. When rendering beyond the newest snapshot, it
extrapolates remote motion for at most three ticks. The controlled ship is
predicted from the latest authoritative state, replicated engine and mass data,
and pending input commands, with an eight-tick prediction limit. Each later
snapshot becomes the new reconciliation point. This prediction is presentation
support, not a second authority: it does not simulate collisions or decide
combat outcomes.

## Operational limits and tests

The server listener currently binds only to the local host. The path has no
authentication or encryption layer and is intended for local development, not
internet hosting. The simulation and socket polling also share one thread, so
network-load and hosting-capacity behavior must be measured before changing that
ownership model.

Protocol encoding and validation are covered in `test/protocol`; server/client
flows, actor assignment, snapshots, resynchronization, and event replication
are covered in `test/server`; interpolation and prediction have focused tests
in `test/client`. See [application runtime](runtime.md) for graphical startup
and [build and test](../development/build-and-test.md) for validation commands.
