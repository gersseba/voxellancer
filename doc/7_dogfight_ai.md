# Dogfight AI: independent flight and shooting skills

## Ownership and behavior

[FightTask](../src/ai/basictasks/fighttask.cpp) updates a flight behavior, then
one shared [ShootingController](../src/ai/shootingcontroller.cpp). Choosing
`fighter`, `cruiser`, or `interceptor` does not choose aiming skill.

- **Fighter:** original approach/engage pursuit and random close-range escape
  waypoint; sign-based turning and no gun firing during escape.
- **Cruiser:** original pursuit, rocket-range target preference, and
  multi-target hardpoint selection.
- **Interceptor:** relative-velocity range control, approximately 55% of the
  shortest usable gun range, lateral strafing, stopping-distance separation
  with hysteresis, and angular-rate-damped turning. It can fire while separating.

Interceptor steering uses the target's current position, not the configurable
crosshair. Lead, tracking, and jitter do not feed back into flight commands.
Contract fighters use the interceptor; their thrust and turning inputs are
bounded like player inputs and still use their installed engines and mass.
Existing target hysteresis and squad sharing remain behavior-owned; gun target
acquisition no longer has the old additional staggered delay.
Shooting has its own random generator, independent of random escape maneuvers.
The common controller also owns reactor burst recovery: pause below the most
expensive affordable gun's cost and resume at that cost or 60% capacity,
whichever is higher. Unaffordable mounts cannot starve usable ones.

Gun cooldown, mount cones, own-hull/friendly-fire checks, projectile velocity,
range, recoil, shields, and voxel damage remain the normal simulation rules.
Behavior decides whether to fire and retains its existing homing-rocket policy;
the three skills control **guns**, not rocket guidance. The arena uses only guns.

Everything runs in the existing authoritative AI update order; there is no new
threading or client-authoritative damage. Subsequent flight trajectories can
still differ because hits destroy engines or because physical weapon recoil
changes velocity. That is a physical consequence, not aim controlling steering.
This is not general obstacle avoidance or team tactical coordination.

## Three continuous skills

All values must be finite and in **[0,1]**. Invalid configuration throws;
the Lua setter logs a warning, returns -1, and preserves previous values.

| Skill | 0 | Intermediate | 1 |
|---|---|---|---|
| Lead | Current target position | Linear blend toward intercept point | Perfect constant-velocity ballistic intercept |
| Tracking | Crosshair slew limited to 5 degrees/s | `5 + 355 * tracking^2` degrees/s | Instant acquisition/following at each fire-control update |
| Accuracy | Strong, centered Gaussian jitter; near 10% reference hits | Continuously reduced jitter | No AI jitter |

Lead reuses [HardpointAimHelper](../src/worldobject/helper/hardpointaimhelper.cpp),
including inherited shooter velocity for profiled bullets and hard
range/lifetime limits. Perfect lead refuses shots without a valid intercept.
Lower lead skills can fire at current position when no intercept exists.
Acceleration after launch is not known: "perfect" lead is not a guaranteed hit
against a maneuvering target.

Each hardpoint retains a world-space crosshair direction. A newly equipped gun
starts on its mounting direction; changing targets retains the previous
direction rather than teleporting it. Tracking is updated even while cooldown,
reactor recovery, or the fighter's escape policy suppresses firing.
Accuracy noise is applied separately to shot direction, not crosshair tracking.

Jitter uses two independent, symmetric Gaussian offsets in the aim plane.
An active spherical shield supplies its real radius. For an unshielded voxel
hull, unique occupied-cell projections along the three local axes estimate
silhouette area. Their view-direction-weighted blend gives a continuous,
orientation-dependent area estimate; the equivalent circular radius is
`sqrt(area / pi) * scale * 0.88`, including an empirically calibrated correction
for non-circular voxel silhouettes. Cached projections refresh on target changes or
voxel removal. This avoids treating a thin ship's mostly-empty bounding sphere
as solid armor.

