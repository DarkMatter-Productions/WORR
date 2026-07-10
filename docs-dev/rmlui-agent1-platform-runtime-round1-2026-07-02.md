# RmlUi Agent 1 Platform Runtime Round 1

Date: 2026-07-02

Owner lane: Agent 1, platform/runtime/packaging

Roadmap: `docs-dev/plans/rmlui-ui-migration-roadmap.md`

Task IDs: `FR-09-T01`, `FR-09-T02`, `FR-09-T03`

## Summary

This round adds a lightweight RmlUi source-asset contract starter only. It does
not add the C++ runtime, RmlUi dependency, renderer bridge, Meson wiring, or
`.install/` staging changes.

The new core asset island gives future runtime work one route to load and one
small metadata file to consume without needing shell/settings/multiplayer
content from the other lanes.

## Changed Files

- `assets/ui/rml/core/runtime_smoke.rml`
  - Adds a well-formed starter RML document for the smoke route
    `core.runtime_smoke`.
  - Uses stable IDs and `data-command` placeholders for future `ui.back` and
    `ui.close` command binding.
  - Requires no data model for first document-load validation.
- `assets/ui/rml/core/routes.json`
  - Adds a tiny route manifest with schema name `worr.rml.routes.v1`.
  - Records the route ID, document path, owner lane, task IDs, runtime
    expectations, placeholder command names, and empty data-model set.
- `docs-dev/rmlui-agent1-platform-runtime-round1-2026-07-02.md`
  - Records this implementation log and handoff notes.

## Contract Details

Route ID: `core.runtime_smoke`

Document path: `core/runtime_smoke.rml`

Document ID: `core-runtime-smoke`

Entry point tag: `runtime_smoke`

Placeholder commands:
- `ui.back`
- `ui.close`

Data models: none required for the first load smoke route.

Manifest paths are intended to be relative to the future RmlUi asset root. In
source form that root is `assets/ui/rml/`; after packaging it should correspond
to the staged runtime root, expected by the roadmap to become
`.install/basew/ui/rml/`.

## Task Mapping

`FR-09-T01`: This creates the first concrete source asset layout under
`assets/ui/rml/core/` and seeds the route namespace with
`core.runtime_smoke`. It also records initial ownership and command/data
contract metadata in a machine-readable file.

`FR-09-T02`: This does not wire the RmlUi dependency or Meson install rules yet.
It prepares a document path and manifest shape that a later packaging pass can
copy into `.install/basew/ui/rml/` once the dependency and staging decisions are
accepted.

`FR-09-T03`: This provides the first runtime bootstrap target. The manifest
marks the route as requiring a native renderer once the runtime exists, but no
renderer or input bridge is implemented here.

## Validation Performed

- Parsed `assets/ui/rml/core/runtime_smoke.rml` with PowerShell XML loading.
  Result: OK.
- Parsed `assets/ui/rml/core/routes.json` with `ConvertFrom-Json` and verified
  `core.runtime_smoke` points to an existing source document.
  Result: OK.
- Scanned the new core asset files for non-ASCII bytes.
  Result: OK.

No build was run because this round intentionally avoided build, dependency,
runtime, and install-staging files.

## Remaining Work

- Implement the client-owned RmlUi runtime and decide how it discovers
  `routes.json`.
- Add WORR-backed RmlUi system and file interfaces for time, logging,
  filesystem access, and localization lookup.
- Bind `data-command` placeholders such as `ui.back` and `ui.close` to the
  eventual menu navigation command dispatcher.
- Add Meson/dependency wiring and `.install/basew/ui/rml/` staging only after
  the `FR-09-T02` dependency strategy is accepted.
- Prove that the sample route draws through native OpenGL, Vulkan, and RTX/vkpt
  paths without redirecting Vulkan renderer work to OpenGL.
