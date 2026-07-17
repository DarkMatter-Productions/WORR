# Native Vulkan Static HDR Tone-Mapping Parity

Date: 2026-07-16

Task IDs: `FR-01-T12`, `FR-02-T13`

Status: static ACES tone mapping is implemented and validation-tested. This
is a deliberately bounded step; floating-point scene rendering and automatic
exposure remain open.

## OpenGL contract

When OpenGL's HDR framebuffer is active, its final post-process shader applies
the following sequence before colour correction, split toning, and LUT
grading:

1. apply the configured input gamma when it is above one;
2. multiply by exposure;
3. apply the fitted ACES curve and normalise it by the ACES-mapped white point;
4. apply the inverse output gamma.

The controls are `gl_hdr`, `gl_hdr_exposure` (`0..10`), `gl_hdr_white`
(`0.1..20`), and `gl_hdr_gamma` (`1..3`). Its automatic-exposure branch
requires a floating scene texture with a generated mip chain.

## Native implementation

Vulkan now exposes the matching archived Vulkan-only controls:

- `vk_hdr`, default `0`;
- `vk_hdr_exposure`, default `1.0`;
- `vk_hdr_white`, default `1.0`;
- `vk_hdr_gamma`, default `2.2`.

The final Vulkan post-process shader implements the same static sequence in
the same order, after bloom and before the established colour/LUT stages. It
is entirely native Vulkan and does not invoke, sample from, or redirect to
OpenGL.

The final push-constant block is already the guaranteed 128-byte limit, so
the four HDR controls are stored after the existing 51 `vec4` paired-Gaussian
weights in the frame-local, host-coherent post-process UBO. Bloom reads only
the preceding weights; the final shader reads the trailing `hdr` `vec4`.
This preserves the cached blur kernel, avoids an additional descriptor set,
buffer, allocation, pipeline, or post-process pass beyond the mandatory final
composite, and updates only 16 bytes per active frame slot. HDR identity
settings continue to avoid the scene copy and final composite completely.

## Scope boundary

The current Vulkan scene copy has the swapchain format, so it cannot preserve
values above the display range. Consequently this change implements and proves
the static output-transform contract for its current LDR scene input, but it
does **not** claim full HDR parity. `vk_hdr_auto_exposure` is intentionally not
provided: exposing it before a floating scene target and mip-reduced luminance
source would silently discard the range the feature is meant to measure.

`FR-02-T13` remains responsible for the native floating/linear scene target,
the renderer-neutral linear-output contract, and then automatic exposure.

## Runtime evidence

`assets/renderer_parity/fr01_hdr_static_manifest.json` contains a static
ACES fixture and its disabled control. Both run on the native headless capture
surface with bloom, DOF, CRT, colour correction, split toning, and LUT grading
disabled. The enabled fixture uses exposure `1.35`, white `1.15`, and gamma
`2.2`.

The validation-enabled capture at
`.tmp/renderer-parity/hdr-static-final` passed both fixed 50,000-pixel crops:

| Scene | Maximum RGB error | MAE | Locked colour / IoU |
| --- | ---: | ---: | --- |
| Static HDR ACES | `0 / 0 / 0` | `0 / 0 / 0` | `[18, 38, 96]`, `1.0` |
| HDR disabled control | `0 / 0 / 0` | `0 / 0 / 0` | `[24, 40, 72]`, `1.0` |

The distinct disabled-control colour proves the enabled contract is not an
identity pass. Vulkan validation reported no VUID or validation error.

## Performance evidence

The visual transform necessarily activates the existing scene-copy and final
composite when it was otherwise idle. It is therefore a measured feature cost,
not a performance-improvement claim. The paired headless 960x720 capture used
120 samples per renderer, discarded 20 warmup samples, and ran on Intel Iris
Xe Graphics (`31.0.101.5590`). Vulkan means from the remaining 100 samples:

| Mode | Complete GPU | GPU post | CPU | Draws | Post draws |
| --- | ---: | ---: | ---: | ---: | ---: |
| HDR disabled | `0.56243 ms` | `0.00152 ms` | `0.59185 ms` | `3` | `0` |
| Static HDR | `1.29180 ms` | `0.53717 ms` | `1.05602 ms` | `4` | `1` |
| Delta | `+0.72937 ms` | `+0.53565 ms` | `+0.46417 ms` | `+1` | `+1` |

The retained provenance roots are
`.tmp/renderer-parity/hdr-static-perf-off-full` and
`.tmp/renderer-parity/hdr-static-perf-on-full`. The result confirms that the
UBO design introduces no separate control pass; the expected copy/composite
cost remains the next target for the floating-scene architecture.

## Verification

```text
python tools/gen_vk_world_spv.py --validate
python -m unittest tools.renderer_parity.test_vulkan_color_correction_source
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/package_assets.py
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_hdr_static_manifest.json --run-root .tmp/renderer-parity/hdr-static-final --vulkan-validation --json-output .tmp/renderer-parity/hdr-static-final/summary.json
python tools/renderer_parity/run_renderer_perf_capture.py --install-dir .install --config renderer_parity/fr01_renderer_perf_hdr_static_off.cfg --fixture assets/renderer_parity/fr01_hdr_static_manifest.json --scenario-id fr01-hdr-static-off-full --run-root .tmp/renderer-parity/hdr-static-perf-off-full --hardware-id "Intel Iris Xe Graphics" --driver "31.0.101.5590" --vulkan-validation --min-samples 100 --timeout 120
python tools/renderer_parity/run_renderer_perf_capture.py --install-dir .install --config renderer_parity/fr01_renderer_perf_hdr_static.cfg --fixture assets/renderer_parity/fr01_hdr_static_manifest.json --scenario-id fr01-hdr-static-on-full --run-root .tmp/renderer-parity/hdr-static-perf-on-full --hardware-id "Intel Iris Xe Graphics" --driver "31.0.101.5590" --vulkan-validation --min-samples 100 --timeout 120
```

All launches use the capture runner's hidden/headless runtime with input
disabled.
