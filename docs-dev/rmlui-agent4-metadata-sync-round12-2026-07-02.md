# RmlUi Round 12 Agent 4 Metadata Sync Check

Date: 2026-07-02

Task IDs: `FR-09-T09`, `FR-09-T10`, `DV-03-T07`, `DV-07-T04`

## Scope

Agent 4 added a deterministic smoke checker that compares discovered feature
route metadata under `assets/ui/rml/*/routes.json` with the central RmlUi smoke
manifest at `tools/ui_smoke/rmlui_manifest.json`.

Owned changes:

- `tools/ui_smoke/check_rmlui_metadata_sync.py`
- `tools/ui_smoke/test_check_rmlui_metadata_sync.py`
- `docs-dev/rmlui-agent4-metadata-sync-round12-2026-07-02.md`

No route manifests, progress reports, roadmap documents, renderer code, runtime
bridge code, or dependency/build files were changed in this worker lane.

## Checker Coverage

The checker validates that:

- every route ID declared by feature route metadata exists in the central smoke
  manifest;
- feature metadata documents match the central manifest after normalizing
  feature documents against `assets/ui/rml`;
- `migration_phase` values match when both the central route and feature route
  declare one;
- `required_now` remains central-manifest-only and is not required in feature
  metadata;
- duplicate route IDs across discovered feature route metadata files are hard
  errors;
- central routes without feature metadata are reported as drift, but only fail
  when the central route is in an advanced phase: `controller_stub`,
  `runtime_stub`, `parity_pending`, or `parity_ready`.

Text and JSON output both include central route count, metadata file count,
metadata route count, matched route count, central routes without feature
metadata, advanced missing-metadata routes, unknown metadata routes, phase
mismatches, document mismatches, duplicate route IDs, and errors.

## Live Repository Result

The live checker currently reports a sync failure caused by one pre-existing
strict-rule drift:

- central routes: 57
- metadata files: 3
- metadata routes: 32
- matched routes: 31
- central routes without feature metadata: 26
- advanced central routes without feature metadata: 0
- unknown metadata routes: 1
- phase mismatches: 0
- document mismatches: 0
- duplicate route IDs: 0

The failing route is `core.runtime_smoke`, declared in
`assets/ui/rml/core/routes.json` but not present in
`tools/ui_smoke/rmlui_manifest.json`.

This worker did not fix the drift because route manifests are outside the
Worker 4 ownership scope. The intended follow-up is for the owning route
metadata/manifest lane to either add `core.runtime_smoke` to the central smoke
manifest or document and implement an explicit checker exception for bootstrap
runtime routes.

## Validation

Run before handoff:

```text
python tools\ui_smoke\check_rmlui_metadata_sync.py
python tools\ui_smoke\check_rmlui_metadata_sync.py --format json
python -m pytest tools\ui_smoke\test_check_rmlui_metadata_sync.py
git diff --check -- tools\ui_smoke\check_rmlui_metadata_sync.py tools\ui_smoke\test_check_rmlui_metadata_sync.py docs-dev\rmlui-agent4-metadata-sync-round12-2026-07-02.md
```

Observed results:

- `python -m pytest tools\ui_smoke\test_check_rmlui_metadata_sync.py` passed
  with 7 tests.
- `python tools\ui_smoke\check_rmlui_metadata_sync.py` ran and failed on the
  live `core.runtime_smoke` metadata-vs-central drift described above.
- `python tools\ui_smoke\check_rmlui_metadata_sync.py --format json` ran and
  reported `ok: false` with the same single strict-rule error.
- `git diff --check -- owned files` passed.
