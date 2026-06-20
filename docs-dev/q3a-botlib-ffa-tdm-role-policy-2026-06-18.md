# Q3A BotLib FFA/TDM Role Policy Helpers

Date: 2026-06-18

Tasks: `FR-04-T04`, `FR-04-T06`, `FR-04-T15`, `DV-03-T05`

## Summary

This worker slice adds objective-side policy helpers for FFA, TDM, and CTF
role shape without changing live bot command ownership. The new APIs live in
`src/game/sgame/bots/bot_objectives.*` and return deterministic metadata for
scoring participation, attack/defense/midfield roles, item-role splits, and
friendly-fire avoidance.

No `bot_brain.cpp`, combat, nav, item, or scenario runner files were edited.

## Implementation

Changed files:

- `src/game/sgame/bots/bot_objectives.hpp`
- `src/game/sgame/bots/bot_objectives.cpp`
- `docs-dev/q3a-botlib-ffa-tdm-role-policy-2026-06-18.md`

New policy vocabulary:

- `BotObjectiveMatchMode`: `FreeForAll`, `TeamDeathmatch`, `CaptureTheFlag`
- `BotObjectiveRole::Midfielder`
- `BotObjectiveItemCategory`: health, armor, ammo, weapon, powerup, tech,
  CTF objective, utility
- `BotObjectiveItemRole`: self stack, weapon control, powerup control, team
  resource, deny enemy, objective

New helper APIs:

- `BotObjectives_BuildMatchContext(bot, requestedRole)`
- `BotObjectives_EvaluateMatchPolicy(context)`
- `BotObjectives_ItemCategoryForItem(item)`
- `BotObjectives_EvaluateItemRolePolicy(matchPolicy, category, priority)`
- `BotObjectives_BuildFriendlyFireContext(shooter, target, friendlyInLineOfFire)`
- `BotObjectives_EvaluateFriendlyFirePolicy(context)`
- name/default/priority helpers for match modes, item categories, item roles,
  and match role lanes

## Policy Rules

FFA policy marks bots as scoring participants with roam, collect, and engage
intent, chooses a midfield lane, and sets spawn-camping avoidance metadata.

TDM policy assigns stable attack, defense, or midfield roles from client/team
identity, honors compatible requested roles, falls back from incompatible roles,
requires team target filtering, and carries friendly-fire avoidance metadata
when `g_friendly_fire_scale` enables team damage.

CTF policy supplies the same attack/defense/midfield match shape for autonomous
role consumption. Existing target-specific CTF helpers still own enemy-flag,
own-flag-return, carrier-support, dropped-flag, and base-return target policy.

Item-role policy splits pickups by role:

- CTF objectives become objective-role items.
- Powerups and techs become powerup-control resources, with deny/reserve hints
  in team modes.
- Weapons and ammo become attack/midfield weapon-control items, or defender
  team resources.
- Health, armor, and utility become self stack in FFA/attack roles or team
  resources for defender/midfield roles.

Friendly-fire policy reports whether a target is self, a teammate, or blocked
by a supplied friendly line-of-fire fact. It does not trace or suppress fire;
combat/brain owners can use the metadata before applying attack buttons.

## Status

`BotObjectiveStatus` now tracks match-policy evaluations/selections, FFA/TDM/CTF
selection buckets, attack/defense/midfield buckets, item-role selections,
friendly-fire avoidance recommendations, target blocks, and latest match/item
role/friendly-fire facts.

The existing brain status printers do not emit the new fields yet.

## Main-Thread Hooks

Recommended live consumption hooks, once `bot_brain.cpp` is available:

- In `BotBrain_BuildFrameCommand()`, after `Bot_CommandSampleActionDecision(bot)`
  and before `Bot_CommandBuildRouteRequest(&routeRequest)`, build/evaluate
  `BotObjectiveMatchPolicy` and feed its role/lane into the blackboard and
  route/item/combat arbitration.
- In `Bot_CommandBuildSmokeObjectiveRoute()`, replace the hard-coded
  `BotObjectiveRole::Attacker` request with match-policy role selection when
  mode `23` evolves from proof smoke into live team-objective behavior.
- In `Bot_BlackboardRecordTeamRole()`, add match-policy fields beside the
  existing assignment-backed `teamRole` fields if autonomous FFA/TDM roles need
  blackboard visibility before they become route assignments.
- In `BotBrain_PrintCompactObjectiveStatus()` and the detailed objective status
  block, surface the new `BotObjectiveStatus` match/item/friendly-fire counters
  after the parallel status owners settle.

## Validation

Command:

```powershell
meson compile -C builddir-win sgame_x86_64
```

Result: passed. `sgame_x86_64.dll` linked successfully. Ninja printed the
shared build-dir warning `premature end of file; recovering`.

Command:

```powershell
python tools\refresh_install.py --build-dir builddir-win
```

Result: passed. `.install/` was refreshed and `basew/pak0.pkz` was repackaged.

Command:

```powershell
python tools\bot_scenarios\test_run_bot_scenarios.py
```

Result: passed, `26` tests.
