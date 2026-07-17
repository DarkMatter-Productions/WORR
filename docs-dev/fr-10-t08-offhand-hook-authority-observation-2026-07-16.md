# FR-10-T08 off-hand Hook authority observation

Date: 2026-07-16  
Project task: `FR-10-T08`  
Related authority policy: `FR-10-T12` policy 24

## Purpose

The cgame immutable prediction-input range already preserves the complete
eight-bit user-command button field, including the native `BUTTON_HOOK` bit.
This increment makes the server-side action oracle express the corresponding
authority facts before a cgame feature may predict, reconcile, or suppress
off-hand Hook presentation.

## Observed facts

The command-scoped local-action observation state now carries:

- `WORR_LOCAL_ACTION_OBSERVATION_OFFHAND_HOOK_HELD` when the canonical
  command supplied the mapped `+hook` button; and
- `WORR_LOCAL_ACTION_OBSERVATION_OFFHAND_HOOK_ACTIVE` when the post-callback
  server grapple state is not `None`.

These facts are deliberately separate from `BUTTON_ATTACK` and the
selected-weapon firing state. The second flag names a real post-callback
lifecycle state only; it neither claims a policy-24 launch specifically nor
fabricates an outcome for a rejected action.

## Boundary

The bounded per-client observation record remains diagnostic only. It does not
change sgame authority, packet or snapshot data, cgame presentation, local
prediction, effect/audio suppression, or correction policy. Legacy `hook`
string-command behavior continues to be outside the mapped-command
prediction/rewind path.

The next eligible cgame work is a shadow-only adapter that consumes this
command identity plus authoritative observation records and demonstrates exact
state/event parity. It must remain non-presenting until a complete catalogue
rule, reconciliation, and duplicate-effect gate exists.

## Verification

- `local_action_observation_test.exe` rebuilt and
  `network-local-action-observation` passed.
- `sgame_x86_64.dll` rebuilt successfully.
- The refreshed Windows `.install` stage validated 16 runtime files, one
  dependency, and a `basew/pak0.pkz` containing 449 assets.

This increment did not launch an interactive game client. The standalone
networking test has no renderer or input initialization, and a post-run check
found no WORR/Q2 process. The project-wide automated-launch rule remains:
any game-process test must use `win_headless=1`, `in_enable=0`,
`in_grab=0`, disabled audio, isolated runtime data, and bounded process
cleanup.

## Status

This is an oracle extension, not full client-side prediction. `FR-10-T08` and
`FR-10-T12` remain incomplete, and roadmap completion totals do not change.
