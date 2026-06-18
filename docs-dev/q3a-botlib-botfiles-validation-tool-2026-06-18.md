# Q3A BotLib Botfiles Validation Tool

Date: 2026-06-18

Tasks: `FR-04-T13`, `DV-07-T06`

## Summary

This round adds a standard-library Python validator for WORR bot profile files.
The tool is intended for CI use before authored profile assets are packaged,
and it matches the current server loader's simple token stream closely enough
to validate Q3-style character scripts, `key value`, `key = value`, quoted
values, braces, comments, preprocessor lines, and trailing `;`/`,` punctuation.

Profile assets remain owned by the parallel asset-authoring work. This change
did not copy or import external botfile text.

## Implementation Notes

- Added `tools/bot_profiles/validate_bot_profiles.py`.
- Added `tools/bot_profiles/test_validate_bot_profiles.py`.
- The validator derives profile IDs from filenames, matching `SV_BotReloadProfiles()`.
- `botfiles/bots/*_c.c` strips the `_c` suffix, so `smoke_c.c` validates as
  profile id `smoke`.
- `botfiles/bots/*_w.c`, `*_i.c`, and `*_t.c` are skipped when a directory is
  scanned because they are Q3-style companion scripts, not profiles.
- Default validation scans:
  - `assets/botfiles/bots/*_c.c` profile entry points, skipping companion
    scripts in the same directory.
  - `assets/bots/profiles/*.bot`
  - `assets/bots/*.bot`
- Missing default roots are ignored; a no-profile run reports `no_profiles` as a warning unless `--fail-on-empty` is supplied.
- Required authored identity fields after alias normalization:
  - `name`
  - `skin`
- Recognized keys mirror the current server bridge:
  - `name`, `skin`, `team`, `skill`
  - `CHARACTERISTIC_NAME`, `CHARACTERISTIC_SKIN`, `CHARACTERISTIC_TEAM`
  - `CHARACTERISTIC_REACTIONTIME`
  - `CHARACTERISTIC_AGGRESSION`
  - `WORR_SKIN`, `WORR_TEAM`
  - `WORR_REACTION_MS`, `WORR_AIM_ERROR`
  - `WORR_PREFERRED_WEAPON`, `WORR_CHAT_PERSONALITY`
  - `WORR_ROLE`, `WORR_MOVEMENT_STYLE`
  - `reaction`, `reaction_time`, `reaction_ms`
  - `aggression`, `aggression_bias`
  - `aim_error`, `aimerror`, `accuracy_error`
  - `preferred_weapon`, `weapon`, `favorite_weapon`
  - `chat_personality`, `chat`, `personality`
  - `role`, `team_role`
  - `movement_style`, `movement`, `move_style`
- Numeric checks are intentionally practical and conservative:
  - `skill`: `0..5`
  - `reaction`: `0..5000`
  - `aggression`: `0..1`
  - `aim_error`: `0..90`
- Duplicate profile IDs are checked case-insensitively across all validated files.
- Duplicate keys warn because the current runtime loader accepts the last value.
  Repeated fields across separate Q3-style `skill` blocks are exempt because
  they are intentional character variants; duplicates inside the same skill
  block still warn.
- The JSON report includes authored `skill_blocks` for Q3-style character
  files.
- Staged `assets/botfiles/bots/*_c.c` files now get companion checks by
  default:
  - matching `_w.c`, `_i.c`, and `_t.c` files;
  - character references to those companions;
  - `inv.h` and framework includes in weight companions;
  - `GWW_*` item companion weights;
  - matching chat block and a minimum chat type count;
  - required shared botfile headers/frameworks.
- `--skip-companion-checks` is available for exploratory imports.
- Unknown WORR keys fail by default so CI catches misspellings;
  `--allow-unknown` downgrades them to warnings for exploratory imports.
  Unknown `CHARACTERISTIC_*` keys are ignored because real Q3-style botfiles
  carry many character traits that the interim WORR bridge does not consume
  yet.
- Text output is concise for logs; JSON output includes summary, normalized profile fields, and structured issues.

## Validation

- `python tools\bot_profiles\test_validate_bot_profiles.py`
  - Result: passed, 12 tests after the multi-skill and companion-check update.
- `python tools\bot_profiles\validate_bot_profiles.py --format json`
  - Result: passed.
  - Observed current repository assets under `assets/botfiles/bots`: 5
    character files, 5 profiles, 0 errors, 0 warnings; 15 companion scripts are
    skipped by directory scans.
- `python tools\bot_profiles\validate_bot_profiles.py`
  - Result: passed.
  - Text output reported the current `bulwark`, `relay`, `smoke`, `vanguard`, and `vector` profile IDs.

## Remaining Risks

- The validator intentionally validates the authored WORR/Q3-style profile
  bridge subset, not the full Q3A character, weapon-weight, item-weight, or
  chat grammar.
- Numeric ranges may need tightening once gameplay consumes profile behavior values directly.
- Runtime still ignores unknown keys, while CI fails them by default; use `--allow-unknown` only for import triage.
