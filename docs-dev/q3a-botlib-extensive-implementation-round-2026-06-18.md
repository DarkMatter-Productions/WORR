# Q3A BotLib Extensive Implementation Round Summary - 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T03`, `FR-04-T04`, `FR-04-T13`,
`FR-04-T14`, `FR-04-T15`, `FR-04-T16`, `DV-03-T05`, `DV-05-T02`,
`DV-05-T03`, `DV-05-T05`, `DV-07-T06`, `DV-08-T05`.

## Scope

This round promoted the remaining default bot scenario placeholders into
source-backed smoke gates, refreshed Q3/Gladiator-style botfile script parity,
updated the harness documentation, refreshed `.install`, and reconciled the
project plan/roadmap with local validation evidence.

No new upstream Q3A or Gladiator source/text was imported. The runtime/tooling
changes are WORR-native and use the existing Q3A/BotLib adapter surfaces.

## Landed Lanes

- Scenario promotion: `aim_fairness_policy_integration`,
  `item_timer_fairness_signals`, `trace_checked_corner_cutting`,
  `ffa_tdm_match_readiness`, and `coop_match_readiness` are implemented rows.
- Server proof modes: mode `24` enables aim/fairness proof, mode `25` enables
  item-timer proof, and mode `26` enables FFA/TDM match-readiness proof.
- Brain/status proof: `bot_brain.*` now emits dedicated proof fields for aim
  policy, item timer policy, and match readiness while preserving existing
  action/objective/nav/source-counter status markers.
- Harness gates: `tools/bot_scenarios/run_bot_scenarios.py` now hard-checks the
  promoted rows through semantic marker metrics instead of optional discovery
  only.
- Botfile script parity: first-party `assets/botfiles/scripts/*_s.c` companions
  gained one named tactical routine each while keeping the existing conservative
  script grammar.
- Documentation: the scenario README, canonical Q3A BotLib plan, SWOT roadmap,
  closeout note, and credit/status ledger now record the 15-scenario pass and
  remaining evidence gaps.
- Packaging: `.install` was refreshed from `builddir-win`, `pak0.pkz` was
  rebuilt, and the mirrored/package botfile payload validation passed.

## Validation Ledger

- `python tools\bot_scenarios\test_run_bot_scenarios.py`: passed, 29 tests.
- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`: passed.
- `meson compile -C builddir-win`: passed after a longer rerun.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`: passed.
- Focused promotion run:
  `.tmp/bot_scenarios/promotion-round.json`, 5 passed, 0 failed, 0 timed out,
  0 errored, 0 pending.
- Full implemented suite:
  `.tmp/bot_scenarios/implemented-latest.json`, 15 passed, 0 failed,
  0 timed out, 0 errored, 0 pending, `overall=pass`.
- Completion snapshot after reconciliation:
  607/744 total checklist items complete (81.6%) and 607/732 phase checklist
  items complete (82.9%).

## Outstanding Work

- Replace smoke-level combat/aim proofs with less scripted live behavior that
  uses profile-fed reaction, error, noise, and target memory across normal play.
- Consume item respawn/timer policy in broader live item selection, not only the
  deterministic observed-pickup proof.
- Turn FFA/TDM/CTF readiness and role helpers into durable autonomous team
  behavior, and expand coop beyond readiness/status proof.
- Expand reference-map coverage beyond the currently available `mm-rage` subset.
- Capture fresh long-soak CPU/perf baselines with the current source-counter
  fields and budget files.
- Add CI/platform breadth for the scenario/build/package gates.
