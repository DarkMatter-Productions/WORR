# FR-10-T12 ProBall held-throw current-world ballistic acceptance

Date: 2026-07-16  
Project task: `FR-10-T12`

## Scope

Policy `23`, `WORR_REWIND_WEAPON_PROBALL_HELD_THROW_BALLISTIC_SPAWN_FORWARD`,
accepts only the first clear, bounded gravity advance of a fresh ProBall after
the ordinary Chainfist-held throw is released. It is not a historical ball
trace or a general ProBall compensation policy.

The authorization is created only at the real `ClientThink` command boundary
when all of the following are true:

- the match is ProBall;
- the live selected weapon is Chainfist;
- the live carrier still has `IT_BALL`; and
- the command is the actual attack-to-release edge.

The retained authorization names `IT_BALL`, is bound to the live shooter
identity, accepted command, map epoch, policy, and one launch, and is consumed
by the normal Chainfist-held `Ball_Launch` path only. A later attack prime
invalidates an unconsumed release authorization. Direct item-use `Ball_Pass`
explicitly passes `false` for the release-bound path and therefore remains
ordinary current-world behavior.

The resolver consumes at most
`sg_lag_compensation_projectile_forward_ms` (100 ms in the gate) through
current-world gravity steps. Any present-world contact rejects the advance,
leaving the normal `NewToss` update as the sole owner of collision response,
placement, and subsequent interaction. It does not query historical players,
historical movers, or a rewound world.

## Explicitly not claimed

This policy does not compensate or decide:

- possession, ball pickup, carrier ownership, pass cooldowns, drops, or
  direct item-use passes;
- ball touch, re-grab delay, attraction, bounce, out-of-bounds, lava/slime,
  idle reset, or ball lifetime;
- goals, scoring, assists, teams, round/match state, or reset behavior; or
- any contact, target, damage, effect, or cooperative/fairness/load result.

All of those remain production-owned current-world behavior pending their own
independent policy and acceptance evidence.

## Acceptance fixture

The canonical gate starts the staged dedicated server as ProBall
(`g_gametype 17`) and uses two independent real UDP clients, joined to the
normal Red and Blue teams. The fixture may stage possession for the isolated
shooter after the clients are admitted, but it never creates a pickup, pass,
throw, touch, trajectory, goal, scoring result, or command authority. It
places Chainfist at the normal pre-fire animation boundary; the real delayed
`+attack` command enters the normal held-throw state and a later real
`-attack` releases it.

The runner uses isolated runtime homes and a dedicated server. Both clients
start with `win_headless=1`, `in_enable=0`, `in_grab=0`, and `s_enable=0`;
there is no renderer-visible test, stdin input, physical device
initialization, or mouse capture. Its Windows job cleanup records termination
of the server, shooter, and target every time.

The one-repeat diagnostic result in
`.tmp/networking/canonical-proball-throw-debug-runtime.json` recorded policy
23, a clear authenticated 56 ms/56 ms current-world advance, no historical
impact, no damage, unchanged live geometry, and termination of all three
processes.

The staged three-repeat result in
`.tmp/networking/canonical-proball-throw-install-runtime.json` recorded:

| Repeat | Authenticated age | Advanced age | Historical impact | Blocked | Server/shooter/target terminated |
| --- | ---: | ---: | ---: | ---: | --- |
| 1 | 72 ms | 72 ms | 0 | 0 | yes/yes/yes |
| 2 | 88 ms | 88 ms | 0 | 0 | yes/yes/yes |
| 3 | 56 ms | 56 ms | 0 | 0 | yes/yes/yes |

## Verification

- `python tools/networking/test_run_canonical_rail_damage_runtime_gate.py`
  — 38/38 passed.
- `python tools/networking/test_lag_compensation_canonical_rail_contract.py`
  — 44/44 passed.
- `ninja -C builddir-win sgame_x86_64.dll` — passed.
- `python tools/refresh_install.py --build-dir builddir-win` — passed and
  refreshed `.install/` (445 packed source assets).
- The one-repeat diagnostic and three-repeat staged headless runtime gates
  described above — passed.
- `meson test -C builddir-win --print-errorlogs` — 139/139 passed.

This narrows one fresh-release launch seam only. `FR-10-T12` remains
incomplete.
