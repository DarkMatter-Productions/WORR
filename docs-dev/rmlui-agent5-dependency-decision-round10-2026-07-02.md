# RmlUi Round 10 Worker 5: Dependency Decision Record

Date: 2026-07-02

Task IDs: `FR-09-T02`, `FR-09-T03`, `DV-06-T01`, `DV-03-T07`, `DV-07-T04`

## Scope

Added a development decision record for future first-class RmlUi dependency
integration:

- `docs-dev/rmlui-dependency-decision-record-2026-07-02.md`

This worker did not edit code, build files, route metadata, package tools, or
runtime tools.

## Status

The decision record is proposed, not implemented. It documents a candidate
Meson subproject/wrap integration strategy with a vendored-source fallback, but
keeps `FR-09-T02` open until an exact RmlUi source, license/provenance audit,
build integration, and staging refresh are accepted.

Legacy UI remains authoritative. The record explicitly says it does not add a
dependency, does not change build files, does not enable any runtime switch by
default, does not implement native renderer/input/file/font interfaces, does
not claim screenshots or parity, and does not remove legacy JSON.

## Checklist Coverage

The record covers:

- `DV-06-T01` license, provenance, version, local patch, transitive dependency,
  and update-policy audit requirements.
- `FR-09-T02` Meson subproject/wrap preference, vendored fallback, and
  packaging/staging proof using current `ui/rml` asset tools.
- `FR-09-T03` required system, file, input, font/text, route, and native
  renderer interfaces before Gate G1.
- Native renderer proof for OpenGL, Vulkan, and RTX/vkpt, with an explicit
  no Vulkan-to-OpenGL fallback rule.
- `DV-03-T07` validation commands for the existing RmlUi smoke, metadata,
  asset, staging, progress, and parity checkers.
- `DV-07-T04` parity/user-doc expectations, deferred until runtime behavior and
  evidence exist.

## Validation

Requested validation:

```text
rg -n "FR-09-T02|FR-09-T03|DV-06-T01|DV-03-T07|DV-07-T04|Non-Goals|No RmlUi dependency is added|No Meson|No runtime switch|No screenshot|No legacy JSON" docs-dev\rmlui-dependency-decision-record-2026-07-02.md docs-dev\rmlui-agent5-dependency-decision-round10-2026-07-02.md
git diff --check -- docs-dev/rmlui-dependency-decision-record-2026-07-02.md docs-dev/rmlui-agent5-dependency-decision-round10-2026-07-02.md
```

## Result

This is documentation-only planning evidence for Round 10 Worker 5. It does not
advance any route beyond the accepted Round 9 baseline and does not claim Gate
G1, G2, G3, or G4 progress.
