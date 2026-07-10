# RmlUi Round 11 Agent 3: Phase Consistency Validation

Date: 2026-07-02

Task IDs: FR-09-T09, FR-09-T10, DV-03-T07, DV-07-T04

## Scope

Added a static phase-consistency checker for the RmlUi migration roadmap. The checker cross-validates migration phase claims from `tools/ui_smoke/rmlui_manifest.json` against route metadata, parity-checklist evidence, and guarded runtime menu-entrypoint evidence from `src/client/ui_rml/ui_rml.cpp`.

This is consistency validation only. It does not add new runtime evidence, renderer evidence, screenshot/layout parity, live controller behavior, or legacy UI removal.

## Files

- `tools/ui_smoke/check_rmlui_phase_consistency.py`
- `tools/ui_smoke/test_check_rmlui_phase_consistency.py`

## Validation Rules

- `controller_stub` routes must have matching route metadata with non-empty `controller_contracts`.
- `runtime_stub` routes must have the same metadata evidence and must either be returned by `UI_Rml_RouteForMenu` or remain in the documented guarded runtime route allowlist.
- `parity_ready` routes require complete parity-manifest evidence for every modeled evidence category.
- When the parity manifest models `controller_bindings`, complete controller-binding coverage must match the current `controller_stub` plus `runtime_stub` route set.
- The checker reports phase counts, metadata-backed advanced routes, runtime-stub entrypoint coverage, parity-ready routes, missing evidence counts, and errors.

## Current Live Result

The live repository check passed with:

- Routes checked: 57
- Route metadata files checked: 3
- Phase counts: `starter=30`, `controller_stub=24`, `runtime_stub=3`, `parity_pending=0`, `parity_ready=0`
- Metadata-backed advanced routes: 27
- Runtime menu-mapped routes: 3
- Parity-ready routes: 0
- Missing evidence: none

## Validation

- `python -m pytest tools/ui_smoke/test_check_rmlui_phase_consistency.py` passed with 5 tests.
- `python tools/ui_smoke/check_rmlui_phase_consistency.py` passed.
- `python tools/ui_smoke/check_rmlui_phase_consistency.py --format json` passed with `ok: true`.
- `git diff --check -- tools/ui_smoke/check_rmlui_phase_consistency.py tools/ui_smoke/test_check_rmlui_phase_consistency.py docs-dev/rmlui-agent3-phase-consistency-round11-2026-07-02.md` passed after final review.
