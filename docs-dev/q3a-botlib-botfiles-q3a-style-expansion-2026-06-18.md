# Q3A BotLib Botfiles Q3-Style Expansion

Date: 2026-06-18

Worker lane: Worker F, botfile assets and profile tooling

Tasks: `FR-04-T13`, `DV-07-T06`, `DV-08-T05`

## Summary

This pass responds to the style gap called out against
`E:\_SOURCE\_ASSETS\Q3A\botfiles`. The staged WORR botfiles now look and behave
more like real Q3A botfile families while keeping the project-specific bridge
fields that current WORR profile loading already documents.

The work stayed inside:

- `assets/botfiles/**`
- `tools/bot_profiles/**`
- botfile-specific docs under `docs-dev/` and `docs-user/`

No runtime source, package tooling, or `q2proto/` files were edited in this
lane.

## Asset Changes

Each existing WORR bot family now has a denser Q3-style shape:

- `_c.c` character scripts use Q3-like file headers and three authored `skill`
  blocks.
- The current runtime-facing default values are preserved by placing each
  bot's previously authored skill block last.
- Character files now include weapon-specific aim accuracy/skill tendencies in
  addition to the existing chat, movement, goal, and `WORR_*` bridge fields.
- `_t.c` chat scripts now include `teamplay.h` and have 19 event `type` blocks
  each, rather than a small validation-only chat seed.
- `_i.c` item scripts now define `GWW_*` held-weapon weights, armor-shard and
  power-screen weights, and still preserve the old per-bot item priorities.
- `teamplay.h` was added as a shared original WORR chat fragment header.

The shared script files were also tightened:

- `chars.h` now has a Q3-style header and keeps the Q3 characteristic order
  plus the high-range WORR extension constants.
- `inv.h` now includes a generic `INVENTORY_ARMOR` symbol for Q3-style armor
  balance scripts while preserving Quake II inventory names.
- `fw_weap.c` now uses ammo gates and situational enemy checks.
- `fw_items.c` now uses balance ranges, scale macros, armor/health thresholds,
  weapon-stay support, ammo thresholds, powerups, flags, and roam weights.

## Tooling Changes

`tools/bot_profiles/validate_bot_profiles.py` now understands multi-skill
character files:

- repeated fields across different `skill` blocks no longer warn as duplicate
  keys;
- duplicate fields inside the same skill block still warn;
- normalized report output includes `skill_blocks`;
- the last skill block continues to win in normalized fields, matching the
  current flat runtime bridge behavior.

The validator also checks the staged `assets/botfiles/bots` family shape by
default:

- `_c.c` files must have matching `_w.c`, `_i.c`, and `_t.c` companions;
- character files must reference those companions;
- weapon and item companions must include `inv.h` and their shared framework;
- item companions must carry `GWW_*` definitions;
- chat companions must define the matching `chat "<id>"` block and at least
  eight chat `type` blocks;
- shared `chars.h`, `inv.h`, `fw_weap.c`, and `fw_items.c` must exist.

`--skip-companion-checks` is available for import triage, but normal CI-style
validation should keep the checks on.

## Validation

- `python -B tools\bot_profiles\test_validate_bot_profiles.py`
  - Result: passed, 12 tests.
- `python -B tools\bot_profiles\validate_bot_profiles.py`
  - Result: passed, 5 profiles, 0 errors, 0 warnings.
- `python -B tools\bot_profiles\validate_bot_profiles.py --format json`
  - Result: passed; each profile reports its authored `skill_blocks`.

Observed current skill blocks:

- `bulwark`: `1`, `2`, `3`
- `relay`: `1`, `2`, `3`
- `smoke`: `1`, `3`, `4`
- `vanguard`: `1`, `4`, `5`
- `vector`: `1`, `3`, `4`

Each chat companion has 19 event `type` blocks after this pass.

## Remaining Work

These files are still staged assets until the full BotLib script VM consumes
character, weapon, item, and chat scripts natively.

The current server profile bridge still scans `_c.c` files as flat token
streams. The last authored skill block is therefore the active profile default
for now. A future native character parser should select skill blocks based on
requested bot skill instead.

The shared fuzzy-weight files are closer to Q3A and Gladiator style, but they
remain original WORR seed logic. Full parity still needs exact runtime inventory
feeding, complete weapon/item tables, and behavior consumers for the authored
weights.
