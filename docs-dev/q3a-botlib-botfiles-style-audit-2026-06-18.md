# Q3A BotLib Botfile Style Audit

Date: 2026-06-18

Worker lane: Worker 6, botfile asset QA and Q3/Gladiator style conformance

Tasks: `FR-04-T13`, `DV-07-T06`, `DV-08-T05`

## Scope

This audit covers only the staged WORR botfile assets under `assets/botfiles/`.
It does not change runtime source, tools, docs-user content, `q2proto/`, the
project plan, or the roadmap.

The quality pass compared the WORR staged assets with local reference botfile
families:

- Q3A: `E:\_SOURCE\_ASSETS\Q3A\botfiles`
- Gladiator: `E:\_SOURCE\_ASSETS\Q2-Gladiator`

The reference assets were used to check structure, naming, and broad script
shape only. The WORR botfiles remain original first-party content.

## Reference Findings

Q3A botfiles use a family-per-bot layout:

- `<id>_c.c` character/profile scripts include `chars.h`, define one or more
  `skill` blocks, and point at companion weapon, item, and chat files.
- `<id>_w.c` weapon scripts include `inv.h`, define per-bot weapon constants,
  then include a shared weapon-weight framework.
- `<id>_i.c` item scripts include `inv.h`, define per-bot item/weapon/ammo
  constants, then include a shared item-weight framework.
- `<id>_t.c` chat scripts use `chat "<id>"` with multiple `type` blocks.
- `chars.h` characteristic numeric IDs are stable and meaningful to BotLib's
  script VM, not just decorative labels.

Gladiator follows the same companion-file split while using Quake II-flavored
inventory and entity names for weights. Its character files also include richer
behavior fields than WORR currently consumes.

## Pre-Pass State

The staged WORR pack already had complete bot families for:

- `bulwark`
- `relay`
- `smoke`
- `vanguard`
- `vector`

Each family had `_c`, `_w`, `_i`, and `_t` files, and the `_c` files already
lined up with the runtime validator's profile IDs after `_c` stripping.

The gaps were style and future-consumption risks:

- `chars.h` had Q3-like names but placeholder numeric IDs that did not match
  Q3A's characteristic table.
- Character files had the required runtime bridge fields, but omitted many
  standard Q3 chat and behavior tendencies.
- Weapon and item companions were bare `#define` lists without shared
  `inv.h`, `fw_weap.c`, or `fw_items.c` script frameworks.
- Chat companions were metadata stubs rather than actual `chat` scripts with
  event `type` blocks.

## Changes Made

`assets/botfiles/chars.h` now uses the Q3A characteristic ID order for the
shared BotLib vocabulary:

- core identity/combat fields
- weapon and item companion fields
- full Q3 chat tendency fields
- movement and goal tendency fields
- `CHARACTERISTIC_WALKER`, `CHARACTERISTIC_EASY_FRAGGER`, and
  `CHARACTERISTIC_FIRETHROTTLE`

WORR-only bridge keys remain in the `1000+` extension range:

- `WORR_SKIN`
- `WORR_TEAM`
- `WORR_AIM_ERROR`
- `WORR_PREFERRED_WEAPON`
- `WORR_CHAT_PERSONALITY`
- `WORR_ROLE`
- `WORR_MOVEMENT_STYLE`

New shared WORR script headers/frameworks were added:

- `assets/botfiles/inv.h`
- `assets/botfiles/fw_weap.c`
- `assets/botfiles/fw_items.c`

The shared files provide Quake II-oriented inventory names and reusable
`weight` blocks for the current staged companions. They are intentionally
lightweight and original; they are not a full import of Q3A or Gladiator fuzzy
logic.

All five `_w.c` companions now:

- include `inv.h`
- define per-bot weapon constants
- include `fw_weap.c`

All five `_i.c` companions now:

- include `inv.h`
- define role-specific health, armor, weapon, ammo, powerup, flag, and roam
  weights
- include `fw_items.c`

All five `_t.c` companions now:

- keep `chat "<id>"` aligned with their profile ID
- include multiple original Q3-style event `type` blocks
- avoid depending on missing shared chat macro headers

All five `_c.c` character files now include additional Q3-style chat, movement,
and goal tendency fields while preserving the runtime-facing IDs, names, skins,
teams, preferred weapons, chat personalities, roles, and movement styles.

## Validation

Bot profile validator:

```bat
python tools\bot_profiles\validate_bot_profiles.py
```

Result:

- passed
- files: 5
- profiles: 5
- errors: 0
- warnings: 0

Additional structure QA was run with an inline Python check that verified:

- every profile ID has `_c`, `_w`, `_i`, and `_t` companions
- each `_c.c` references its matching companion paths
- each `_t.c` has a matching `chat "<id>"` block
- each `_w.c` includes `inv.h` and `fw_weap.c`
- each `_i.c` includes `inv.h` and `fw_items.c`
- each chat script has multiple `type` blocks
- shared `chars.h`, `inv.h`, `fw_weap.c`, and `fw_items.c` exist

Result: passed.

## Follow-Up Update

A later Worker F implementation pass is documented in
`docs-dev/q3a-botlib-botfiles-q3a-style-expansion-2026-06-18.md`. That pass
closed several gaps identified here:

- character scripts now carry multiple skill blocks;
- chat scripts now include a shared `teamplay.h` and 19 event types each;
- item companions now define `GWW_*` held-weapon weights;
- the profile validator now checks companion files under
  `assets/botfiles/bots` by default.

The remaining gap is runtime consumption: `_w`, `_i`, and `_t` scripts are still
staged data until the native BotLib script VM path consumes character, weight,
and chat files directly.

`assets/botfiles/inv.h` still uses a WORR/Quake II staging vocabulary. The
numeric inventory IDs should be reconciled with the final BotLib inventory
import table when runtime script consumption lands.

No `.install/` refresh or packaged runtime smoke was performed in this lane
because no build workflow was run and this worker's editable scope was limited
to `assets/botfiles/**` plus this audit document.
