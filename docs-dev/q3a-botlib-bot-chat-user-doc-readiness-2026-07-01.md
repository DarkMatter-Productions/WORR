# Q3A BotLib Bot Chat User Docs Readiness

Date: 2026-07-01

Tasks: `FR-04-T07`, `FR-04-T16`, `DV-07-T06`

## Summary

This round completes the M5/M8 bot chat user-facing documentation readiness
slice. The previous match-result outcome work made the live chat event family
stable enough to document as supported behavior. This change adds a dedicated
operator-facing bot chat guide and extends release acceptance so the guide must
continue to cover every public chat cvar and supported live chat event.

The work is WORR-native documentation/tooling and does not import upstream code.

## User-Facing Docs

Added `docs-user/bot-chat.md` with:

- Quick setup for `bot_allow_chat`, `bot_chat_live_events`,
  `bot_chat_min_interval_ms`, and `bot_chat_team_only`.
- The supported live event taxonomy:
  `spawn`, `team_ready`, `route_ready`, `item_taken`, `item_denied`,
  `enemy_sighted`, `objective_changed`, `flag_state`, `low_health`, `blocked`,
  and `victory_defeat`.
- Practical local-test, public-server, and silent-bot configurations.
- Profile chat personality notes for `quiet`, `direct`, `taunting`, `helpful`,
  and `steady`.

Updated `docs-user/bots.md`, `docs-user/bot-cvars.md`, and
`docs-user/bot-profiles.md` so they link to the new guide and no longer describe
live chat as only a proof-line path.

## Release Gate

`tools/bot_release/run_bot_acceptance.py` now requires:

- `docs-user/bot-chat.md` to exist.
- The bot chat guide to mention every public bot chat cvar.
- The bot chat guide to mention every supported live event name.
- `docs-user/bots.md`, `docs-user/bot-cvars.md`, and
  `docs-user/bot-profiles.md` to link to `bot-chat.md`.

`tools/bot_release/test_run_bot_acceptance.py` adds focused unit coverage for
the passing and failing versions of that contract.

## Validation

- `python -m unittest tools.bot_release.test_run_bot_acceptance`
- `python tools\bot_surface\audit_bot_surface.py --format json --output .tmp\bot_surface\public_bot_surface_chat_docs_audit.json`

Follow-up release acceptance was run after the docs and roadmap updates:

- `python tools\bot_release\run_bot_acceptance.py --install-dir .install --scenario-report .tmp\bot_scenarios\implemented_hazard_context.json --movement-reference-audit .tmp\bot_scenarios\movement_reference_gap_audit.json --format json --output .tmp\bot_release\bot_release_acceptance_chat_docs.json`
