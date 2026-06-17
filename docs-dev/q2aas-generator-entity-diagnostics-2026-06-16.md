# Q2 AAS Entity and Content Diagnostics

Date: 2026-06-16

Tasks: `FR-04-T11`, `FR-04-T16`, `DV-07-T06`

## Summary

This round extends the q2aas validation harness with first-pass map diagnostics
from both the Quake II BSP and the generated AAS. The goal is to catch obvious
navigation input problems before runtime bot work begins: missing item/spawn
origins, pickups outside generated AAS area bounds, high-value pickups not
reachable from any spawn area in the generated reachability graph, and mover /
trigger entities that need later conditional route support.

## Implementation

- Added Quake II entity lump tokenization and key/value parsing to
  `tools/q2aas/validate_worr_q2aas.py`.
- Added entity grouping for spawn points, pickups, high-value pickups, movers,
  doors, elevators/plats, teleports, triggers, and hurt triggers.
- Added Quake II brush-content summaries from the BSP brush lump, including
  water, slime, lava, ladder, detail, translucent, and clip brush counts.
- Added AAS area and reachability parsing from the generated `.aas` file.
- Added AAS-area origin coverage for spawn points, pickups, and high-value
  pickups using generated AAS area bounds.
- Added a first-pass high-value reachability report by building a directed AAS
  reachability graph and walking it from all mapped spawn areas.
- Added a concise console diagnostics summary to `q2aas-staged-smoke`.

The AAS area assignment is intentionally conservative: it uses generated AAS
area bounding boxes with a small tolerance. This is a validation heuristic, not
a replacement for the runtime BotLib area query that Phase 2 still needs.

## Validation Results

Commands run:

```powershell
python -m py_compile tools\q2aas\validate_worr_q2aas.py
meson compile -C builddir-win q2aas-staged-smoke
```

`q2aas-staged-smoke` passed on `.install/basew/maps/mm-rage.bsp` and reported:

- Entities: `9` spawn points, `2` intermission cameras, `48` pickups, `2`
  high-value pickups, `2` movers, `1` trigger.
- Origin coverage: `0` spawn orphans, `0` item orphans.
- High-value reachability: `0` high-value pickups unreachable from spawn areas.
- Brush contents: `0` liquid brushes, `0` hazard brushes, `16` ladder brushes,
  `86` clip brushes.
- Mover inventory: `0` doors, `2` elevators/plats, `0` teleports.

The detailed diagnostics are written into both:

- `.tmp/q2aas/validation-report.json`
- `.tmp/q2aas/mm-rage.aas.meta.json`

## Current Limits

This is diagnostic coverage, not route synthesis. Doors, plats, elevators,
teleports, liquid volumes, and hurt volumes still need generator/runtime support
before bots can reason about them as conditional navigation. The next reference
map additions should deliberately include water/lava/slime, teleporters, doors,
and multi-floor movers so these counts can become real behavioral gates.
