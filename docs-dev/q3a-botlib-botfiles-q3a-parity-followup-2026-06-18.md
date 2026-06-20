# Q3A BotLib Botfiles Q3A Parity Follow-Up

Date: 2026-06-18

Worker lane: Worker H, botfile asset/script parity follow-up

Tasks: `FR-04-T13`, `FR-04-T15`, `FR-04-T16`, `DV-07-T06`

## Scope

This pass stayed in the repository botfile lane:

- `assets/botfiles/teamplay.h`
- this implementation note under `docs-dev/`

No runtime source, bot scenario tooling, q2aas tooling, user docs, project plan,
roadmap, or `q2proto/` files were edited.

## Comparison Findings

The current WORR botfiles were compared against
`E:\_SOURCE\_ASSETS\Q3A\botfiles` and the existing botfile plan notes.

WORR already matched the main Q3A/Gladiator-style family shape:

- shared root files such as `chars.h`, `inv.h`, `fw_weap.c`, `fw_items.c`, and
  `teamplay.h`;
- bot family companions using `_c.c`, `_w.c`, `_i.c`, and `_t.c`;
- compact `botfiles/scripts/*_s.c` route companions using idTech3 script
  commands;
- Q2-oriented weapon, item, utility, and key weights authored as original WORR
  data.

The remaining parity gap found in this lane was the shared `teamplay.h` event
surface. Q3A's `botfiles/teamplay.h` defines 70 shared team-chat type names.
The current WORR file had the newer command/start/stop and CTF basics, but was
still missing the Q3A arrival, status, subteam, checkpoint, leadership, and
command-parser event names.

## Asset Change

`assets/botfiles/teamplay.h` now carries original WORR responses for every Q3A
shared teamplay type name. The added blocks cover:

- location and team-location replies;
- accompany arrival, cannot-find, and flag-carrier support;
- camp arrival and active status reports for help, accompany, defend, get item,
  kill, camp, patrol, capture, rush, return, attack, harvest, and roam states;
- subteam join/leave/status, checkpoint confirmation, follow/lead commands, and
  offense/defense preference replies;
- CTF defend-base, carrier-death, and carrier-kill events;
- team leader query/announcement events;
- command parser acknowledgements for accompany-me, attack-enemy-base, and
  harvest orders.

No Q3A or Gladiator botfile text was copied. The reference assets were used for
structure and event-name coverage; the response strings remain WORR-authored.

## Validation

```bat
python -B tools\bot_profiles\validate_bot_profiles.py assets\botfiles\bots --format json
```

Result: passed with 5 files, 5 profiles, 0 errors, and 0 warnings.

```bat
python -B tools\bot_profiles\validate_bot_profiles.py
```

Result: passed with 5 files, 5 profiles, 0 errors, and 0 warnings.

```bat
python -m pytest tools\bot_profiles\test_validate_bot_profiles.py
```

Result: passed, 18 tests.

```bat
python -m pytest tools\test_package_assets.py
```

Result: passed, 11 tests.

Additional comparison check:

- Q3A `teamplay.h` type names: 70
- WORR `teamplay.h` type names: 70
- missing Q3A names in WORR: 0
- WORR extra names: 0

Additional syntax check:

- brace balance passed for `assets/botfiles/**/*.c` and
  `assets/botfiles/**/*.h`.

## Residual Notes

The current server profile bridge still loads `_c.c` character entries and
skips companion scripts as staged data. The expanded shared team chat surface is
therefore parity-ready asset content for later native BotLib chat/script
consumption, not proof that each event is emitted by the current runtime.

The broader Q3A root grammar files such as `match.c`, `syn.c`, `rnd.c`, and
`rchat.c` remain outside this targeted follow-up. They should only be added when
the runtime has a planned consumer for those tables.
