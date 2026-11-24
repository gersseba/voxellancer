# Weapons, shields, and voxel damage

**Status: implemented first pass, reference-tested; balance still needs playtesting.**
The six catalog weapons use these live INI values in solo and co-op contracts.
Station purchases, inventory, and per-mount fitting are implemented; progress
persists between missions within a run, not across New Run or game/server restarts.
Post-attack salvage can also award a catalog gun or a personal run upgrade.
Payload amplifiers add 15% of base damage per stack to all newly fired gun
projectiles; shield capacitors add 20% of authored shield capacity per stack,
preserving charge fraction. Refits and shield purchases retain these bonuses.
The tables below are unmodified reference values; UI weapon statistics include
the current pilot's payload bonuses.

## Scope

- Each weapon owns exactly one fixed projectile profile. Players equip and buy
  weapons, not separate ammunition/projectile variants.
- Physical and energy shots share collision and damage resolution. Thermal and
  disruption payloads become one **energy payload**, without equipment-disable
  effects in this version.
- No charge mechanics, heat generation, or shield-coupling parameters.
- Player-facing weapon statistics: velocity, travel distance, damage against
  shields, armor, and normal blocks. Damage is per trigger event; scatter
  weapons explicitly identify the pellet count.
- Cadence and impact pattern remain necessary internal balance parameters.
  The subsequent contract progression adds a regenerating firing-energy
  reserve, not ammunition, reloading, heat, or shield coupling.

## Reference targets and units

- Distances use world units (U), not an assumed conversion to meters.
- Normal block: **50 HP**, matching [the current default](../data/voxels.ini).
- Armor block: **150 HP**, density 3, encoded with the `0x000A` material prefix
  in [voxel definitions](../data/voxels.ini). Crucial voxels remain a separate
  material. Existing ships are not automatically converted to armored hulls.
- Reference shield: **200 HP**, matching
  [the Starter shield](../data/equipment/shields/thinshield.ini).
- Baseline gun: **Pulse Repeater I**, 10 shield and 10 hull damage per shot,
  10 energy, 0.5 s interval: 20 damage and 20 energy per second.
- Damage values are target HP removed, not joules. Shield and block HP use
  the same small-number scale, so a Pulse I shot removes 10 of either.
- Reference tests use stationary targets, direct perpendicular impacts,
  no shield regeneration, and invulnerability off. Block tests use no shield.

The [profiled projectile resolver](../src/world/handler/mover.cpp) applies the
budgets below through continuous shield and hull accounting. Legacy unprofiled
weapons and body collisions retain their existing damage paths.

## Initial weapon roster

All damage columns are full-payload budgets against the specified target, not
damage applied independently to every affected voxel. Plasma and scatter
budgets must be shared among their contacts.

| Weapon | Velocity (U/s) | Travel distance (U) | Shield damage | Armor damage | Normal-block damage | Internal firing interval | Energy / trigger |
|---|---:|---:|---:|---:|---:|---:|---:|
| Pulse Repeater I | 500 | 600 | 10 | 10 | 10 | 0.50 s | 10 |
| Pulse Repeater II | 600 | 780 | 15 | 15 | 15 | 0.40 s | 12 |
| Heavy Autocannon | 400 | 800 | 5 | 30 | 30 | 0.50 s | 8 |
| Railgun | 625 | 1,400 | 45 | 75 | 75 | 1.50 s | 30 |
| Plasma Mortar | 375 | 450 | 40 | 10 total | 40 total | 1.00 s | 25 |
| Scatter Cannon | 375 | 350 | 24 total | 8 total | 32 total | 0.80 s | 16 |

Authored gun projectiles, including legacy guns, stay between 375 and 625 U/s
(0.75-1.25 times the Pulse Repeater I baseline of 500 U/s). These are launch
speeds; profiled projectiles also inherit the firing ship's velocity. Homing
rockets are outside this band.

### Use cases relative to Pulse Repeater I

Ratios compare one gun at full rate with every shot landing, against Pulse I's
20 shield DPS, 20 hull DPS and 20 energy/s.

