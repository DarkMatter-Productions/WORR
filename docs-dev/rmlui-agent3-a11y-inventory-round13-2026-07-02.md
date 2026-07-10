# RmlUi Agent 3 Accessibility Inventory Round 13

Date: 2026-07-02

Tasks: `FR-09-T04`, `FR-09-T09`, `DV-03-T07`, and `DV-07-T04`.

## Summary

Round 13 Worker 3 added a static accessibility/localization inventory checker
for the RmlUi migration smoke suite. The checker scans every present RML route
document listed by `tools/ui_smoke/rmlui_manifest.json`, inventories common
localization and accessibility attributes, and fails on empty hook values or
non-integer `tabindex` values.

The inventory complements the existing semantics checker. It records static
coverage facts for localization and accessibility hooks, but it does not claim
runtime localization resolution, screen-reader behavior, focus traversal, or
layout parity.

## Implemented

- Added `tools/ui_smoke/check_rmlui_a11y_inventory.py`.
- Added `tools/ui_smoke/test_check_rmlui_a11y_inventory.py`.
- Inventoried `data-l10n`, `data-l10n-key`, `data-loc`, `data-loc-key`,
  `data-localization-key`, `data-localisation-key`, `aria-label`,
  `aria-labelledby`, `aria-describedby`, `role`, `tabindex`, and `accesskey`.
- Included `data-loc-key` because the current RML corpus already uses that
  nearby localization spelling.
- Added text and `--format json` output.
- Reported route/document counts, hook counts by attribute, routes with hooks,
  unique localization keys, unique roles, malformed hooks, route references,
  and errors.

## Current Inventory

The accepted repository scan for this worker slice reported:

- Route documents checked: `57`.
- Missing route documents: `0`.
- Total accessibility/localization hook refs: `8`.
- Routes with hooks: `3`.
- Unique localization keys: `6`.
- Unique roles: `0`.
- Malformed/empty hooks: `0`.

All current refs are `data-loc-key` localization hooks. No static ARIA, `role`,
`tabindex`, or `accesskey` hooks are present in the corpus yet.

## Validation

Required worker validation:

- `python tools\ui_smoke\check_rmlui_a11y_inventory.py`
- `python tools\ui_smoke\check_rmlui_a11y_inventory.py --format json`
- `python -m pytest tools\ui_smoke\test_check_rmlui_a11y_inventory.py`
- `git diff --check -- tools\ui_smoke\check_rmlui_a11y_inventory.py tools\ui_smoke\test_check_rmlui_a11y_inventory.py docs-dev\rmlui-agent3-a11y-inventory-round13-2026-07-02.md`

## Caveats

- This is a static inventory guardrail only.
- It does not validate localization key existence in string tables.
- It does not validate ARIA role compatibility or IDREF target existence.
- It has not been integrated into `report_rmlui_progress.py`; that file was
  intentionally left untouched for the worker owning progress integration.
