---
type: architecture
title: Client-server simulation and replication
description: Explains how Voxellancer runs an authoritative simulation server, connects the graphical client over TCP, and presents replicated state with interpolation and bounded prediction.
tags: [architecture, networking, simulation, client, server]
verified:
  - by: openwiki/0.7.2
    at: 2026-11-19T09:46:20.407Z
sources:
  - id: openwiki-source-d44494ef3e497fea81240ef8
    resource: repo://CMakeLists.txt
  - id: openwiki-source-8926abec0d4cf04126788739
    resource: repo://src/client/inputpredictor.cpp
  - id: openwiki-source-18f8ead36ee6202ab5e3f24e
    resource: repo://src/client/localserverprocess.cpp
  - id: openwiki-source-b82ef5c901a3fbc4495245b4
    resource: repo://src/client/main.cpp
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
  - id: openwiki-source-3742844d97ced7569ebb10f5
    resource: repo://test/server/testtcpserver.cpp
generated: { by: "copilot", at: "2026-11-19T09:46:20.407Z" }
---

# Client-server simulation and replication

Voxellancer separates authoritative gameplay state from graphical presentation.
`voxellancer-server` owns the simulation and accepts client input; `voxellancer`
renders the game, samples player input, and displays the server's replicated
world. The separately built `voxellancer-client` is a headless transport and
integration probe, not the graphical game client. The detailed world update
order is covered in [world simulation](world-simulation.md), while this page
follows data between the processes.

## Runtime boundary

The standalone server creates a `World::Mode::Server`, loads shared game data
and the selected scenario, and owns a `SimulationSession` and `TcpServer`.
During normal operation, simulation advancement and network polling run
sequentially on the same server thread.

Local singleplayer is launched through the normal graphical executable with
`-local-singleplayer`. Before initializing GLFW, it chooses a loopback port,
starts the matching sibling `voxellancer-server` with the battle scenario, and
connects a `TcpClient` to that port. Startup fails explicitly if the server
cannot be found or started, exits while the client is connecting, or does not
accept the connection before the timeout. The user launches one executable,
but the client and server are separate processes. The graphical process remains
responsible for rendering and input, and stops its child server when it exits.

With `--target-players`, the server accepts clients into a lobby before loading
the scenario or advancing simulation ticks. It waits for the configured number
of clients, then loads the selected scenario, assigns one ship actor to each
connection, sends actor assignments and initial snapshots, and broadcasts that
the world has started. The player target is bounded by the server's 16-client
limit. Without a target-player lobby, the server follows its immediate scenario
startup path.

## Authoritative simulation

`SimulationTickRunner` advances at a fixed 30 Hz. A single advance runs at most
five catch-up ticks; any remaining elapsed time stays in the accumulator for a
later advance. The server queues bounded, sequenced input commands and applies
them at simulation tick boundaries before updating the world. Collision,
damage, weapon effects, missions, object lifecycle, and resulting transforms
are server-owned; clients send input intent, not authoritative transforms or
damage outcomes.

The graphical client schedules remote input two simulation ticks ahead. The
server validates commands against the assigned ship and its tick/sequence
constraints, applies accepted state at a tick boundary, and acknowledges each
result. Input is therefore ordered against simulation ticks rather than
render-frame timing.

## TCP protocol and replication flow

The current transport uses SFML TCP sockets. The server binds to the local host,
and allows up to 16 clients. Each TCP frame has a four-byte little-endian
payload length followed by a protocol-version byte and message-type byte. The
current protocol version is 18; a version mismatch is rejected rather than
negotiated. TCP supplies reliable, ordered delivery for all messages; there is
no separate UDP snapshot channel.

At 30 simulation ticks per second, the server sends snapshots every two ticks
(15 Hz). Each client receives entities within its interest radius, plus its
assigned actor; the normal battle radius is 650 world units, while the
large-combat server path uses 1,500. Snapshots are per-client baseline deltas.
When an entity first becomes visible, the server also sends its definition key,
voxel state, and equipment state. Subsequent equipment changes and authoritative
voxel damage/removal, shield-hit, weapon-fired, and mission-state events use
separate typed messages. Snapshot state includes transforms, velocities,
shield/hull fractions, scale, maximum mass, and engine state.

If a client cannot apply a snapshot delta, it requests a resynchronization. The
server clears that connection's snapshot and equipment baselines and sends a
full snapshot. While resynchronization is pending, the client discards queued
state-dependent messages from the old baseline and waits for a new full
baseline before resuming replica updates. The server also limits how frequently
a connection can request another resynchronization.

## Client presentation and prediction

The client interpolates position, orientation, velocity, and other replicated
fields between snapshots. When rendering beyond the newest snapshot, it
extrapolates remote position and orientation for at most 50 milliseconds. The
controlled ship is predicted from the latest authoritative state, replicated
engine and mass data, and pending input commands, with a four-tick prediction
limit. Each later authoritative snapshot becomes the new reconciliation point.
This prediction is presentation support, not a second authority: it does not
simulate collisions or decide combat outcomes.

## Operational limits and tests

The server listener currently binds only to the local host. The path has no
authentication or encryption layer and is intended for local development, not
internet hosting. The simulation and socket polling also share one thread, so
network-load and hosting-capacity behavior must be measured before changing that
ownership model.

Protocol encoding and validation are covered in `test/protocol`; server/client
flows, lobby startup, actor assignment, snapshots, resynchronization, and event
replication are covered in `test/server`; interpolation and prediction have
focused tests in `test/client`, and fixed-step/session behavior is covered in
`test/simulation`. See [application runtime](runtime.md) for graphical startup
and [build and test](../development/build-and-test.md) for validation commands.
