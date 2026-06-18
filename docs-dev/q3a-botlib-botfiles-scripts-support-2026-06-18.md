# Q3A BotLib Botfile Scripts Support

Date: 2026-06-18

Tasks: `FR-04-T13`, `DV-07-T06`

## Summary

Worker L added compact WORR-native botfile script companions for the current
profile pack and taught the profile validator to require and parse those
companions. This keeps the authored asset layout closer to Q3A BotLib data
without changing runtime code, source code, or packaging code.

The requested reference path,
`E:\_SOURCE\_ASSETS\Q3A\botfiles\scripts`, was not present locally. The nearest
available references were `E:\_SOURCE\_ASSETS\Q3A\botfiles\script.c` and the
existing Q3A `botfiles` text assets. Those were used only for grammar shape:
`script "main"`, named points/boxes, command-call statements, movement, aim,
say, weapon, fire, and wait commands. No external botfile bodies were copied.

## Asset Changes

Added one script companion per current WORR profile:

- `assets/botfiles/scripts/bulwark_s.c`
- `assets/botfiles/scripts/relay_s.c`
- `assets/botfiles/scripts/smoke_s.c`
- `assets/botfiles/scripts/vanguard_s.c`
- `assets/botfiles/scripts/vector_s.c`

Each file is intentionally small and self-contained. The scripts define
`script "main"` with a few authored route-check commands: `point`, `box`,
`movebox`, `say`, `selectweapon`, `moveto`, `aim`, optional `fireweapon`, and
`wait`.

## Validator Changes

`tools/bot_profiles/validate_bot_profiles.py` now:

- skips `*_s.c` files when scanning directories for profile records;
- derives `botfiles/scripts/<profile>_s.c` from every
  `botfiles/bots/<profile>_c.c` profile;
- reports `missing_script_companion` when the script companion is absent;
- parses script block declarations and requires `script "main"`;
- validates allowed command names, command arity, quoted/numeric arguments,
  integer weapon slots, `say(..., NULL)`, `wait(time(...))`, and
  `wait(touch(...))`;
- reports structured script errors such as `unknown_script_command`,
  `invalid_script_arity`, `invalid_script_argument`,
  `invalid_script_statement`, and `missing_script_command`.

`tools/bot_profiles/test_validate_bot_profiles.py` now covers valid script
companions, missing script companions, and malformed script companions.

## User Documentation

`docs-user/bot-profiles.md` now lists `basew/botfiles/scripts/*_s.c` as part of
the profile pack and explains that these files are staged/validated metadata for
future BotLib script consumption, while the character file remains the current
profile entry point.

## Validation

- `python tools\bot_profiles\test_validate_bot_profiles.py`
  - Result: passed, 14 tests.
- `python tools\bot_profiles\validate_bot_profiles.py`
  - Result: passed.
  - Observed repository assets: 5 files, 5 profiles, 0 errors, 0 warnings.

## Scope Notes

- No `src/**` files were changed.
- No packaging code was changed.
- The new assets are original WORR-authored content under
  `assets/botfiles/scripts/**`.

## Remaining Work

Runtime BotLib script loading and execution is still future work. This slice
only adds staged script data plus CI-oriented validation so later runtime work
has a deterministic asset contract.
