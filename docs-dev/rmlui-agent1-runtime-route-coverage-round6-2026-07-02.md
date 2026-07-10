# RmlUi Agent 1 Runtime Route Coverage Round 6

Date: 2026-07-02

Owner lane: Agent 1, client runtime scaffold

Task IDs: `FR-09-T02`, `FR-09-T03`, `FR-09-T09`, `DV-03-T07`, `DV-04-T02`

## Summary

This round expands the dependency-free client RmlUi runtime probe registry from
the initial Round 5 shell/smoke subset to the full smoke-manifest document set.
The static `ui_rml_routes` registry now covers all 57 route IDs tracked by
`tools/ui_smoke/rmlui_manifest.json`, plus the internal
`core.runtime_smoke` document used by the runtime smoke path.

This is route/document probe coverage only. It does not link RmlUi, parse RML,
implement rendering, implement controller parity, or transfer menu authority
from the legacy UI. `UI_Rml_OpenMenu` continues to return `false` so the legacy
UI remains authoritative.

## Changed Files

- `src/client/ui_rml/ui_rml.cpp`
  - Expands `ui_rml_routes` to 58 registered entries:
    - 57 smoke-manifest routes from Waves A, B, and C.
    - `core.runtime_smoke` for dependency-free runtime smoke verification.
  - Stores all document paths relative to `ui_rml_asset_root`, for example
    `settings/video.rml` instead of `assets/ui/rml/settings/video.rml`.
  - Preserves the existing `ui_rml_probe [route_id]` command behavior:
    - With no argument, probes every registered route in registry order.
    - With a route ID, probes only that route.
    - Completion offers every registered route ID.
- `docs-dev/rmlui-agent1-runtime-route-coverage-round6-2026-07-02.md`
  - Records the scope, task mapping, validation, and caveats for this route
    coverage expansion.

## Route Coverage

The manifest-backed route coverage now includes:

- Shell/settings/single-player routes owned by the shell/settings migration
  lane, including `main`, `game`, `options`, `video`, `downloads`,
  `download_status`, `singleplayer`, `startserver`, save/load, and related
  settings documents.
- Utility and multiplayer/session routes owned by the rich tools and session
  validation lanes, including address book, key binding, server/demo/player
  lists, multiplayer, vote/admin/session flows, tournament flows, map selector,
  and match stats documents.
- The non-manifest `core.runtime_smoke` document for runtime smoke probing.

The registry intentionally mirrors runtime document paths under `ui/rml` rather
than repository source paths under `assets/ui/rml`.

## Task Mapping

`FR-09-T02`: Keeps the client runtime scaffold dependency-free while widening
the route/document boundary that a future RmlUi integration will use.

`FR-09-T03`: Expands runtime bootstrap probing from a tiny route subset to the
full smoke-manifest route set without creating an RmlUi runtime.

`FR-09-T09`: Gives packaged-asset and loose-file smoke checks complete route
coverage for the tracked RmlUi migration documents.

`DV-03-T07`: Aligns the runtime probe surface with the current smoke manifest
so documentation, smoke tooling, and engine probes refer to the same route IDs.

`DV-04-T02`: Keeps the route registry centralized in `src/client/ui_rml/`
instead of scattering document availability checks through legacy menu code.

## Validation

- Manifest comparison check passed:
  - Manifest routes: `57`.
  - Registered routes: `58`.
  - Missing manifest route IDs: `0`.
  - Unexpected extra route IDs: `0`.
  - `core.runtime_smoke`: present.
- `ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj -v`
  - Result: passed.
  - Ninja also emitted `warning: premature end of file; recovering`, but the
    command exited successfully.
- `git diff --check -- src/client/ui_rml/ui_rml.cpp docs-dev/rmlui-agent1-runtime-route-coverage-round6-2026-07-02.md`
  - Result: passed with no output.

## Caveats

- This round does not add RmlUi as a dependency.
- This round does not add rendering, input dispatch, document parsing, style
  loading, controller bindings, or parity checks.
- This round does not change `q2proto/`.
- This round does not touch renderer code; Vulkan paths remain untouched.
