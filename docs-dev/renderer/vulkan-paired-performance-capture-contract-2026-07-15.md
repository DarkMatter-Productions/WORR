# Paired Renderer Performance Capture Contract

Date: 2026-07-15

Task ID: `FR-01-T15`

Status: implemented collection gate and one environment-bound dense inline-BSP
CPU-submission budget. A shared full-frame GPU timing contract is implemented,
but representative-map and accepted GPU budgets remain open.

## Purpose

`tools/renderer_parity/analyze_renderer_perf.py` can now verify that Vulkan and
OpenGL telemetry logs are the exact artifacts from one declared scenario before
it evaluates a performance budget. This prevents a polished-looking mean/p95
ratio from silently comparing different maps, command scripts, driver runs, or
modified log files.

The analyzer remains non-interactive. It reads logs and a JSON manifest only;
it does not launch a client or server.

`tools/renderer_parity/run_renderer_perf_capture.py` is the compliant runtime
collector for the fixed-view smoke scenario. It launches both native renderers
through `win_headless 1`, hashes the full `exec` configuration tree and shared
launch profile, and rejects capture logs that name different active adapters.
Its first result is documented in
`vulkan-paired-fixed-view-telemetry-2026-07-15.md`; it is collection evidence
only, not a performance budget.

The runner's common profile and every existing non-DOF workload configuration
now explicitly disable the latched `r_dof` effect before workload setup. This
prevents an archived user setting from changing the measured work and is part
of the configuration hash. The current controlled audit and the status of the
historical dense-instance budget are documented in
`vulkan-paired-performance-nondof-contract-2026-07-16.md`.

For effect-specific capture, repeatable `--launch-cvar NAME=VALUE` overrides
replace the named common-profile value before renderer startup. The runner
rejects duplicate, malformed, multiline, and NUL-containing inputs;
canonicalises the effective map; includes it in the configuration hash; and
records it in the capture manifest. This makes latched effects such as
`r_dof=1` auditable rather than relying on a too-late config command.

## Capture manifest schema

The `--capture-manifest` JSON input uses schema version `1`:

```json
{
  "schema_version": 1,
  "scenario": {
    "id": "stable-scenario-name",
    "fixture_sha256": "<64 lowercase-or-uppercase hex characters>",
    "config_sha256": "<64 lowercase-or-uppercase hex characters>",
    "launch_cvars": {
      "r_dof": "1"
    }
  },
  "environment": {
    "hardware_id": "stable GPU/CPU/OS identifier",
    "driver": "display-driver identifier"
  },
  "vulkan": {
    "renderer": "vulkan",
    "log_sha256": "<SHA-256 of the exact VK_STATS log>"
  },
  "opengl": {
    "renderer": "opengl",
    "log_sha256": "<SHA-256 of the exact GL_STATS log>"
  }
}
```

The fixture hash covers the exact map/assets/demo used by both renderers. The
configuration hash covers their shared simulation, resolution, cvar profile,
warm-up, and capture command sequence. Renderer-specific selection belongs in
the two runs but must not change the scenario. The environment fields make a
driver or hardware change visible in retained evidence instead of treating it
as a continuation of an old baseline. `launch_cvars` is optional in schema
version `1`, but when present it records the canonical complete map used for
the capture rather than only the caller's overrides.

The analyzer validates all required fields and both telemetry file hashes. Its
JSON report retains the scenario and artifact hashes. `--budget` now requires
`--capture-manifest`, so a threshold cannot be reported as a reproducible
acceptance result without this provenance.

## Example

```text
python tools/renderer_parity/analyze_renderer_perf.py \
  --vulkan .tmp/renderer-perf/vulkan.log \
  --opengl .tmp/renderer-perf/opengl.log \
  --capture-manifest .tmp/renderer-perf/capture.json \
  --warmup 30 --min-samples 120 \
  --budget assets/renderer_parity/fr01_renderer_perf_budget.json \
  --json-output .tmp/renderer-perf/report.json
```

`fr01_renderer_perf_bmodel_instances_budget.json` is the first checked-in
budget. It is deliberately narrow: its capture contract pins the scenario,
fixture hash, configuration hash, adapter/CPU/OS identifier, and display
driver. It requires 100 valid-GPU samples for both renderers, holds the native
Vulkan dense-inline-BSP path to draw/upload and CPU mean/p95 ceilings, and
requires Vulkan CPU mean to remain no more than 0.8 times OpenGL for that same
captured workload. It does not make a cross-renderer GPU claim.

The optional `capture_contract` object is validated against the already
hash-checked capture manifest. The optional `vulkan_max` object supplies raw
Vulkan metric ceilings; `vulkan_over_opengl_max` retains ratio limits. This
prevents a different map, command profile, adapter, or driver from silently
passing a machine-specific budget. A future budget must cover valid GPU
samples and both mean/p95 metrics that it claims. GPU budgets must use only
the shared `gpu_frame_ms_*` metrics and set `require_gpu_frame_valid`; legacy
`gpu_ms` and phase totals are diagnostic only. See
`vulkan-opengl-full-frame-gpu-timing-2026-07-16.md`.

## Headless validation

```text
python -m unittest tools/renderer_parity/test_analyze_renderer_perf.py
```

The tests cover ordinary aggregation, manifest acceptance, and rejection of a
telemetry log changed after its manifest was made. No interactive client window
is launched.
