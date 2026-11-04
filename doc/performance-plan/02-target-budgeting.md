# Step 2: Bound and stagger AI target evaluation

## Goal

Avoid asking every ship and weapon to evaluate every available target on every
world update.

## Baseline evidence

The [persisted baseline](../../benchmark-results/large-combat-vsync-baseline.json)
records **17.250 ms/frame** for AI target scan and firing in the slowest
three-second interval. This parent metric includes **7.275 ms/frame** of
line-of-fire queries. In
[`BoardComputer::shootBullet`](../../src/ai/boardcomputer.cpp), the current loop
passes every valid target to the ship components for firing checks.

## Planned change

Measure target-list sizes and weapon checks per ship, then rank a bounded
candidate set using inexpensive tests first (for example range, squared
distance, and aim alignment). Evaluate only the best few candidates per
hardpoint or ship, and stagger target refreshes across simulation ticks.
Keep authoritative firing and collision checks for selected targets.

Treat target-count limits and stagger intervals as behavior changes: preserve
focus-fire and faction rules, and tune them against combat outcomes rather than
FPS alone.

## Assumed improvement

Hypothesis: reduce the enclosing target-scan-and-firing CPU metric by
**40-70%**, approximately **6.9-12.1 ms/frame** in the baseline's worst window
if each ship currently considers substantially more targets than the selected
budget. Confidence is low until candidate counts are instrumented. This range
includes line-of-fire work and overlaps Step 1; do not add the two estimates.

## Validation

- Record targets considered, targets selected, weapon aim checks, and line-of-
  fire queries per frame.
- Compare hit rate, damage dealt, time to destroy ships, and surviving ships
  alongside FPS and CPU timings.
- Test several target budgets and stagger intervals with the same placement
  seed; reject settings that materially degrade combat behavior.
