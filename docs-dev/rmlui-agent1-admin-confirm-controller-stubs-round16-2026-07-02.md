# RmlUi Agent 1 Admin and Confirmation Controller Stub Prep Round 16

Date: 2026-07-02

Tasks: `FR-09-T08`, `FR-09-T09`, `FR-07-T02`, `DV-03-T07`, and `DV-04-T02`.

## Summary

Prepared the admin and confirmation starter documents for the static
`controller_stub` metadata pass without changing shared route JSON in this
worker slice:

- `admin_menu`
- `admin_commands`
- `forfeit_confirm`
- `leave_match_confirm`

The current workspace metadata now records these four routes as
`controller_stub` in `assets/ui/rml/session/routes.json` and
`tools/ui_smoke/rmlui_manifest.json`; this worker did not edit those shared
JSON files.

## Static Hook Coverage

The intended controller categories are:

- `admin_menu`: `navigation`, `command_action`, and `condition_state`.
- `admin_commands`: `command_action`.
- `forfeit_confirm`: `command_action`.
- `leave_match_confirm`: `command_action`.

`admin_menu` now declares a static `data-route-target="tourney_replay_confirm"`
on the replay button, matching its `worr_tourney_replay_menu` route-opening
command. The existing admin-commands button already declares
`data-route-target="admin_commands"`, the actionable buttons already expose
`data-command`, and the replay gate already exposes
`data-visible-if="ui_admin_show_replay=1"`.

No `cvar_binding` claim is prepared for these four routes. Their visible
labels and status text are static or localization-key based; the only cvar
reference in this slice is the admin replay visibility gate, which belongs to
`condition_state`.

## Caveat

This is static controller-stub preparation only: no live admin controller, no
live confirmation controller, no live runtime, no runtime RmlUi open path, no
screenshot/layout parity evidence, and no user-visible RmlUi behavior are
claimed. The legacy menu fallback remains authoritative.
