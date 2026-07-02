# WORR Bot Release Acceptance

`run_bot_acceptance.py` is the executable M8 release-readiness dry run for the
bot feature. It ties together the checks that used to live as separate manual
steps:

- public bot cvar/command surface audit;
- first-party profile validation;
- `botfiles/bots.txt` min-player roster exposure;
- authored botfile package contract;
- staged `.install/basew` archive and loose botfile mirror;
- staged generated AAS files for the current reference maps;
- user documentation presence, including the public bot cvar/default reference
  and the supported bot chat event/cvar contract;
- multiplayer playtest plan coverage;
- multiplayer playtest triage category coverage;
- Duel/CTF play-depth release attachment tooling;
- Duel/CTF headless play-depth runner tooling, including first-party profile
  coverage checks;
- M3 multiplayer milestone gate tooling;
- bot perf per-run and repeated-soak variance budget validity; and
- scenario report evidence for the current implemented catalog, including the
  promoted campaign/movement-reference rows and accepted movement-reference audit.

Run:

```powershell
python tools\bot_release\run_bot_acceptance.py --install-dir .install --scenario-report .tmp\bot_scenarios\implemented_hazard_context.json --movement-reference-audit .tmp\bot_scenarios\movement_reference_gap_audit.json --format json --output .tmp\bot_release\bot_release_acceptance_keyed_path.json
python tools\bot_release\run_bot_acceptance.py --install-dir .install --scenario-report .tmp\bot_scenarios\implemented_hazard_context.json --movement-reference-audit .tmp\bot_scenarios\movement_reference_gap_audit.json
python -m unittest tools.bot_release.test_run_bot_acceptance
```

The default gate expects at least 114 implemented scenario rows and a clean
scenario summary. It also requires the focused post-promotion
`coop_campaign_interaction_matrix_base2`,
`coop_campaign_interaction_depth_base2`,
`coop_campaign_progression_chain_base2`,
`coop_campaign_progression_consumer_base2`,
`coop_campaign_post_interaction_base2`,
`coop_campaign_progression_carry_base2`,
`coop_campaign_keyed_path_train`, `movement_crouch_route`, and
`movement_hazard_context` rows, the staged `worr_crouch_ref.bsp` reference map,
an accepted movement-reference audit, working Duel/CTF play-depth attachment
tooling, working headless play-depth runner tooling, and working M3 gate
tooling. Use `--allow-missing-scenario-report` only for environments that have
not generated local scenario artifacts yet.