For equivalent radius `r`, noise deviation is:

```text
p = 0.1 + 0.9 * accuracy
sigma = r / sqrt(-2 * ln(1 - p))
```

Accuracy 1 takes the explicit zero-noise branch. The formula gives probability
`p` inside an equivalent circular silhouette; it is **not** a forced-hit
probability for every irregular hull. The calibration tests shoot actual
Pulse I projectiles at stationary, invulnerable `contractraider` voxel hulls
and `contractship` spherical shields from 300 U, with lead/tracking both 1.
They sample 2,000 shots per zero-accuracy target and require a rate within
2 percentage points of 10%, intermediate
accuracy substantially higher, and perfect accuracy to hit every shot.
Moving targets, damaged silhouettes, launch distance, shields, mount rejection,
and authored weapon spread can change combat hit rates. No dice roll invents a
hit or suppresses collision damage.

### Configuring a pilot

Normal NPCs use `[aiShooting] lead`, `tracking`, and `accuracy` in
[config.ini](../data/config.ini), currently **0.5 each**. This intentionally
replaces the old fighter's biased error and interceptor's automatic perfect
shooting with one common, tunable skill model. Default flight archetype mapping
is still unchanged.

In C++:

```cpp
FightTask task(computer, targets, AiArchetype::Interceptor, {0.7f,0.4f,0.6f});
task.shooting().setSkill({0.8f,0.5f,0.7f});
task.shooting().seed(8101);
```

In Lua:

```lua
local task = createFightTaskWithArchetype(ship, "interceptor")
setFightTaskShootingSkill(task, 0.7, 0.4, 0.6)
addFightTaskTarget(task, opponent)
```

`createFightTask(ship)` still selects fighter/cruiser from `ship_type`.

## Sixteen controlled duels

[AiComparisonScenario](../src/scenarios/aicomparisonscenario.cpp) creates
**32 combatants** with one designated opponent each. All use the same full,
unshielded `contractraider` hull, engines, three Pulse I guns, unlimited NPC
energy, and no upgrades or player protection.

Every adjacent pair of duels mirrors starting positions, orientations, and
velocities while keeping A/B roles fixed:

| Pairs | Variable changed | A | B | Other controls |
|---|---|---|---|---|
| 1-2 | Flight, head-on | Fighter | Interceptor | Lead/tracking/accuracy all 1 |
| 3-4 | Flight, crossing | Fighter | Interceptor | Lead/tracking/accuracy all 1 |
| 5-6 | Lead | 0 | 1 | Both interceptor, tracking/accuracy 1 |
| 7-8 | Lead | 0.5 | 1 | Both interceptor, tracking/accuracy 1 |
| 9-10 | Tracking | 0 | 1 | Both interceptor, lead/accuracy 1 |
| 11-12 | Tracking | 0.5 | 1 | Both interceptor, lead/accuracy 1 |
| 13-14 | Accuracy | 0 | 1 | Both interceptor, lead/tracking 1 |
| 15-16 | Accuracy | 0.5 | 1 | Both interceptor, lead/tracking 1 |

All start 500 U apart. Crossing starts face diagonally with opposite 25 U/s
lateral velocities. Arenas are 50,000 U apart and target lists never cross
pairs. Every contestant uses the same AI update phase, with matching flight
and shooting seeds per geometry. A/B roles also define factions and scoring.
These are repeatable inputs, not guaranteed bitwise determinism across
platforms, floating-point positions, or unordered world traversal.

Outcomes update after each 30 Hz simulation tick. The first loss starts a
two-second grace period for already-fired Pulse return fire; mutual destruction
is separate. No loss at **120 seconds (3,600 ticks)** means timeout, not a win
based on remaining hull. Pending first-loss grace may finish after the cutoff.
Survivors stop and results, hull fractions, and shot counters freeze on
resolution. Hull is surviving voxel HP divided by initial HP, including removed
voxels as damage.

