# Native Vulkan Bloom Emission-Replay Elision

Date: 2026-07-16

Task IDs: `FR-01-T12`, `FR-01-T15`

Status: implemented and validation-tested. Bloom stays entirely native Vulkan,
but frames with no authored world or entity emission now avoid the unnecessary
full-resolution emission extraction render pass.

## Change

The normal Vulkan bloom path preserves OpenGL's material-emission contract by
replaying only world glow companions and eligible entity glowmaps, shells, and
rims into a separate emission attachment. Before this change, every active
bloom frame opened and cleared that attachment even when both replay lists were
empty.

World and entity submission now expose conservative native source queries. The
post-process state uses their result to select one of two existing Vulkan
paths:

- Frames with authored emission retain the extraction attachment, replay pass,
  and emission-aware prefilter descriptor.
- Frames without a contributing batch bind the completed scene as the safe
  descriptor fallback, disable only the emission sample in the prefilter, and
  skip the extraction pass entirely. Scene-threshold bloom, downsampled blur,
  and final composition continue unchanged.

The eligibility query includes direct depth-hack sources and the same material
families and pipeline availability checks as the recording path. Per-frame
descriptor state includes the emission-present bit, so a reused frame slot
cannot retain the wrong fallback descriptor. No OpenGL function, timer, or
renderer path participates.

## Evidence

The active translucent-shell visual receiver at
`.tmp/renderer-parity/bloom-emission-gated-smoke` still passes under Vulkan
validation: maximum RGB difference `6 / 10 / 5`, zero pixels beyond the
existing error-16 limit, core IoU `1.0`, and halo IoU `0.98375`.

`fr01_renderer_perf_bloom_no_emission.cfg` adds a reproducible active-bloom
receiver with the same matched bloom controls but no authored source. Its
validation-enabled 960x720 Intel Iris Xe capture is retained at
`.tmp/renderer-parity/fr01-renderer-perf-bloom-no-emission` with configuration
SHA-256
`f0e8c68ba0ffc7143e5863f14fd078b03dfb2e8de1a616cdc90c7c3b203fe641`.
After trimming 20 warm-up frames it records 100 valid Vulkan GPU samples:
`1.72621 ms` complete GPU mean, `0.91299 ms` scene mean, `0.76335 ms` post
mean, and seven total draws (two world, one entity, four post-process).

That draw distribution confirms the active no-emission route retains the
prefilter/two blur/final sequence while avoiding extraction submission. This
single new lane is a correctness and diagnostic baseline, not a
cross-renderer budget; the measured next comparison is an explicit forced
replay A/B on the same no-emission fixture.

## Verification

```text
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/package_assets.py
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python -m unittest tools.renderer_parity.test_vulkan_bloom_source tools.renderer_parity.test_run_renderer_perf_capture
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --executable .install/worr_x86_64.exe --manifest assets/renderer_parity/fr01_model_shell_bloom_emission_manifest.json --run-root .tmp/renderer-parity/bloom-emission-gated-smoke --vulkan-validation
python tools/renderer_parity/run_renderer_perf_capture.py --install-dir .install --executable .install/worr_x86_64.exe --config renderer_parity/fr01_renderer_perf_bloom_no_emission.cfg --fixture assets/maps/worr_fr01_bmodel_first_frame.bsp --scenario-id fr01-bmodel-active-bloom-no-emission-telemetry --run-root .tmp/renderer-parity/fr01-renderer-perf-bloom-no-emission --hardware-id "GPU=<adapter>; CPU=<cpu>; OS=<version>" --driver "<driver>" --vulkan-validation --min-samples 100
```

All runtime launches use `win_headless 1` with input disabled.
