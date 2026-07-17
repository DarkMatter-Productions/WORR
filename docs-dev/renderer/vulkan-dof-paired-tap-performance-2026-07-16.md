# Native Vulkan DOF Paired-Tap Performance Pass

Date: 2026-07-16

Task IDs: `FR-01-T12`, `FR-01-T15`

Status: implemented and measured. The native Vulkan depth-of-field blur now
uses the same paired bilinear Gaussian-tap strategy as the OpenGL shader
generator. It retains the existing quarter-resolution target, shared sigma
equation, and four alternating blur passes, while reducing texture samples in
each Gaussian pass by approximately half.

## Change

`vk_bloom.frag` is the native Vulkan blur shader used by both bloom and
gameplay DOF. Its old Gaussian branch sampled each texel in the `[-radius,
radius]` kernel separately. OpenGL precomputes adjacent Gaussian weights and
combines two neighbours into one linear-filtered texture sample.

The Vulkan shader now performs that same pairing: it derives the two weights,
uses their relative weight as the sub-texel offset, and applies their sum to
the filtered sample. The unpaired endpoint is retained where the radius is
odd. This changes no renderer routing, no scene ownership, and no DOF pass
count.

The one-pass direct-to-swapchain DOF composite was also prototyped under
validation. Although it was visually correct, its depth-read-only render-pass
cost did not improve the measured workload on the reference adapter. It was
removed rather than adding a speculative fast path.

An additional Gaussian-weight recurrence removed most per-fragment `exp`
evaluations while retaining focused visual parity. The same active-DOF capture
instead regressed from `1.33777` to `1.68570 ms` complete GPU mean and from
`0.700` to `0.85570 ms` post-process mean. That variant was rejected; the
retained implementation is the directly evaluated paired-tap kernel documented
here.

A subsequent uniform-coefficient pass retains the paired-tap topology while
precomputing the pair data once per frame slot. It improves the active DOF post
phase to `0.67683 ms` and passes the complete eight-scene strict matrix; its
mixed complete-frame result is documented without being promoted to a budget in
`docs-dev/renderer/vulkan-blur-kernel-uniform-optimization-2026-07-16.md`.

## Reproducible active-DOF workload

`run_renderer_perf_capture.py` now accepts repeatable
`--launch-cvar NAME=VALUE` overrides. It validates, canonicalises, hashes, and
records the complete effective launch-cvar map in `capture.json`. This matters
because `r_dof` is latched: setting it only in a config after renderer startup
cannot make an active-DOF timing scenario trustworthy.

`fr01_renderer_perf_dof.cfg` follows the real gameplay inventory route used by
the visual fixture. It enables focus-distance/range DOF, keeps bloom, colour
correction, LUT grading, and CRT disabled in both backends, pauses the same
fixed bmodel view, and logs 120 telemetry frames. The authoritative capture
uses `--launch-cvar r_dof=1`; its command and configuration hash contain the
effective control map.

## Evidence

Both captures use 120 samples, discard the first 20, and run at 960x720 on
Intel Iris Xe, 13th Gen Intel Core i7-13700H, Windows 11 Home 10.0.26200,
driver `31.0.101.5590 (2024-06-10)`. Vulkan validation was enabled. The
before/after Vulkan runs use the same active-DOF fixture and launch profile.

| Native Vulkan metric | Direct taps | Paired taps | Change |
| --- | ---: | ---: | ---: |
| Complete GPU mean | 2.66942 ms | 1.33777 ms | -49.9% |
| Complete GPU p95 | 3.643 ms | 1.850 ms | -49.2% |
| Postprocess GPU mean | 1.760 ms | 0.700 ms | -60.2% |
| Postprocess GPU p95 | 2.610 ms | 0.960 ms | -63.2% |
| Native postprocess draws | 7 | 7 | unchanged by design |

The paired-tap provenance root is
`.tmp/renderer-parity/fr01-renderer-perf-dof-paired-tap`; the active-DOF
configuration hash is
`4b81865cfbda75c64c2994b2ad3189831a9fcbc7ac0c8146f91b23a941fdc9b9`.
CPU submission time was not promoted to a budget: the recorded work remains
dominated by the existing Vulkan scene-copy and render-pass setup, while this
change deliberately targets the measured GPU blur hot path.

The full eight-scene strict visual matrix at
`.tmp/renderer-parity/dof-paired-tap/report.json` passed after the shader
change. Across the four active scenes every 307,200-pixel crop remained at
zero pixels beyond RGB error one; all four `r_dof=0` controls remained
pixel-identical.

## Verification

```text
python tools/gen_vk_world_spv.py --validate
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/package_assets.py
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python -m unittest tools/renderer_parity/test_vulkan_bloom_source.py tools/renderer_parity/test_vulkan_dof_control_source.py tools/renderer_parity/test_run_renderer_perf_capture.py
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_dof_manifest.json --run-root .tmp/renderer-parity/dof-paired-tap --vulkan-validation
python tools/renderer_parity/run_renderer_perf_capture.py --install-dir .install --config renderer_parity/fr01_renderer_perf_dof.cfg --fixture assets/maps/worr_fr01_bmodel_first_frame.bsp --scenario-id fr01-bmodel-depth-aware-dof-telemetry --run-root .tmp/renderer-parity/fr01-renderer-perf-dof-paired-tap --hardware-id "GPU=<adapter>; CPU=<cpu>; OS=<version>" --driver "<driver>" --vulkan-validation --min-samples 100 --launch-cvar r_dof=1
```

Every runtime launch above is hidden/headless (`win_headless 1`) with client
input disabled.
