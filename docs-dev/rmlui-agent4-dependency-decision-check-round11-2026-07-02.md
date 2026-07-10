# RmlUi Round 11 Agent 4 Dependency Decision Check

Date: 2026-07-02

Task IDs: `DV-06-T01`, `FR-09-T02`, `FR-09-T03`, `DV-03-T07`, `DV-07-T04`

## Scope

Agent 4 added a deterministic smoke checker for the RmlUi dependency decision
record so later rounds cannot accidentally treat the planning decision as a
completed dependency integration.

Owned changes:

- `tools/ui_smoke/check_rmlui_dependency_decision.py`
- `tools/ui_smoke/test_check_rmlui_dependency_decision.py`
- `docs-dev/rmlui-agent4-dependency-decision-check-round11-2026-07-02.md`

## Checker Coverage

The checker validates that
`docs-dev/rmlui-dependency-decision-record-2026-07-02.md`:

- exists and references the required RmlUi dependency and validation task IDs;
- keeps the decision status explicitly `proposed, not implemented`;
- includes no-go evidence for Meson/build dependency, vendored source,
  runtime loader/switch, and source include/linkage work not being added yet;
- preserves native renderer obligations for OpenGL, Vulkan, and RTX/vkpt,
  including the no Vulkan-to-OpenGL fallback rule;
- names the Gate G1 interface areas for system, file, input, font/text, and
  runtime route/controller integration;
- includes static validation, asset staging, and runtime evidence requirements.

The tool supports text output and `--format json`. JSON output exposes the
boolean result groups, pass counts, discovered task IDs, status facts, and an
`errors` list for CI-friendly consumption.

## Validation

Run before handoff:

```text
python tools\ui_smoke\check_rmlui_dependency_decision.py
python tools\ui_smoke\check_rmlui_dependency_decision.py --format json
python -m pytest tools\ui_smoke\test_check_rmlui_dependency_decision.py
git diff --check
```

Expected current-record metrics:

- required task IDs: 5/5
- no-go wording: 4/4
- native renderer obligations: 4/4
- Gate G1 interfaces: 5/5
- validation evidence: 3/3
