# Q3A botlib team/coop objective policy round - 2026-06-18

Task ID: `Q2REPRO-BOT-OBJECTIVE-POLICY-2026-06-18`

## Scope

This round advances the WORR-native bot objective policy layer in `src/game/sgame/bots/bot_objectives.*`.
It is intentionally limited to reusable policy helpers and status-friendly results. It does not wire new autonomous
commands into `bot_brain.cpp`, mutate navigation state, or claim complete team/coop behavior.

## Implementation

- Extended match policy mode naming with a `cooperative` mode so coop contexts can be reported without being treated
  as deathmatch scoring decisions.
- Added coop context and policy helpers:
  - `BotObjectives_BuildCoopContext(...)` records coop state, alive bot state, leader selection, player counts, and
    leader distance.
  - `BotObjectives_EvaluateCoopPolicy(...)` selects data-only intents for follow, wait, regroup, lead advance, and
    support combat.
  - Coop leader selection prefers an alive human player, then falls back to another alive player when no human leader
    is available.
- Added resource policy helpers:
  - `BotObjectives_BuildResourceContext(...)` combines match policy, coop policy, item category, and caller-provided
    need/contest hints.
  - `BotObjectives_EvaluateResourcePolicy(...)` returns data-only take/share/reserve/deny/objective decisions with
    priorities and reasons.
- Expanded `BotObjectiveStatus` with coop/resource counters and last-result fields so later `bot_brain` work can expose
  or consume the policy decisions without changing the helper contracts.

## Behavior boundaries

- FFA/TDM/CTF match policy still describes scoring, collection, objective preference, role shape, and friendly-fire
  filtering. Coop is labeled as `cooperative` but is not counted as deathmatch scoring.
- Coop policy is advisory only. It produces follow/wait/regroup/lead/support intent data; it does not press movement
  buttons, issue follow commands, teleport, open doors, or wait at progression gates.
- Resource policy is advisory only. `mayPickup = false` means a future consumer should consider reserving the item; this
  slice does not suppress item pickup or reroute a bot.

## Follow-up integration

- A future brain-owned slice can call `BotObjectives_BuildCoopContext(...)` and `BotObjectives_EvaluateCoopPolicy(...)`
  during frame policy evaluation.
- Item selection can call `BotObjectives_BuildResourceContext(...)` and `BotObjectives_EvaluateResourcePolicy(...)` with
  actual self/teammate need and enemy contest signals.
- Status output can add the new coop/resource `BotObjectiveStatus` fields without changing policy evaluation semantics.

## Validation

- `git diff --check -- src/game/sgame/bots/bot_objectives.hpp src/game/sgame/bots/bot_objectives.cpp`
  completed without whitespace errors. Git reported the repository's usual CRLF normalization warning for the touched
  source files.
- `ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_objectives.cpp.obj` completed successfully.
- `ninja -C builddir-win sgame_x86_64.dll` completed successfully, including recompiling `bot_brain.cpp` against the
  expanded policy header. Ninja reported `premature end of file; recovering` after the successful build.
