How to play
===========

You can fly around, fight and destroy other ships or do missions.

## Controls
The game opens on a main menu and connects to an automatically started local
contracts server. Start and Resume are disabled until pilot assignment and
initial world/contract state arrive; connection information appears at the
bottom. Settings is always available. Start enters the station/mission/shop
loop; Resume is available after starting, and Start is then disabled to avoid
resetting a live session. Esc opens the menu during gameplay or returns from
Settings. The online simulation does not pause while the menu is open.

Local solo contracts save automatically when you arrive at a base (including
the starting station) and when you confirm travel out of it. You can quit
anywhere. After relaunching, choose **Start** to return to the last saved base.
A departure checkpoint keeps your purchases, repairs, loadout, and chosen
destination; confirm travel again when ready. Changes after the latest
checkpoint, including combat and salvage before the next base, are lost.
Co-op runs do not use this checkpoint.

Choose **Play Contracts with Local Server (macOS)** in VS Code to build and
launch the menu. At a base, **Enter / M** opens the branching sector map.
Click a connected node or use the arrow keys to select it, then **TRAVEL / Enter**
confirms your destination. Every surviving co-op pilot must confirm; changing the shared
destination resets all readiness. **M / Esc** returns to the base from its map.

Attack nodes contain a Raider Patrol (100 credits), Debris Crossfire (150), or
Fortified Blockade (200), with different enemies, formations, and destructible
cover. Enemy counts scale with surviving pilots. Clear the hostiles to choose
one personal reward: the listed credits, a disclosed random gun, or a run-only
upgrade. Click a card's **CLAIM** button, use **1 / 2 / 3**, or select with
**Left/Right** and press **Enter**. Upgrades add 15% gun damage or 20% shield
capacity per stack against base values, including later refits. Capacity
upgrades retain the current charge fraction. All survivors must choose before
returning to the **map**. Payload stacks also add 15% reactor regeneration
after the normal pause; shield stacks reduce gun energy cost by 10% each
(up to 40%) while shields are at least 90% charged.
Base nodes offer a shop, free shield and energy refills,
and paid hull repair (**H** or the repair button). Repair restores HP on surviving
blocks only; lost blocks and mounts stay lost. **?** nodes offer derelict salvage
or an optional fight. The map previews the signal, known opposition, and reward.
Follow the links toward the final attack; visited nodes cannot be
replayed. In Fortified Blockade and the Iron Regent finale, **P** marks the
shield projector and **o** marks a gun mount. Weaken the shield, then aim
manually at a module: destroying a gun mount removes its weapon, and destroying
the projector also shuts off the shield. Clearing the finale and choosing salvage opens the victory
debrief. Solo death or a party wipe opens the defeat debrief. **NEW RUN / Enter**
resets ships, credits, inventory, equipment, upgrades, statistics, and the map
without reconnecting; every connected co-op pilot must confirm. Settings persist.
There are **no revives**: dead pilots spectate for the rest of the run, including
base visits. They receive no rewards and cannot shop or confirm travel, but
can join everyone in the next fresh run.