### Telemetry

A **shot** is a gun trigger/volley actually accepted by the normal gun checks,
not a targeting attempt. A **hit** is that volley physically contacting its
designated opponent's hull or shield at least once. Scatter pellets and
penetrating/multiple contacts cannot double-count a volley. In-flight records
retain statistics after the shooter dies. Hits measure contact, not necessarily
HP loss; invulnerable calibration targets still register contacts.

Both graphical and headless modes print:

- `START`: fixture, pair count, skill levels, and counter definitions.
- Per-pair lines: experiment, mirror/geometry, result, both behaviors and all
  three skills, hull, actual shots/hits/hit rates, and resolution tick.
- `GROUP`: separate outcomes and counters for flight, lead, tracking, accuracy.
- `FINAL`: overall A/B wins, mutual losses, timeouts, and elapsed ticks.

A rate is `NA` when no shots were fired. A is the lower skill in skill tests,
but the fighter in flight tests, so overall A/B wins are **not** an aggregate
fighter-versus-interceptor result. Inspect each experiment separately.

### Watching

From the repository root:

```sh
./build/macos/voxellancer -scenario ai-comparison -nointro
```

Or choose **Scenarios > AI Dogfight Comparison** from the main menu.
The scenario runs locally and adds one invulnerable, unarmed observer.

- **1-9 / 0:** jump to pairs 1-10.
- **Comma / period:** previous/next pair, wrapping through all 16.
- Selecting a pair returns from free camera.
- **F:** free camera; normal flight controls move the observer.
- **Esc:** open the menu (or exit when launched directly).

The left scoreboard shows experiment, A/B hull, outcome, and selected row.
The right panel shows both behaviors, lead/tracking/accuracy percentages, and
actual shots/hits. Named A/B contact markers include distance and
off-screen/behind cues. Normal pilot HUD is suppressed for the observer.
Flying the observer into combat can contaminate results; use headless mode for
measurement.

### Headless measurement

```sh
./build/macos/voxellancer-server --scenario ai-comparison
```

No clients, socket, graphics context, or real-time waiting are needed. It runs
the same controlled arena and exits after all 16 results. Redirect stdout to
retain the report. Networking, squad, and team-size options do not change this
fixed fixture.

Sixteen duels on one loadout remain an exploratory smoke evaluation, not a
conclusive balance study. Skill tests isolate configuration inputs, but damage
and maneuvering then interact through real physics. Repeat across seeds,
weapons, reactors, shields, obstacles, and larger ships before replacing the
default flight behavior.

The **old** ten-pair mixed-controller experiment reported 10 interceptor wins
by tick 2,366. It changed flight and shooting together and is superseded;
that score is not evidence from these equal-skill flight comparisons.

### Scenario tuning baseline

The current reference fixture, shooting seed 8101, measured:

| Target | Accuracy 0 (2,000 shots) | Accuracy 0.5 (1,000 shots) | Accuracy 1 (200 shots) |
|---|---|---|---|
| Unshielded raider | 8.7% | 40.6% | 100% |
| Spherical shield | 9.45% | 54.3% | 100% |

