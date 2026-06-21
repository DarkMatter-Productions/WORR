# Q3A BotLib CTF Base Return Route Ownership

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds a default-off CTF base-return route-owner proof for bots that need to recover their own team flag from an enemy carrier. The new `sg_bot_ctf_base_return_route` bridge promotes smoke mode `39` as `ctf_base_return_route`, proving that live objective policy can select an enemy carrying the bot team's flag and drive route commands through the `OwnBaseReturn` lane as a `Returner`.

The work also hardens the shared CTF carrier smoke setup used by carrier-support and base-return proofs. Seeded flag carriers now must pass a player-solid occupancy check before `TeleportPlayer`, and flags are granted only after placement succeeds. That avoids unsafe CTF proof setup positions where a seeded carrier could immediately telefrag or be telefragged before final status capture.

## Implementation

- `src/game/sgame/bots/bot_objectives.*` now exposes `BotObjectives_AssignOwnFlagReturnObjective`. The selector prefers alive enemy players carrying the bot team's own flag, records `FlagCarrier` target-source metadata, and keeps world or dropped own-flag entities as a fallback path.
- `src/game/sgame/bots/bot_brain.cpp` adds `sg_bot_ctf_base_return_route`, smoke mode `39`, CTF readiness/seed preparation, base-return assignment validation, route-owner counters, and `ctf_base_return_route_*` / `last_ctf_base_return_route_*` status output.
- The shared CTF carrier-placement helper now rejects occupied routeable points with a zero-length `MASK_PLAYERSOLID` trace before teleporting the carrier.
- `src/server/main.c` reserves mode `39`, sets up four-bot CTF scenario cvars, emits the `ctf_base_return_route` begin marker, and resets the new cvar after smoke cleanup.
- `tools/bot_scenarios/run_bot_scenarios.py` and `tools/bot_scenarios/test_run_bot_scenarios.py` promote `ctf_base_return_route`, add the optional counter family, and gate the scenario on route-specific role, lane, target type, target source, carrier client, route command, and own-base-return policy evidence.

## Validation

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario ctf_carrier_support_route --timeout 120 --base-port 28140 --format text --json-out .tmp\bot_scenarios\ctf_carrier_support_route_report.json`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario ctf_base_return_route --timeout 120 --base-port 28150 --format text --json-out .tmp\bot_scenarios\ctf_base_return_route_report.json`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 120 --base-port 28200 --format text --json-out .tmp\bot_scenarios\latest_report.json`

The focused CTF carrier-support and base-return scenarios passed. The final implemented suite reported 31 passed, 0 failed, 0 timed out, 0 errored, and 0 pending.

## Follow-Up

This is still a proof lane rather than full autonomous CTF strategy. Broader follow-up work should make live CTF bots select, switch, and combine attacker, defender, returner, support, dropped-flag, carrier-support, and base-return roles without dedicated smoke cvars.
