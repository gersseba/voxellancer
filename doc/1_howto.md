How to play
===========

You can fly around, fight and destroy other ships or do missions.

## Controls
* W,A,S,D - direction
* Left Mouse - fire
* Right Mouse - click to select target / hold to change direction
* Space - change to fight controls
* Q, E - roll
* R - Rocket
* P - Pause
* F1-F5 change scenarios

You can configure the controls as you want. The instructions are currently only displayed in the console.

* F10 start gamepad/joystick configuration
* F11 start keyboard configuration

only for debugging:
* T - show active leafs of worldtree
* F8 - reload shader and configs
* F9 - world status information
* 1-9 - show framebuffer
* F - toggle flycam

## Commandline parameters

	Usage: voxellancer [-fullscreen] [-stereo] [-server-preview | -local-singleplayer | -multiplayer-client] [-benchmark-large-combat]

	  -fullscreen  fullscreen mode
	  -stereo      stereo rendering
	  -nointro     skip the intro
	  -server-preview  connect to a local authoritative server
	  -multiplayer-client  connect to the server on 127.0.0.1:27801
	  -local-singleplayer  start a local authoritative battle server
	  -benchmark-large-combat  benchmark the server-side 800-ship battle
	  -benchmark-run-id <id>   group server and client reports
	  -benchmark-client-id <id> identify a graphical client report

## Server and benchmark tasks

The VS Code task list contains exactly four tasks:

1. **Start Server** — start a battle server on port 27801.
2. **Start Client** — start a graphical multiplayer client.
3. **Start Local Client with Integrated Server (Large Combat Benchmark)** —
   start a local authoritative large-combat server and its graphical client.
4. **Start Server and Two Clients (Large Combat Benchmark)** — start a lobby
   for two visible clients and run the same large-combat benchmark.

The seeded large-combat workload contains 400 AI ships per team. The server
advances exactly 1,800 intervals after all target clients connect. Client frame
recording begins on the first started-world snapshot, so its observed tick span
may be slightly shorter. Reports in
`benchmark-results\` use a shared run ID and explicit roles: one server report,
one `client-1` report, and one `client-2` report for multiplayer runs. Server
profiles include simulation, network polling, snapshot building, interest
filtering, delta construction, encoding, packet queue timings, and per-client
traffic. Client profiles include frame phases, network polling, snapshot
sampling, input prediction, replica updates, and GPU timings when supported.
Reports retain aligned per-interval/frame timings and p50/p95/p99/max summaries.
Nested timings overlap; server tick and client frame durations measure different
work and should not be summed or compared as equivalent.