The recorded headless run resolved all 16 duels by tick 3,600: **13 wins,
3 timeouts, and no mutual losses**. The table pools hits and fired volleys
across the two mirrored starts in each row. Outcomes refer to the A/B
configurations in the [duel matrix](#sixteen-controlled-duels).

| Duels | Comparison (A vs. B) | A hits / volleys | B hits / volleys | Outcome |
|---|---|---:|---:|---|
| 1-2 | Fighter vs. Interceptor, head-on | 11 / 20 | 9 / 21 | Interceptor won both |
| 3-4 | Fighter vs. Interceptor, crossing | 22 / 182 | 18 / 1,011 | Interceptor won one; one timeout |
| 5-6 | Lead 0 vs. 1 | 0 / 141 | 33 / 373 | Full lead won both |
| 7-8 | Lead 0.5 vs. 1 | 8 / 340 | 30 / 652 | Full lead won both |
| 9-10 | Tracking 0 vs. 1 | 0 / 36 | 28 / 52 | Instant tracking won both |
| 11-12 | Tracking 0.5 vs. 1 | 18 / 618 | 18 / 616 | Both timed out with nearly identical hull within each duel |
| 13-14 | Accuracy 0 vs. 1 | 2 / 34 | 27 / 52 | No-jitter aim won both |
| 15-16 | Accuracy 0.5 vs. 1 | 22 / 59 | 24 / 58 | One win each |

Use these numbers as a reference for **unshielded Pulse I raiders**, not as
target win rates or hit rates for other loadouts. Zero lead and zero tracking
made the pilots ineffective in these crossing starts. Midpoint tracking kept
up about as well as instant tracking here, so `0.5` should not be read as
"half as effective." The Interceptor won more flight duels despite landing
fewer hits; judge outcomes and hull damage alongside contact rates.

The AI computes lead per gun using its muzzle position, projectile speed,
lifetime, and range. A slow plasma mortar generally needs more lead than a
fast railgun against the same crossing target. The player's HUD shows one
gun's lead as a cue, aimed at a surviving voxel near the target's center of
mass rather than empty space. Firing on the marker requests
[a separate intercept for each gun](../src/worldobject/worldobjectcomponents.cpp),
computed by the server in networked play. Guns without a valid shot do not
fire; manual aiming away from the marker still uses one crosshair point.
This baseline does not cover mixed-speed loadouts or the player aiming
experience with them. Lead assumes constant velocity and the current hull
orientation; subsequent acceleration or rotation can still cause a miss.

### Suggested pilot presets

These are **starting values for scenario playtests**, not tested combinations
or new global defaults. Keep the ship, loadout, and flight behavior the same
when first tuning these tiers; choose the flight archetype separately for the
encounter you want.

| Pilot | Lead | Tracking | Accuracy | Intended encounter |
|---|---:|---:|---:|---|
| Rookie | 0.55 | 0.30 | 0.30 | Early patrol: some lead keeps it dangerous, but quick changes of direction and visible shot spread give the player room to recover. |
| Veteran | 0.80 | 0.55 | 0.60 | Regular mid-run opponent: follows sustained turns and punishes predictable flight, without snapping instantly onto a new target. |
| Ace | 0.95 | 0.85 | 0.90 | Elite or named rival: nearly full lead, fast but finite tracking, and some remaining jitter. Reserve `(1, 1, 1)` for comparison runs when tuning these tiers. |

Use the existing per-task Lua setter; for example, to create a veteran without
changing global defaults:

```lua
local task = createFightTaskWithArchetype(ship, "fighter")
setFightTaskShootingSkill(task, 0.80, 0.55, 0.60)
addFightTaskTarget(task, opponent)
```

In playtests, adjust one value at a time: **lead** for shots falling behind
steady crossing targets, **tracking** for how quickly aim catches up after
turns or target switches, and **accuracy** for spread once aim has settled.
Then retest with the scenario's actual guns, shields, and enemy count. In
particular, try a crossing pass with the slowest gun before reusing a preset
tuned on Pulse I.

## Focused validation

[FightTask tests](../test/ai/testfighttask.cpp) cover existing target/squad
selection, inherited-velocity interception and unreachable shots, rearward
turning, collision-course braking, target loss, shared reactor recovery,
lead endpoints/midpoint, invalid skill rejection, tracking rate/reacquisition,
skill-independent steering/randomness, physical hit-rate calibration,
scatter volley accounting, equalized/mirrored fixtures, mutual loss, timeout
boundaries, and in-flight return-fire grace.

```sh
./build/macos/server_tests --only=FightTask --no-color
```

The standard full harness remains `cmake -P tools/agent-check.cmake`; it runs on
every platform with the documented vcpkg prerequisites.
