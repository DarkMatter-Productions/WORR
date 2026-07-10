# RmlUi Agent 2 Event Inventory Round 13

Date: 2026-07-02

Tasks: `FR-09-T05`, `FR-09-T09`, `DV-03-T07`, `DV-04-T02`

## Summary

Round 13 Worker 2 added a static RmlUi event/action inventory smoke checker for
the route documents listed in `tools/ui_smoke/rmlui_manifest.json`.

The checker inventories:

- `data-event-click`
- `data-event-change`
- `data-command`
- `data-route-target`
- `data-action-type`
- `data-bind-command`
- `data-command-cvar`

It parses present route `.rml` documents with `ElementTree`, counts missing
documents, validates that tracked static attributes are not empty, and reports
counts by attribute, route IDs with interaction hooks, unique event tokens,
unique action tokens, unique route-target tokens, command tokens, bind-command
references, and command-cvar references.

## Live Inventory

Accepted live inventory from `python tools\ui_smoke\check_rmlui_event_inventory.py`:

- Routes known: `57`
- Documents checked: `present=57`, `missing=0`
- Total event/action refs: `465`
- `data-event-click`: `38`
- `data-event-change`: `0`
- `data-command`: `289`
- `data-route-target`: `52`
- `data-action-type`: `33`
- `data-bind-command`: `38`
- `data-command-cvar`: `15`
- Routes with event hooks: `57`
- Unique event tokens: `1`
- Unique action tokens: `2`
- Unique route-target tokens: `36`
- Unique command tokens: `70`
- Unique bind-command refs: `38`
- Unique command-cvar refs: `15`
- Malformed/empty event attributes: `0`

The only route-document `data-event-*` token currently present is
`keybind.capture`. The checker also includes `data-event-change` because it
exists in the reusable RmlUi component corpus, even though the current route
manifest documents do not declare it directly.

## Validation

Passed:

- `python tools\ui_smoke\check_rmlui_event_inventory.py`
- `python tools\ui_smoke\check_rmlui_event_inventory.py --format json`
- `python -m pytest tools\ui_smoke\test_check_rmlui_event_inventory.py`

Focused pytest result:

- `5 passed`

## Caveats

This is a static inventory guardrail only. It does not wire RmlUi event
dispatch, implement live command routing, validate route-target existence, or
inspect imported component documents. Progress-report integration is intentionally
left to the worker that owns `tools/ui_smoke/report_rmlui_progress.py`.