| Weapon | Role | Shield DPS | Hull DPS (normal / armor) | Energy / s | Use case |
|---|---|---:|---:|---:|---|
| Pulse Repeater I | Baseline all-rounder | 20 (1×) | 20 / 20 (1×) | 20 (1×) | Reliable at 600 U. A pair exactly matches the reactor, so it never runs dry. |
| Pulse Repeater II | High-output repeater | 37.5 (1.9×) | 37.5 / 37.5 (1.9×) | 30 (1.5×) | Straight upgrade with longer range and 25% more damage per energy. A pair overdraws the reactor by 20/s, emptying it in about five seconds before throttling. |
| Heavy Autocannon | Hull and armor breaker | 10 (0.5×) | 60 / 60 (3×) | 16 (0.8×) | Weak against shields but cheap and brutal against bare or armored hulls; pair it with a pulse gun that strips shields. Penetrates two voxels. |
| Railgun | Long-range penetrator | 30 (1.5×) | 50 / 50 (2.5×) | 20 (1×) | Same energy rate as Pulse I but 2.3× the range and large single hits. Rewards accuracy; penetrates up to six voxels. |
| Plasma Mortar | Shield breaker / splash | 40 (2×) | 40 / 10 (2× / 0.5×) | 25 (1.25×) | Fastest shield removal, at short range with a slow shell. Splash spreads hull damage over exposed surface modules; poor against armor. |
| Scatter Cannon | Close-range burst | 30 (1.5×) | 40 / 10 (2× / 0.5×) | 20 (1×) | Point-blank shotgun: needs to be within 350 U for most pellets to land. Same energy as Pulse I. |

Pulse II is the only straight tier upgrade. The other guns are sidegrades:
each beats Pulse I at one job and pays with range, energy, cadence or a weak
damage type.

### Contract reactors and shield builds

Contract pilots share 100 weapon energy across all mounts, regenerating 40/s
at all times, including while firing. Unaffordable triggers spawn nothing and
spend nothing. Scatter pays once for all eight pellets. Mount priority rotates
between requests so one mount cannot permanently monopolize a nearly empty
reserve.

The starter ship has five compatible mounts and begins with two symmetric Pulse I
guns. Together they spend exactly 40 energy/s, so they can fire indefinitely;
any stronger or additional gun spends faster than the reactor recovers. Against
an identical stationary ship, with all shots hitting and no upgrades or
invulnerability, the 200 HP starter shield breaks after ten volleys, about five
seconds of fire. Its 2 s recovery delay keeps it from regenerating while it is
under sustained fire. Projectile travel adds impact delay.

| Shield | Capacity (HP) | Regeneration (HP/s) | Delay after hit | Price (CR) |
|---|---:|---:|---:|---:|
| Starter | 200 | 20 | 2 s | Starting equipment |
| Balanced Mk2 | 300 | 30 | 2 s | 100 |
| Bulwark | 450 | 20 | 5 s | 150 |
| Flux | 150 | 50 | 1 s | 150 |

Shield recharge is independent of weapon energy and resumes even after a
continuous-HP shield breaks. Positive absorbed damage restarts the delay; a
zero-damage hit does not. Run shield-capacity bonuses apply to each build's
authored capacity, not its regeneration. At bases, **N** opens build selection;
purchases equip immediately after server confirmation and discard the previous
shield without refund. Switching builds is allowed at full price; rebuying the
equipped build is rejected without charging. Bases refill shields and weapon
energy, not hulls. New Run restores Starter and a full reactor.

Flight HUD bars show current/max reserves, shield recharge waits, broken shields, and
low-energy feedback. Weapon cards expose energy per trigger. Only contract
pilots currently opt into reactors; existing NPCs and legacy scenarios retain
unlimited firing unless reactor properties are authored.

### Fixed projectile profiles

| Weapon | Profile | Flight lifetime at launch speed | Hull behavior |
|---|---|---:|---|
| Pulse Repeater I | Energy bolt | 1.20 s | Deposit budget into the first surface voxel; no penetration |
| Pulse Repeater II | Energy bolt | 1.30 s | Same pattern, with higher damage and speed |
| Heavy Autocannon | Physical slug | 2.00 s | Penetrate at most two successive voxels |
| Railgun | Physical penetrator | 2.24 s | Penetrate at most six successive voxels; fires immediately, no charging |
| Plasma Mortar | Energy payload | 1.20 s | Spread one budget over exposed surface voxels within 2 U of impact; at most 12 recipients; no penetration |
| Scatter Cannon | Eight physical pellets per trigger event | 0.93 s | Each pellet: 3 shield / 1 armor / 4 normal damage; no penetration; 8-degree full spread cone |

### Aim scatter

Each projectile may define `impact.scatter`, the half-angle in degrees of a
small cone around the aim point. Every shot (and every scatter pellet) is
deflected uniformly across that cone's cross-section using a deterministic
per-gun sequence, so authoritative server runs stay reproducible. Scatter is
applied after crosshair convergence and target lead, so the HUD lead indicator
still marks the cone's center. The cone is meant to stop pinpoint fire on the
crucial voxel at medium and long range without being visible as spray.

