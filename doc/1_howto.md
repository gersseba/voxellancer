How to play
===========

You can fly around, fight and destroy other ships or do missions.

## Controls
The game opens on a main menu and connects to an automatically started local
contracts server. **Contracts** enters the station/mission/shop loop once pilot
assignment and initial world/contract state arrive. **Scenarios** offers the
legacy and test scenarios, including the weapon range and AI comparison,
without requiring a server. Selecting a scenario starts a fresh instance;
**Resume** returns to the current mode, **Settings** changes preferences, and
**Exit** closes the game. Use arrows or Tab and Enter, or the mouse. Esc opens
the menu during gameplay, returns from a submenu, or resumes the current mode.
Local scenarios pause in the menu. The online simulation continues while the
menu or a local scenario is open; your remote flight and firing are neutralized.
Connection information appears at the bottom, and a disconnected server does
not interrupt a local scenario.

Local solo contracts save automatically when you arrive at a base (including
the starting station) and when you confirm travel out of it. You can quit
anywhere. After relaunching, choose **Contracts** to return to the last saved base.
A departure checkpoint keeps your purchases, repairs, loadout, and chosen
destination; confirm travel again when ready. Changes after the latest
checkpoint, including combat and salvage before the next base, are lost.
Co-op runs do not use this checkpoint.

Choose **start game** in VS Code to build and
launch the menu. At a base, **Enter / H** opens the branching sector map.
Click a connected node or use the arrow keys to select it, then **TRAVEL / Enter**
confirms your destination. Every surviving co-op pilot must confirm; changing the shared
destination resets all readiness. **Esc** returns to the base from its map.

Attack nodes contain a Raider Patrol (100 credits), Debris Crossfire (150), or
Fortified Blockade (200), with different enemies, formations, and destructible
cover. Each sortie announces its name and objective across the HUD for a few
seconds. Each sortie randomly picks one of the cover layouts suited to the
level (open space, an asteroid field, an asteroid belt, a debris cluster, or a
huge derelict hulk), and its asteroids and wreckage are generated fresh; use
them to break line of fire. Enemies start out of range and hold position until you
close to about 900 units (1.5× the Pulse Repeater's range) or hit one, then the
whole squad engages. Enemy counts scale with surviving pilots. Clear the hostiles to choose
one personal reward: the listed credits, a disclosed random gun, or a run-only
upgrade. Click a card's **CLAIM** button, use **1 / 2 / 3**, or select with
**Left/Right** and press **Enter**. Upgrades add 15% gun damage or 20% shield
capacity per stack against base values, including later refits. Capacity
upgrades retain the current charge fraction. All survivors must choose before
returning to the **map**. Payload stacks also add 15% reactor regeneration
after the normal pause; shield stacks reduce gun energy cost by 10% each
(up to 40%) while shields are at least 90% charged.
Base nodes offer a shop, free shield and energy refills,
and paid hull repair (the repair button). Repair restores HP on surviving
blocks only; lost blocks and mounts stay lost. **?** nodes offer derelict salvage
or an optional fight. The map previews the signal, known opposition, and reward.
In **Distress Call**, a friendly **WINGMAN** fights alongside you. Clear the
raiders to finish; if the wingman survives, the credit choice pays **200 CR**
instead of **125 CR**. Losing the wingman forfeits only that bonus. The wingman
leaves after the fight and never replaces or revives a player.
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

At bases, the overview highlights sections on hover: **A/E** opens Armory,
**M** opens Market, **B** opens Bar, and **H** opens Hangar. Gambling in the
bar is not implemented. In the market, click **BUY** or press **Enter** to
store the current personal random gun offer. Select a stored weapon and click
**SELL** or press **S** to sell one for half its catalog price, rounded down.
Fitted weapons cannot be sold. **N** opens shield builds. Use **1/2/3**, **Left/Right**, or click to select
Balanced Mk2, Bulwark, or Flux, then **Enter / Buy and Equip** to purchase.
Balanced costs 100 credits; Bulwark and Flux cost 150. The old shield is replaced
without a refund; **N** or **Esc** returns to the base. Only one gun may be
bought per base visit (100–200 credits); reaching another base
refreshes the offer. Fighting alone does not reopen a sold offer.
In the armory, click a numbered mount on the hollow ship, select a stored
weapon, then **FIT WEAPON / Enter**. The replaced weapon goes back to inventory.
**Left/Right** selects mounts, **Up/Down** selects inventory, and **Q/R** rotates
the preview. **Esc** returns to station services. Inventory lists only stored
weapons. Weapon cards compare
velocity, range, shield, armor, and normal-block damage; splash/volley values are
totals, including your run's damage bonus. Cards also show energy per shot.
Inventory and fitted weapons last only for this run.
**Tab** toggles the station/objective panel; **HIDE** closes station services.
Hiding it at
the base does not launch: press Enter to open the map. **O** restores the
station/objective panel. See the [contract guide](../README.md#station-contracts-first-progression-slice)
for local co-op startup and current limitations.

The starter ship has five weapon mounts, two fitted with Pulse I guns. The
reactor recharges while you fire, so both guns can fire nonstop. An identical
starter shield breaks after about five seconds of that fire when all shots hit.
The flight HUD shows **shield HP** on the left and **weapon energy** on the
right, including shield recharge countdowns. Contract guns share 100 energy,
recovering 40/s at all times. Stronger guns and extra mounts can spend faster
than that; an unaffordable gun does not fire until the reserve recovers.
Scatter spends once for the whole volley. Shields
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
  Empty-space fire converges at the shortest equipped gun range; a nearer
  voxel under the crosshair takes priority, excluding your own ship. Gun
  limits, obstacles, projectile lifetime, and movement may prevent a hit.
  Otherwise, line up the lead marker before firing and each gun will lead the target
  independently. Standalone scenarios show the marker for your selected
  target; networked contracts show a cyan `+` for the enemy lead point
  nearest the crosshair, changing to `(+)` when lined up. The cue aims near the
  center of mass at a surviving hull voxel; sudden movement or rotation can
  still make a shot miss. The first run's
  opening attack also shows a lead-alignment prompt and an energy reminder.
* Right Mouse - click to select target / hold to change direction
* M - market at a base; free mouse cursor during flight
* Space - change to fight controls
* Q, E - roll
* R - Rocket
* Esc - equipment/map back at a base; otherwise main menu / resume
* Tab - show/hide the contract panel and switch between cursor and flight control
* P - Pause/resume a local scenario via the main menu (standalone shortcuts use the legacy pause)
* F1-F7 change local scenarios; F7 loads the shield showcase

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

## Server and benchmark commands

The VS Code **start game** task builds the client and local server and opens
the main menu. Multiplayer and benchmark commands remain available through the
[build and run harness](3_buildinstructions.md#build-test-and-run-harness).

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
