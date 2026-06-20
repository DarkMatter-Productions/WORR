# Q3A Botfile Script Tactical Routines

Date: 2026-06-18

Worker lane: botfile asset script parity

Tasks: `FR-04-T13`, `DV-07-T06`, `DV-08-T05`

## Scope

This pass is limited to authored WORR botfile assets and this development note:

- `assets/botfiles/scripts/*_s.c`
- `docs-dev/botfiles/q3a-botfile-script-tactical-routines-2026-06-18.md`

No C++, profile validator code, packaging tools, `tools/bot_scenarios`, user
docs, `q2proto/`, roadmap, or project-plan files were edited.

The repository has no source `basew/botfiles/` directory. Authored botfiles live
under `assets/botfiles/` and are packaged or mirrored into
`.install/basew/botfiles/` by the install refresh workflow.

## Reference Comparison

The current WORR script companions were compared against:

- Q3A script grammar example:
  `E:\_SOURCE\_ASSETS\Q3A\botfiles\script.c`
- Q3A bot family layout and chat/weight companions under:
  `E:\_SOURCE\_ASSETS\Q3A\botfiles`
- Gladiator Quake II botfile vocabulary under:
  `E:\_SOURCE\_ASSETS\Q2-Gladiator`

Q3A documents named script blocks with `script "main"` as the initial entry.
The local WORR validator accepts multiple named script blocks and the existing
command set: `point`, `box`, `movebox`, `moveto`, `aim`, `say`, `wave`,
`selectweapon`, `fireweapon`, and `wait`.

## Changes

Each bot script kept its existing `script "main"` route and gained one
self-contained named tactical routine:

- `bulwark_s.c`: `anchor_hold`, a short armor-stack hold and fallback step.
- `relay_s.c`: `escort_supply`, a support rendezvous that waits on teammate
  touch before rotating supplies.
- `smoke_s.c`: `corner_trap`, a rocket splash setup and cover exit.
- `vanguard_s.c`: `break_choke`, a front-line choke entry and pressure exit.
- `vector_s.c`: `reposition`, a rail-lane reset and denial angle.

The additions are intentionally additive and parser conservative. They do not
introduce new commands, preprocessor dependencies, external assets, or copied
reference text.

## Validation

Profile validator against the active authored botfile family:

```bat
python -B tools\bot_profiles\validate_bot_profiles.py assets\botfiles\bots --format json
```

Result: passed; 5 files, 5 profiles, 0 errors, 0 warnings.

The same validator was also run in human-readable mode:

```bat
python -B tools\bot_profiles\validate_bot_profiles.py assets\botfiles\bots
```

Result: passed; 5 profiles, issues: none.

A lightweight local script structure check verified:

- all five script companions exist;
- each companion defines `main` plus exactly one named tactical routine;
- every script block has at least one `point`, `box`, `movebox`, `moveto`, and
  `wait`;
- script braces are balanced.

Result: passed for all 5 bot scripts.

Whitespace check:

```bat
git diff --check -- assets\botfiles\scripts docs-dev\botfiles
```

Result: passed.

## Remaining Gaps

- The script companions are still authored data until runtime BotLib script
  execution consumes them directly.
- There is still no map-specific script selection or trigger path for these
  named routines.
- The tactical coordinates are neutral seed data, not per-map tuned route data.
- `.install/basew/botfiles/` needs a refresh after the broader build/install
  workflow when packaging artifacts are required.