At bases, **B** buys the current personal random gun offer into inventory, and
**N** opens shield builds. Use **1/2/3**, **Left/Right**, or click to select
Balanced Mk2, Bulwark, or Flux, then **Enter / Buy and Equip** to purchase.
Balanced costs 100 credits; Bulwark and Flux cost 150. The old shield is replaced
without a refund; **N** or **Esc** returns to the base. Only one gun may be
bought per base visit (100–200 credits); reaching another base
refreshes the offer. Fighting alone does not reopen a sold offer.
**E** opens equipment: click a numbered mount on the hollow ship, select a stored
weapon, then **FIT WEAPON / Enter**. The replaced weapon goes back to inventory.
**Left/Right** selects mounts, **Up/Down** selects inventory, and **Q/R** rotates
the preview. **Esc** or **E** returns to station services. Weapon cards compare
velocity, range, shield, armor, and normal-block damage; splash/volley values are
totals, including your run's damage bonus. Cards also show energy per shot.
Inventory and fitted weapons last only for this run.
**Tab** toggles the station/objective panel; **HIDE** closes station services.
Hiding it at
the base does not launch: press Enter to open the map. **O** restores the
station/objective panel. See the [contract guide](../README.md#station-contracts-first-progression-slice)
for local co-op startup and current limitations.

The flight HUD shows **shield HP** on the left and **weapon energy** on the
right, including recharge countdowns. Contract guns share 100 energy, recovering
35/s after 0.4 s without a successful shot. An unaffordable gun does not fire;
release fire to refill. Scatter spends once for the whole volley. Shields
recharge independently after their build's hit delay, including after depletion.
Bulwark favors capacity, Flux favors recovery, and Balanced sits between them.
Bases refill both reserves for free; hull repair is a separate purchase. These meters follow
the HUD visibility setting and are hidden for dead spectators. Prototype
invulnerability must be turned off in Settings to take shield or hull damage.

Contract enemies have orange-red brackets and an **ENEMY** label. Their
distance is shown in world units (**U**). Look toward the screen-edge indicator
for an off-screen enemy; **ENEMY BEHIND** means you need to turn around. Your
co-op partners are never marked as enemies. Turning Flight HUD off also hides
these markers. Enemy marking identifies a contact; it does not auto-lock guns.

* W,A,S,D - direction
* Left Mouse - fire toward the crosshair (within the guns' aiming limits);
  mouse aiming is independent of ship steering in solo and co-op.
  Line up the lead marker before firing and each gun will lead the target
  independently. Standalone scenarios show the marker for your selected
  target; networked contracts show a cyan `+` for the enemy lead point
  nearest the crosshair, changing to `(+)` when lined up. The first run's
  opening attack also shows a lead-alignment prompt and a burst-fire reminder.
* Right Mouse - click to select target / hold to change direction
* M - route map/back at a base; free mouse cursor during flight
* Space - change to fight controls
* Q, E - roll
* R - Rocket
* Esc - equipment/map back at a base; otherwise main menu / resume
* Tab - show/hide the contract panel and switch between cursor and flight control
* P - Pause (developer scenarios only)
* F1-F7 change scenarios (developer modes only); F7 loads the shield showcase

The station panel opens with a usable cursor. Tab also frees the cursor when
opening the contract panel in flight. For other developer overlays, press M
to free the cursor. Drag the panel header to move it, drag its lower-right
corner to resize it, and use - to minimize or restore it. X closes the panel
and releases flight control. Drag/resize/minimize controls apply to the legacy
mission/developer overlays, not the fixed station/equipment screens.

Player invulnerability is **On** by default for testing. Open **Esc > Settings >
Invulnerability** to toggle it, including during a mission. It prevents shield
and hull damage to your own pilot on the authoritative server, not enemies or
other pilots. The preference is saved across launches (on macOS, in
`~/.voxellancer/settings.ini`). Disabling it restores normal combat damage;
it does not repair previously damaged hull.

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
	  -local-singleplayer  start a local authoritative contracts server (or benchmark)
	  -scenario contracts  open the contracts main menu (also the default)
	  -benchmark-large-combat  benchmark the server-side 800-ship battle
	  -benchmark-run-id <id>   group server and client reports
	  -benchmark-client-id <id> identify a graphical client report

## Server and benchmark tasks

The Windows runtime tasks are:

1. **Start Server** — start a two-pilot contracts lobby on port 27801.
2. **Start Client** — start a graphical multiplayer client.
3. **Start Local Client with Integrated Server (Large Combat Benchmark)** —
   start a local authoritative large-combat server and its graphical client.
4. **Start Server and Two Clients (Large Combat Benchmark)** — start a lobby
   for two visible clients and run the same large-combat benchmark.
5. **Start Shield Showcase Scenario** — launch the shield demonstration.

On macOS, **Start Client with Local Server (macOS)** and **Play Contracts with
Local Server (macOS)** both build the client/server and open the contracts
main menu.

Settings offers VSync, antialiasing (SSAA / FXAA / Off), and the flight HUD.
Preferences are saved in `settings.ini` in the user configuration directory.
The generated Voxellancer Flight font replaces voxel letters throughout the UI.

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
