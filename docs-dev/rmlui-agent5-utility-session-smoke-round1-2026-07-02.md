# RmlUi Agent 5 Utility, Session, and Smoke Round 1

Date: 2026-07-02

Tasks: `FR-09-T07`, `FR-09-T08`, `FR-09-T09`, `FR-09-T10`,
`FR-07-T01`, `FR-07-T02`, `DV-03-T07`

Roadmap: `docs-dev/plans/rmlui-ui-migration-roadmap.md`

## Summary

This first Agent 5 round seeds RmlUi starter documents for the rich utility and
multiplayer/session lanes, and adds a lightweight manifest checker for early
document-load smoke coverage. The documents are scaffold-level only: they name
the expected data models, commands, and table/preview/session controller slots
without claiming live runtime parity yet.

## Changed Files

- `assets/ui/rml/utility/servers.rml`
- `assets/ui/rml/utility/demos.rml`
- `assets/ui/rml/utility/players.rml`
- `assets/ui/rml/multiplayer/multiplayer.rml`
- `assets/ui/rml/session/vote_menu.rml`
- `tools/ui_smoke/rmlui_manifest.json`
- `tools/ui_smoke/check_rmlui_manifest.py`
- `docs-dev/rmlui-agent5-utility-session-smoke-round1-2026-07-02.md`

## Route Coverage

Starter routes marked `required_now`:

- `servers` -> `assets/ui/rml/utility/servers.rml`
- `demos` -> `assets/ui/rml/utility/demos.rml`
- `players` -> `assets/ui/rml/utility/players.rml`
- `multiplayer` -> `assets/ui/rml/multiplayer/multiplayer.rml`
- `vote_menu` -> `assets/ui/rml/session/vote_menu.rml`

The manifest lists every Wave A, Wave B, and Wave C route from the roadmap with
an owning agent and expected document path. Missing pending routes are reported
by the checker but do not fail unless the route is marked `required_now`.

## Validation

- Added `tools/ui_smoke/check_rmlui_manifest.py`.
- Ran `python tools/ui_smoke/check_rmlui_manifest.py`.
- Result: pass. The checker reported `57` total routes, `5` `required_now`
  routes, `5/5` required documents present, `10` total present documents in
  the shared workspace, and `47` pending documents.
- Ran `python -m py_compile tools/ui_smoke/check_rmlui_manifest.py`.

## Handoff Notes

- Agent 3 should wire the eventual shared table/list and player-preview
  controller contracts to the placeholder attributes used by `servers`,
  `demos`, and `players`.
- Agent 4 can add shell/settings/single-player documents at the manifest paths
  already reserved for its Wave A/B ownership.
- Agent 5 follow-up should expand `session/` coverage from `vote_menu` into
  `callvote_*`, `mymap_*`, tournament, forfeit, replay, map selector, and
  match stats routes before Gate G3.
- Do not mark additional routes `required_now` until their starter document is
  committed or the checker will correctly fail.
