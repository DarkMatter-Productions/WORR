# Q3A BotLib CTF Objective Gameplay Hooks - 2026-06-18

Tasks: `FR-04-T04`, `DV-03-T05`

## Summary

This Worker K slice wires the remaining real CTF objective event hooks into
`src/game/sgame/gameplay/g_capture.cpp`. The hooks now record team-objective
pickup, return, and capture observations from the authoritative
`CTF_PickupFlag()` branches instead of relying on synthetic mode `23` counters
or the generic item-touch path.

## Implementation

Changed source:

- `src/game/sgame/gameplay/g_capture.cpp`

Hook map:

- Two-flag capture: after `AwardFlagCapture(ent, other, flagTeam, pickupTime)`,
  call `BotObjectives_RecordFlagCapture(other, enemyFlagItem)`.
- Same-team dropped-flag return: after the real return scoring/stat/audio branch
  accepts the return, call `BotObjectives_RecordFlagPickup(other, ent)` before
  resetting the touched dropped flag entity.
- One Flag CTF capture: after
  `AwardFlagCapture(ent, other, scoringTeam, pickupTime)`, call
  `BotObjectives_RecordFlagCapture(other, IT_FLAG_NEUTRAL)`.
- Successful flag carry pickup: after
  `GiveFlagToPlayer(ent, other, flagTeam, flagItem)`, call
  `BotObjectives_RecordFlagPickup(other, ent)` before returning success.

These calls let `bot_objectives` classify:

- enemy flag pickups
- own flag returns
- neutral flag pickups
- enemy flag captures
- neutral flag captures

The same-team return hook is intentionally placed before `CTF_ResetTeamFlag()`
because dropped-flag reset can remove the touched entity; recording first
preserves real entity number, spawn count, source, and origin details.

## Validation

Command:

```powershell
ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_gameplay_g_capture.cpp.obj
```

Result: passed. Ninja emitted the existing shared-build warning:

```text
ninja: warning: premature end of file; recovering
```

Command:

```powershell
meson compile -C builddir-win sgame_x86_64
```

Result: passed. The build compiled the touched `g_capture.cpp` object and linked
`sgame_x86_64.dll`. Ninja again emitted the same recoverable build-log warning.

No scenario harness promotion or runtime CTF smoke was run in this scoped lane.
