# Q3A BotLib Botfiles Worker I Validation

Date: 2026-06-18

Worker lane: Worker I, botfiles validation/style only

Tasks: `FR-04-T13`, `DV-07-T06`, `DV-08-T05`

## Scope

This pass validated the staged WORR botfiles under `assets/botfiles/` against
the Q3A/Gladiator family shape:

- top-level `chars.h`, `inv.h`, `fw_weap.c`, and `fw_items.c`
- one complete `_c`, `_w`, `_i`, and `_t` companion set per bot
- `_c.c` files referencing their matching weapon, item, and chat companions
- `_w.c` and `_i.c` files including `inv.h` plus the shared framework
- `_t.c` files using `chat "<id>"` with multiple `type` blocks

No runtime source, scenario tools, project plans, roadmap docs, or `q2proto/`
files were edited.

## Change

The only asset correction was a BFG10K weight token alignment. WORR already
used `INVENTORY_BFG10K`, `CHARACTERISTIC_*_BFG10K`, and `weight "BFG10K"`, but
the shared frameworks and per-bot companions used the shorter `W_BFG` symbol.

Q3A item and weapon weight scripts use `W_BFG10K`, and Gladiator item scripts
also use `W_BFG10K` for BFG pickup weighting. The WORR scripts now use
`W_BFG10K` consistently for the BFG10K weapon/item weight. `W_BFGAMMO` remains
separate for Quake II BFG ammo weighting.

Touched files:

- `assets/botfiles/fw_weap.c`
- `assets/botfiles/fw_items.c`
- `assets/botfiles/bots/bulwark_i.c`
- `assets/botfiles/bots/bulwark_w.c`
- `assets/botfiles/bots/relay_i.c`
- `assets/botfiles/bots/relay_w.c`
- `assets/botfiles/bots/smoke_i.c`
- `assets/botfiles/bots/smoke_w.c`
- `assets/botfiles/bots/vanguard_i.c`
- `assets/botfiles/bots/vanguard_w.c`
- `assets/botfiles/bots/vector_i.c`
- `assets/botfiles/bots/vector_w.c`

## Validation

Profile validator:

```bat
python tools\bot_profiles\validate_bot_profiles.py --fail-on-empty
```

Result: passed, 5 files, 5 profiles, 0 errors, 0 warnings.

Profile validator tests:

```bat
python -m unittest tools.bot_profiles.test_validate_bot_profiles
```

Result: passed, 10 tests.

Package asset tests:

```bat
python -m unittest tools.test_package_assets
```

Result: passed, 2 tests.

Actual asset package dry run:

```bat
python tools\package_assets.py --assets-dir assets --install-dir .tmp\botfiles-validation-worker-i --base-game basew --archive-name pak0.pkz
```

Result: wrote `.tmp\botfiles-validation-worker-i\basew\pak0.pkz`, packed 87
files from `assets`, and mirrored loose asset path `botfiles`.

Additional structural audit verified:

- complete `bulwark`, `relay`, `smoke`, `vanguard`, and `vector` families
- 20 bot companion files total
- matching companion references from each `_c.c`
- required includes in `_w.c` and `_i.c`
- matching `chat "<id>"` blocks with at least four `type` blocks
- balanced brace counts in staged botfile scripts
- no stray standalone `W_BFG` token remained

Result: passed.

## Remaining Notes

The existing `tools/bot_profiles/validate_bot_profiles.py` validator still
validates the runtime profile entry points, not the full Q3A fuzzy-weight or
chat grammars. The companion scripts now match the expected family shape and
obvious token vocabulary, but full grammar execution remains future BotLib
script VM work.
