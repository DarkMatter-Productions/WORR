# Q3A BotLib Botfile Script Parity

Date: 2026-06-18

Worker lane: Worker R, authored bot script style parity

Tasks: `FR-04-T13`, `DV-07-T06`, `DV-08-T05`

## Summary

This pass replaces the first compact `assets/botfiles/scripts/*_s.c` snippets
with more deliberately authored idTech3-style bot script companions. The
scripts remain original WORR content, but they now follow the local Q3A
reference shape more closely:

- Q3-style file headers with name, function, source, scripter, date, and tab
  size metadata.
- A single initial `script "main"` block per companion, matching the current
  validator and Q3A `botfiles/script.c` example.
- Named points, boxes, `movebox` bindings, `moveto`, `aim`, `say`,
  `selectweapon`, `fireweapon`, and `wait` statements grouped like authored
  BotLib script data rather than generated one-route samples.
- Role-specific route language for Vector, Vanguard, Smoke, Relay, and
  Bulwark, aligned with their existing `_c.c` profile roles and preferred
  weapons.

## Reference Check

The local Q3A reference tree does not include a `botfiles/scripts/` directory;
the script grammar reference is `E:\_SOURCE\_ASSETS\Q3A\botfiles\script.c`.
The wider Q3A botfiles were used for header/comment style. The available local
Gladiator source tree did not contain matching idTech3 script companion assets,
so it did not provide a file body to mirror.

## Scope

Only the five script companions were edited:

- `assets/botfiles/scripts/bulwark_s.c`
- `assets/botfiles/scripts/relay_s.c`
- `assets/botfiles/scripts/smoke_s.c`
- `assets/botfiles/scripts/vanguard_s.c`
- `assets/botfiles/scripts/vector_s.c`

No runtime source, tools, packaging code, project plan, roadmap, or companion
bot profile files were changed in this lane.

## Validation

Run after the script parity pass:

```bat
python -B tools\bot_profiles\test_validate_bot_profiles.py
python -B tools\bot_profiles\validate_bot_profiles.py
```

Results:

- `test_validate_bot_profiles.py`: passed, 14 tests.
- `validate_bot_profiles.py`: passed, 5 profiles, 0 errors, 0 warnings.

The profile validator parses the `assets/botfiles/scripts/*_s.c` companions
through the current script command contract.
