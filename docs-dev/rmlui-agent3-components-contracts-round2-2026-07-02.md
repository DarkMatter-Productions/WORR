# RmlUi Agent 3 Components and Contracts Round 2

Date: 2026-07-02

Tasks: `FR-09-T05`, `FR-09-T07`, `FR-03-T08`, `DV-04-T02`

## Scope

This round adds second-pass shared RmlUi component contracts for keybind
capture and image-grid selectors, plus a small hand-authored route manifest
contract. It intentionally does not add C++ controllers. The assets are
mock-first handoff files for future runtime, content, and validation lanes.

## Changed Files

- `assets/ui/rml/common/components/keybind.rml`
- `assets/ui/rml/common/components/keybind.rcss`
- `assets/ui/rml/common/components/image-grid.rml`
- `assets/ui/rml/common/components/image-grid.rcss`
- `assets/ui/rml/contracts/input-keybind.mock.json`
- `assets/ui/rml/contracts/image-grid.mock.json`
- `assets/ui/rml/contracts/route-contract.schema.json`
- `docs-dev/rmlui-agent3-components-contracts-round2-2026-07-02.md`

## Implementation Notes

- Added `worr-keybind-list` and `worr-keybind-row` templates with stable
  capture, clear, conflict, reset, and command dispatch hook names.
- Added keybind mock data for movement and combat bindings, including capture
  policy, conflict policy, bridge ownership, and future controller event names.
- Added `worr-image-grid-selector` and `worr-image-grid-item` templates for
  imagevalues-style selectors such as crosshair selection.
- Added image-grid mock data with selected item state, cvar ownership, image
  provider ownership, and apply/reset command events.
- Added `route-contract.schema.json` as a dependency-free JSON-schema-like
  contract for route manifest files. It is intentionally permissive through
  `additionalProperties` so Agent 1, Agent 4, and Agent 5 route metadata can
  keep evolving without blocking on a validator package.

## Validation

- JSON parse validation passed with PowerShell `ConvertFrom-Json`:
  - `assets/ui/rml/contracts/input-keybind.mock.json`
  - `assets/ui/rml/contracts/image-grid.mock.json`
  - `assets/ui/rml/contracts/route-contract.schema.json`
- XML-ish parse validation passed with PowerShell `[xml]` parsing:
  - `assets/ui/rml/common/components/keybind.rml`
  - `assets/ui/rml/common/components/image-grid.rml`
- Route shape smoke validation passed against the new minimal route contract
  fields:
  - `assets/ui/rml/core/routes.json`: 1 route
  - `assets/ui/rml/shell/routes.json`: 23 routes
  - `tools/ui_smoke/rmlui_manifest.json`: 57 routes
- Whitespace validation passed with `git diff --check` for the owned files.
- Build validation was not run because this slice adds mock assets and
  hand-authored contracts only; no Meson/runtime/C++ integration was changed.

## Handoff Notes

- Agent 4 can consume the image-grid component for `crosshair` and any future
  imagevalues-style settings route.
- Agent 5 can consume the keybind component for `keys`, `legacykeys`, and
  weapon/input utility surfaces.
- Runtime/controller work should bind `keybind.capture_begin`,
  `keybind.clear_binding`, `keybind.accept_conflict`,
  `keybind.cancel_capture`, `image_grid.select`, `image_grid.reset`,
  `cvar.set`, and `command.dispatch`.