| Weapon | Scatter half-angle | Radius at 100 U | Radius at 300 U | Radius at 600 U |
|---|---:|---:|---:|---:|
| Pulse Repeater I / II | 0.15° | 0.26 U | 0.79 U | 1.57 U |
| Heavy Autocannon | 0.25° | 0.44 U | 1.31 U | 2.62 U |
| Railgun | 0.05° | 0.09 U | 0.26 U | 0.52 U |
| Plasma Mortar | 0.20° | 0.35 U | 1.05 U | 2.09 U |
| Scatter Cannon | 0° | fixed 8-pellet ring | — | — |

A voxel is 1 U. Measured with perfect AI aim at 300 U, Pulse I lands about 80%
of shots on a head-on raider (7 × 3 voxel silhouette) and 100% on the contract
ship.

One-to-one ownership means one projectile *definition* per weapon, not one
projectile instance per trigger event. Scatter repeats its own fixed pellet
profile eight times; it does not offer a separate ammunition choice.

Travel distance is a hard path-length limit measured relative to the launching
ship's inertial velocity at firing, so firing while moving does not silently
change the listed range. Expire at either that limit or the listed lifetime,
whichever comes first. Listed range describes uninterrupted flight; penetration
can shorten it by reducing speed. Stop a penetrator once its remaining budget
is exhausted.

For plasma, enumerate exposed candidates before removing any voxel, then share
the budget using normalized distance weights `max(0, 1 - distance / 2 U)`.
Choose the nearest 12 eligible surface voxels in stable order, including the
impact voxel. Occluded voxels receive nothing; destroying an outer voxel does
not expose an inner layer to this same shot. No eligible recipients means no
hull damage. Visual particles never add damage.

## Budget and physics rules

### Shields

There is no coupling/resistance multiplier. Use the weapon's shield damage
directly. For a full shot and remaining shield HP `H`:

```text
absorbed HP = min(H, listed shield damage)
remaining fraction = 1 - absorbed HP / listed shield damage
remaining armor damage = listed armor damage * remaining fraction
remaining normal damage = listed normal damage * remaining fraction
```

All listed shield damage values are positive. Apply this accounting once;
shield interception must not charge the hit again during hull collision.
Only one material-specific hull value is used at each contact.

Example: a Pulse I hits a shield with 4 HP remaining. It removes those
4 HP and carries 60% of its payload onward: **6 normal or 6 armor damage**,
not both. A shield with at least 10 HP stops it entirely.

Scatter resolves this rule per pellet against the then-current shield HP,
not once per pellet using the whole volley's 24 damage.

### Hulls

- A full projectile starts with a remaining budget fraction of 1. After shield
  absorption it starts hull resolution with the remaining fraction above.
- A voxel's damage capacity is that fraction times the listed damage for its
  material. Removing `h` voxel HP spends `h / listed material damage` of the
  original budget. Never reset the fraction after destroying a voxel.
- Nonpenetrating shots deposit their budget at the first voxel. Unused
  destructive capacity becomes local heating/deformation, not extra damage
  to hidden blocks. Penetrators continue only through destroyed voxels and
  within their profile's traversal limit.
- Plasma apportions its fraction by surface weights; scatter apportions its
  volley through its eight pellets. Never apply the full budget to each
  recipient.
- Actual voxel intersections govern which cells are hit. This first pass spends
  budget per voxel, not proportionally to the chord length inside it; there is
  no additional obliquity/ricochet model.

Physical projectiles retain mass, momentum, recoil, and swept collisions.
The listed physical damage is calibrated at reference impact speed; changing
relative impact energy scales the initial damage budget consistently. Evaluate
that scaling once at the first contact, not again after shield absorption or
each penetrated voxel. Penetration spends kinetic energy and reduces speed.
Projectile masses are 0.1 for autocannon, 0.03 for rail, and 0.01 per scatter
pellet in engine mass units. Damage HP alone does not determine knockback.
Off-center impacts approximate angular impulse using ship mass and bounds;
this is not a full rigid-body inertia tensor. Physical shots resweep their
remaining tick at the reduced velocity after each penetrating contact.

Energy projectiles carry one authored energy payload, rather than obtaining
their damage from artificial mass. They cause negligible mechanical recoil
and impulse. Do not infer payload strength from the number of visual particles.
Transferred momentum must not be applied again to every detached fragment.

