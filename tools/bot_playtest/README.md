# WORR Bot Multiplayer Playtest Generator

`generate_bot_playtest.py` writes a repeatable operator checklist for the
current multiplayer bot behavior gates. It covers FFA, Duel, TDM, and CTF with
canonical `bot_` cvars and Q3-style bot commands.

`triage_bot_playtest.py` reads the generated checklist plus operator notes and
groups observed failures into scenario-candidate categories.

`build_bot_playdepth_evidence.py` turns the Duel and CTF notes into a compact
release attachment after the play-depth pass is recorded.

`run_bot_playdepth_headless.py` starts the required Duel and CTF cases on the
dedicated server, captures stdout/stderr plus `botlist` roster output, and
writes prefilled notes that remain pending until visual review is complete. It
also checks expected first-party profile coverage, so a CTF run that fills the
server but skips a required profile is treated as machine-evidence failure.

`check_m3_multiplayer_gate.py` combines the required automated M3 scenario
baseline with the Duel/CTF play-depth attachment and reports whether M3 is
passed, pending, or failed.

Run:

```powershell
python tools\bot_playtest\generate_bot_playtest.py --output-dir .tmp\bot_playtest
python tools\bot_playtest\generate_bot_playtest.py --output-dir .install\basew
python tools\bot_playtest\triage_bot_playtest.py --plan .tmp\bot_playtest\bot_multiplayer_playtest.json --notes .tmp\bot_playtest\bot_multiplayer_playtest_notes_template.json
python tools\bot_playtest\run_bot_playdepth_headless.py --plan .tmp\bot_playtest\bot_multiplayer_playtest.json
python tools\bot_playtest\build_bot_playdepth_evidence.py --plan .tmp\bot_playtest\bot_multiplayer_playtest.json --notes .tmp\bot_playtest\bot_multiplayer_playtest_notes_template.json
python tools\bot_playtest\check_m3_multiplayer_gate.py --scenario-report .tmp\bot_scenarios\implemented_hazard_context.json --playdepth-evidence .tmp\bot_playtest\bot_duel_ctf_playdepth_evidence.json
python -m unittest discover -s tools\bot_playtest -p "test_*.py"
```

The default output under `.tmp\bot_playtest` is best for evidence and review.
When validating a staged build, generate into `.install\basew` so the produced
`bot_playtest_*.cfg` files can be launched from `.install` with `+exec`.

Generated artifacts:

- `bot_multiplayer_playtest.md`: human checklist with observations and failure
  signals.
- `bot_multiplayer_playtest.json`: machine-readable release evidence.
- `bot_multiplayer_playtest_notes_template.json`: operator notes template for
  recording pass/fail outcomes and failure signals.
- `bot_multiplayer_playtest_triage.*`: triage output after notes are processed.
- `headless\<stamp>\bot_playdepth_headless_runs.*`: dedicated-server command,
  stdout/stderr, bot roster capture, and expected profile coverage for the
  required play-depth cases.
- `headless\<stamp>\bot_multiplayer_playtest_headless_notes.json`: prefilled
  operator notes from the headless run. Outcomes intentionally stay pending.
- `bot_duel_ctf_playdepth_evidence.*`: release attachment for the required
  Duel and CTF play-depth cases.
- `bot_m3_multiplayer_gate.*`: M3 milestone gate result combining automated
  scenario proof and play-depth evidence.
- `bot_playtest_*.cfg`: per-mode setup configs.

The generator intentionally resets all mode-specific bot policy cvars before
enabling the cvars for a case. That keeps FFA, Duel, TDM, and CTF observations
independent when they are run in sequence.
