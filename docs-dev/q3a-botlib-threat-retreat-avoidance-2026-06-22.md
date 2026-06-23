# Q3A BotLib Threat Retreat Avoidance

Date: 2026-06-22

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`

## Purpose

This round adds the next M2 combat/survival behavior slice: a low-health bot can
break contact from a live threat, suppress firing while the retreat route is
active, and re-engage after the short avoidance window expires. The behavior is
default-off and scenario-gated while the broader live combat loop is still being
promoted.

## Implementation

- Added `sg_bot_threat_retreat` as a default-off survival behavior gate in
  `src/game/sgame/bots/bot_brain.cpp`.
- Added `ThreatRetreat` as timed route-goal kind `8`, with status kind name
  `threat_retreat`.
- Added low-health threat source selection from the current enemy, recent
  damage source, or a fallback direction.
- Added conservative tuning for the proof slice: health threshold `35`, retreat
  distance `768`, route window `700` ms, and cooldown `2200` ms.
- Added blackboard/status counters for requests, source type, activations,
  refreshes, route requests/deferrals, expirations, invalid skips, attack
  suppressions, re-engagements, and last threat-retreat state.
- Suppressed attack while the threat-retreat timed route is active, then records
  post-retreat re-engagement once combat resumes.
- Added reserved smoke mode `72` in `src/server/main.c`, including begin-marker
  evidence for `threat_retreat=1`, two-bot FFA setup, low-health actor state,
  and no legacy `sg_bot_frame_command_smoke_combat=engage_enemy` dependency.
- Added the `threat_retreat_avoidance` scenario, raw reserved hints,
  `threat_retreat_route_counters` optional field family, parser coverage for
  supplemental status rows, and unit-test fixtures in `tools/bot_scenarios/`.
- Updated older timed-route owner marker gates so route-owner scenarios assert
  their policy-specific status fields instead of stale generic timed-route tail
  fields.

## Validation

- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios` passed 47
  tests.
- `meson compile -C builddir-win` passed; Ninja emitted the existing
  `premature end of file; recovering` warning and exited successfully.
- `.install/` was refreshed with the current Windows binaries and `basew`
  game DLLs after the build.
- Focused `threat_retreat_avoidance` validation passed from
  `.tmp\bot_scenarios\20260622T202608Z`.
- Neighboring combat survival regressions passed from
  `.tmp\bot_scenarios\20260622T202618Z`.
- Focused timed route-owner regression coverage passed for `ffa_roam_route`,
  `ffa_spawn_camp_avoidance`, `team_role_route`, `ctf_role_route`, and
  `coop_lead_advance` from `.tmp\bot_scenarios\20260622T203111Z`.
- The full implemented scenario run passed from
  `.tmp\bot_scenarios\20260622T203125Z` with `78` passed rows, `0` failed rows,
  `0` timeouts, `0` errors, and `0` pending rows.
- Catalog verification reports `78` implemented rows and `0` pending rows.

## Provenance

No Q3A, BSPC, Quake3e, baseq3a, Gladiator, or `q2proto/` source files were
imported or modified for this round. The changes are WORR-native behavior,
server smoke setup, scenario harness, tests, and documentation only.