## Expected reference results

Isolated-block counts below assume repeated hits on the same healthy voxel;
they are not whole-ship kill counts. Shield counts ignore regeneration and
assume every shot/pellet lands. Rounded-up hit counts include partial final hits.

| Weapon | Trigger events to exhaust 200 shield HP | Hits to destroy one 50-HP normal block | Hits to destroy one 150-HP armor block |
|---|---:|---:|---:|
| Pulse Repeater I | 20 | 5 | 15 |
| Pulse Repeater II | 14 | 4 | 10 |
| Heavy Autocannon | 40 | 2 | 5 |
| Railgun | 5 | 1 | 2 |
| Plasma Mortar | 5 | 2 if it is the only recipient | 15 if it is the only recipient |
| Scatter Cannon | 9 full volleys | 13 pellets | 150 pellets, requiring at least 19 volleys |

Expected damage patterns are more informative than isolated-block counts:

- **Pulse I/II:** 10 or 15 damage to the first surface voxel; no damage carries
  into the block behind it, even when the shot destroys a weakened voxel.
- **Autocannon:** 30 damage to the first voxel. Against a voxel with 30 HP or
  less it carries the remainder into the next one, up to two voxels.
- **Railgun:** removes one healthy normal voxel and deals 25 to the next.
  Against a 60-HP armor voxel followed by normal material, armor consumes
  `60 / 75` of the budget, leaving 15 normal damage for the voxel behind it.
- **Plasma:** if four normal surface voxels receive equal weights, each takes
  **10 damage**. A lone exposed voxel takes all 40. It does not damage blocks
  hidden behind the surface.
- **Scatter:** each pellet deals **4 normal or 1 armor damage**, spread across
  the target's surface. The all-hit volley totals are 32 normal or 8 armor
  damage before overkill losses.

Full-payload sustained DPS, useful for internal comparisons only:

| Weapon | Shield DPS | Armor DPS budget | Normal DPS budget | Energy / s |
|---|---:|---:|---:|---:|
| Pulse Repeater I | 20 | 20 | 20 | 20 |
| Pulse Repeater II | 37.5 | 37.5 | 37.5 | 30 |
| Heavy Autocannon | 10 | 60 | 60 | 16 |
| Railgun | 30 | 50 | 50 | 20 |
| Plasma Mortar | 40 | 10 | 40 | 25 |
| Scatter Cannon | 30 | 10 | 40 | 20 |

These are ideal budget rates while energy is available, not indefinitely
sustainable reactor-limited rates or measured damage/time-to-kill guarantees.
Spread, overkill, shield overflow, movement, geometry, and structural splitting
change practical results. There is no range falloff inside the listed travel
distance in this first pass; plasma's surface distribution still has falloff.

## Station catalog and validation

| Weapon | Price (credits) |
|---|---:|
| Pulse Repeater I | 100 |
| Pulse Repeater II | 200 |
| Heavy Autocannon | 150 |
| Railgun | 200 |
| Plasma Mortar | 150 |
| Scatter Cannon | 100 |

Each pilot receives one random offer at the starting base and when reaching
another Base node on the sector map. Winning an attack returns to the map,
not the shop, and does not reroll offers. Buying adds one gun to a 64-weapon inventory and marks that offer
sold; unsuccessful actions do not reroll it. Fitted weapons are separate from
storage capacity. The equipment screen swaps one exact mount and returns its
previous gun to storage. Buying/fitting clears personal readiness.

Focused [contract tests](../test/server/testtcpserver.cpp) cover surface versus
penetrating damage, mixed armor/normal budgets, within-tick slowdown, plasma
occlusion, hard range, shield overflow, invulnerability, scatter totals,
purchase/swap validation, shared energy and mount fairness, once-per-volley
spending, depleted-shield recovery, partial-tick recharge, base refills,
shield switching/run resets, and TCP co-op replication. Protocol tests cover
resource round trips and malformed values. The tables above include
arithmetic reference predictions, not a claim that every row has its own test.
Remaining validation includes extensive balance playtesting, four-pilot UX,
equal-weight plasma fixtures, and worst-case profiling. No charge, heat,
shield-coupling, ammunition, ricochet, or equipment-disable system was added.

All damage stays on the authoritative fixed-tick server. Bound voxel work,
use stable impact ordering and deterministic scatter, and keep presentation
separate from damage. Validate high-speed collision sweeps, range limits,
armor occlusion, structural splitting, replication, and worst-case server cost.
Disable prototype invulnerability when evaluating combat balance.
