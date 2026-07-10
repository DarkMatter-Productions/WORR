# RmlUi Agent 2 Document ID Inventory Round 14

Date: 2026-07-02

Tasks: `FR-09-T04`, `FR-09-T05`, `FR-09-T08`, `FR-09-T09`, `FR-07-T01`, `FR-07-T02`, `DV-03-T07`, `DV-04-T02`

## Summary

Round 14 Worker 2 added a static document identity inventory checker for the RmlUi migration route set. The checker reads the central smoke manifest plus discovered `assets/ui/rml/*/routes.json` metadata, parses each central route document, and verifies the `<body>` identity surface.

The checker validates:

- A `<body>` element exists in every central route document.
- The `<body>` has a non-empty `id`.
- A present `data-route-id` matches the central route ID.
- Feature metadata `document_id` matches the document `<body id>` when feature metadata exists.
- Duplicate body IDs, missing documents, malformed RML, and identity mismatches fail the check.

## Live Counts

Accepted live inventory from `python tools\ui_smoke\check_rmlui_document_id_inventory.py`:

- Routes known: 57.
- Route metadata files: 5.
- Route metadata entries: 58.
- Documents checked: 57 present, 0 missing.
- Body IDs: 57.
- Unique body IDs: 57.
- Metadata document IDs matched to central routes: 57.
- Matched metadata/body document IDs: 57.
- Mismatched metadata/body document IDs: 0.
- Missing body IDs: 0.
- Route-id mismatches: 0.
- Duplicate body IDs: 0.
- Malformed documents: 0.

The extra metadata entry is the existing support route `core.runtime_smoke`; it is discovered in route metadata but is not part of the central 57-route smoke manifest.

## Validation

Commands run:

```powershell
python tools\ui_smoke\check_rmlui_document_id_inventory.py
python tools\ui_smoke\check_rmlui_document_id_inventory.py --format json
python -m pytest tools\ui_smoke\test_check_rmlui_document_id_inventory.py
```

Results:

- Text checker passed with the live counts above.
- JSON checker passed and reported `ok: true`.
- Focused pytest coverage passed: 6 tests.

## Caveats

This is a static inventory and guardrail only. It does not create live RmlUi controller bindings, runtime navigation, renderer coverage, input handling, screenshots, parity evidence, or legacy JSON removal. It only proves that central route documents currently expose stable body/document identity metadata for later runtime and parity work.
