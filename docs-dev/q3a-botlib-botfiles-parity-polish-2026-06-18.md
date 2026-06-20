# Q3A BotLib Botfiles Parity Polish

Date: 2026-06-18

Worker lane: Worker K, botfile asset parity polish

Tasks: `FR-04-T13`, `FR-04-T15`, `FR-04-T16`, `DV-07-T06`

## Scope

This pass stayed in the botfile asset lane requested for Worker K:

- `assets/botfiles/**`
- this implementation note under `docs-dev/`

No runtime C++, bot scenario tooling, project plan, roadmap, credits, or
user-facing docs were edited. `tools/bot_profiles/test_validate_bot_profiles.py`
was already dirty in the shared worktree and did not need changes for this
asset-only polish.

## Reference Comparison

The current WORR botfiles were compared against the local Q3A reference
directory at `E:\_SOURCE\_ASSETS\Q3A\botfiles`.

The reference layout has:

- shared root vocabularies such as `chars.h`, `inv.h`, `fw_weap.c`,
  `fw_items.c`, and `teamplay.h`;
- bot family companions under `botfiles/bots` using `_c.c`, `_w.c`, `_i.c`,
  and `_t.c` suffixes;
- chat scripts with broad event `type` coverage, including team command and
  objective response types;
- small script examples using `point`, `box`, `movebox`, `moveto`, `aim`,
  `say`, `wave`, `selectweapon`, `fireweapon`, and `wait`.

The WORR assets already had the correct five profile IDs and companion family
shape: `bulwark`, `relay`, `smoke`, `vanguard`, and `vector`. The remaining
placeholder-like gaps were mostly shared team chat coverage, Q2 utility item
coverage, a flat BFG weapon gate, duplicated generic chat flavor, and scripts
that did not yet exercise `wave`.

No Q3A or Gladiator botfile text was copied; the reference was used for layout
and vocabulary shape only.

## Asset Changes

`assets/botfiles/teamplay.h` now carries a compact Q3-style shared team chat
surface instead of only a handful of string macros. It adds original WORR
responses for location queries, help/accompany/defend/getitem/patrol/camp
orders, CTF flag events, command acknowledgements, and teammate death/fire
events.

`assets/botfiles/inv.h` now includes WORR staging symbols for Q2 utility and
coop items:

- adrenaline and ancient head;
- bandolier and ammo pack;
- blue/red/power cube/pyramid/data/pass/commander-head key families.

`assets/botfiles/fw_items.c` now has reusable weights for:

- `item_adrenaline`, `item_ancient_head`, `item_bandolier`, `item_pack`;
- `item_silencer`, `item_breather`, `item_enviro`;
- common Q2 coop key classnames.

Each `_i.c` companion now defines role-specific utility weights. Bulwark values
defensive/coop control, Relay values supplies and support utility, Smoke and
Vanguard favor tempo pickups, and Vector leans toward duel resource control.

`assets/botfiles/fw_weap.c` now treats BFG10K as a situational crowd-pressure
weapon instead of a flat ammo-present gate.

Each `_t.c` companion now adds Q2 hazard death responses and a role-specific
weapon-kill event. Bulwark also no longer carries duplicate `kill_praise`
blocks.

Each script companion under `assets/botfiles/scripts` now exercises the
idTech3-style `wave` command near its route opening while preserving the
existing deterministic route points, box bindings, weapon slots, and waits.

## Validation

Requested validation:

```bat
python tools\bot_profiles\validate_bot_profiles.py assets\botfiles\bots --format json
```

Result: passed; 5 files, 5 profiles, 0 errors, 0 warnings.

```bat
python -m pytest tools\bot_profiles\test_validate_bot_profiles.py
```

Result: passed; 18 tests passed.

Additional cheap structure checks:

- brace balance passed for 30 botfile text files;
- companion family check passed for all 5 bot IDs;
- shared coverage counts after the polish:
  - `teamplay.h`: 29 chat `type` blocks;
  - `fw_items.c`: 48 `weight` blocks;
  - `fw_weap.c`: 11 `weight` blocks;
- search check found the new adrenaline/key, hazard death, role weapon-kill,
  and `wave(...)` script hooks.

## Residual Limitations

These files are still staged authored assets until the native BotLib script VM
or a WORR-native equivalent consumes character, weapon, item, chat, and script
companions directly.

The current profile bridge still flattens `_c.c` files and uses the last
authored skill block as the active default. Skill block selection by requested
bot skill remains future work.

The new inventory IDs in `inv.h` are staging symbols for weight-script parity.
They still need reconciliation with the final runtime inventory feed before the
fuzzy weights can drive live pickup choice directly.

The expanded shared team chat event names are authored data, not proof that
every event is currently emitted by the runtime.

No `.install/` refresh or package rebuild was run in this lane because the
requested validation scope was the bot profile validator, pytest, and cheap
syntax/search checks.
