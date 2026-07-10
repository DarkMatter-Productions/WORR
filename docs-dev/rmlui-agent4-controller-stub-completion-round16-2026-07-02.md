# RmlUi Round 16 Agent 4 Controller-Stub Completion Validation

Date: 2026-07-02

Task IDs: `FR-09-T05`, `FR-09-T08`, `FR-09-T09`

## Scope

Agent 4 added a static smoke checker for the Round 16 route-phase completion
handoff. The checker reports whether tracked RmlUi smoke routes have advanced
out of `migration_phase: "starter"` into `controller_stub`, `runtime_stub`,
or later parity phases.

Owned changes:

- `tools/ui_smoke/check_rmlui_controller_stub_completion.py`
- `tools/ui_smoke/test_check_rmlui_controller_stub_completion.py`
- `docs-dev/rmlui-agent4-controller-stub-completion-round16-2026-07-02.md`

## Checker Behavior

The checker reads only `tools/ui_smoke/rmlui_manifest.json` by default. It does
not inspect RML documents, route metadata contracts, C++ menu entrypoints,
runtime files, renderer state, or screenshot evidence.

It reports:

- total tracked routes;
- phase counts for `starter`, `controller_stub`, `runtime_stub`,
  `parity_pending`, and `parity_ready`;
- advanced routes and percentage;
- non-runtime advanced routes and percentage;
- remaining starter route IDs;
- JSON/text output and validation errors.

Default mode is intentionally informational for starter leftovers. This keeps
the checker usable before the Round 16 coordinator metadata lands, when the
accepted Round 15 baseline was `starter=12`, `controller_stub=42`,
`runtime_stub=3`, and `advanced=45/57`.

`--require-complete-controller-stubs` promotes remaining starter route IDs to a
nonzero failure. This strict mode is the final gate once the coordinator moves
the last twelve routes into `controller_stub`.

## Current Live Result

The shared worktree had already received the Round 16 coordinator phase update
when validation ran. Current live counts are:

- `starter=0`
- `controller_stub=54`
- `runtime_stub=3`
- `parity_pending=0`
- `parity_ready=0`
- `advanced=57/57`
- `non-runtime advanced=54/54`
- remaining starter route IDs: none

## Validation

Run before handoff:

```text
python -m pytest tools/ui_smoke/test_check_rmlui_controller_stub_completion.py
```

Result: `5 passed in 0.17s`.

```text
python tools/ui_smoke/check_rmlui_controller_stub_completion.py
```

Result: exit `0`; `starter=0`, `controller_stub=54`, `runtime_stub=3`,
`advanced routes=57/57 (100.0%)`, `non-runtime routes advanced=54/54
(100.0%)`, starter route IDs `none`.

```text
python tools/ui_smoke/check_rmlui_controller_stub_completion.py --format json
```

Result: exit `0`; JSON `ok=true`, `strict=false`, `total_routes=57`,
`starter_routes.count=0`, `controller_stub_routes=54`,
`runtime_stub_routes=3`, `advanced_routes.count=57`.

```text
python tools/ui_smoke/check_rmlui_controller_stub_completion.py --require-complete-controller-stubs
```

Result: exit `0`; strict completion passed with `starter=0`,
`controller_stub=54`, `runtime_stub=3`, and starter route IDs `none`.

```text
python tools/ui_smoke/check_rmlui_controller_stub_completion.py --require-complete-controller-stubs --format json
```

Result: exit `0`; JSON `ok=true`, `strict=true`, `starter_routes.count=0`,
`advanced_routes.count=57`, and `errors=[]`.

## Non-Goals

This checker does not prove live controller execution, runtime RmlUi
navigation, native renderer output, screenshots, or parity readiness. It is a
route-phase completion gate only. Vulkan renderer work remains native-only and
unrelated to this validation.
