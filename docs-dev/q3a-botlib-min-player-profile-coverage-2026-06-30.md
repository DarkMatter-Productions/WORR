# Q3A BotLib Min-Player Profile Coverage

Date: 2026-06-30

Tasks: `FR-04-T04`, `FR-04-T06`, `FR-04-T13`, `FR-04-T15`, `FR-04-T16`, `DV-07-T06`

## Purpose

The M3 headless play-depth runner exposed a concrete autofill gap: the CTF
case filled to six bots, but the roster skipped `smoke` and duplicated earlier
profiles. That contradicted the current release contract, where
`botfiles/bots.txt`, release acceptance, and the generated playtest plan treat
`vanguard`, `vector`, `bulwark`, `relay`, and `smoke` as the first-party roster.

This round removes the remaining public-autofill special case for `smoke` and
adds profile-coverage checks to the M3 play-depth tooling.

## Implementation

- Removed the server-side `smoke` skip from `bot_select_autofill_profile()` in
  `src/server/main.c`.
- Kept explicit profile forcing through `bot_profile` unchanged.
- Extended `tools/bot_playtest/run_bot_playdepth_headless.py` so each case
  records expected profiles, observed profiles, missing expected profiles, and
  profile coverage status.
- The headless runner now fails a non-dry run when a case target is large
  enough to cover every expected profile but one is missing.
- Extended `tools/bot_playtest/build_bot_playdepth_evidence.py` so a case
  marked `pass` while missing required profile coverage fails the attachment.
- Updated release acceptance's synthetic headless proof and tests to exercise
  profile coverage.
- Updated user/operator docs so `smoke` is described as part of the bundled
  first-party rotation, not a validation-only profile.

## Validation

Commands run:

```powershell
python -m unittest discover -s tools\bot_playtest -p "test_*.py"
python -m unittest tools.bot_release.test_run_bot_acceptance tools.bot_scenarios.test_audit_movement_reference_gaps
meson compile -C builddir-win worr_ded_engine_x86_64 sgame_x86_64
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
python tools\bot_playtest\run_bot_playdepth_headless.py --plan .tmp\bot_playtest\bot_multiplayer_playtest.json --startup-wait 120 --run-wait 120 --timeout 90 --format text
python tools\bot_playtest\build_bot_playdepth_evidence.py --plan .tmp\bot_playtest\bot_multiplayer_playtest.json --notes .tmp\bot_playtest\headless\20260630T200649Z\bot_multiplayer_playtest_headless_notes.json --format text
python tools\bot_playtest\check_m3_multiplayer_gate.py --scenario-report .tmp\bot_scenarios\implemented_hazard_context.json --playdepth-evidence .tmp\bot_playtest\bot_duel_ctf_playdepth_evidence.json --format text
```

Results:

- Playtest unit suite: 27 tests passed.
- Release/audit unit suite: 21 tests passed.
- Server/game build passed. Ninja reported the existing `.ninja_log` recovery
  warning while continuing successfully.
- `.install` was refreshed with the rebuilt Windows dedicated runtime and
  staged `basew` assets.
- The refreshed headless run passed both required cases and wrote
  `.tmp\bot_playtest\headless\20260630T200649Z\bot_playdepth_headless_runs.json`.
- CTF roster coverage now includes `bulwark`, `relay`, `smoke`, `vanguard`,
  and `vector`.
- M3 still reports `pending` because both required play-depth notes remain
  awaiting visual review.

## Follow-Up

Use `.tmp\bot_playtest\headless\20260630T200649Z\bot_multiplayer_playtest_headless_notes.json`
as the starting point for the visual Duel/CTF review. Once the operator marks
the required cases as `pass`, rebuild the play-depth evidence and rerun the M3
gate.
