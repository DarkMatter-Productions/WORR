# Capability-Correct Native Vulkan Shadow Samplers

Date: 2026-07-19

Task ID: `FR-02-T14`

## Outcome

Native Vulkan shadow resources now select their sampling format and sampler
state from the physical-device format capabilities. The former path always
created linear depth, comparison, and moment samplers, even when the selected
format only advertised ordinary sampled-image support. That can be invalid on
legal Vulkan implementations that lack linear filtering for a depth or
floating-point color format.

The Vulkan path remains entirely native. No shadow resource, sample, fallback,
or validation route uses OpenGL.

## Capability policy

Depth and moment format selection now has two passes:

1. Choose the first usable format that also supports
   `VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT`.
2. If no candidate supports linear filtering, choose a usable sampled format
   and create legal nearest-filter samplers instead.

The depth sampler and hardware-comparison sampler share the realized depth
filter. The moment sampler independently uses its selected moment-format
filter. Linear mip generation and mip filtering remain enabled only when the
moment format supports the existing linear blit requirement; the fallback
uses nearest filtering and a one-level moment image.

This retains the established OpenGL-equivalent linear path on capable devices,
while preserving functional depth/PCF/PCSS and VSM/EVSM shadow rendering on
constrained devices without an unsupported sampler configuration.

`r_shadow_dump` exposes the realized choice through
`depth-filter=linear|nearest` and `moment-filter=linear|nearest`, so a runtime
report provides direct adapter-specific evidence.

## Guardrails

`tools/renderer_parity/test_vulkan_shadow_sampler_capability_source.py`
locks the linear-preferred format search, legal fallback, independent depth
and moment sampler filters, conditional moment mip mode, runtime report, and
the native-only renderer boundary.

## Verification

The native Vulkan DLL built and the staged runtime/package were refreshed:

```text
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py --assets-dir assets --install-dir .install
```

The current staged and built Vulkan DLLs have the same SHA-256 hash. The
535-file asset package was rebuilt.

The full renderer source suite passed:

```text
python -m unittest discover -s tools/renderer_parity -p "test_*.py"
# 344 tests passed
```

This headless EVSM flashlight run completed with validation enabled:

```text
python tools/shadowmapping_repro_smoke.py --install-dir .install \
  --run-root .tmp/shadow-sampler-capability \
  --renderer vulkan --scene flashlight-owner --filter evsm --wait 180 \
  --vulkan-validation
```

The Intel Iris Xe runtime dump reports native moment-array materialization at
512px with 10 mips and `depth-filter=linear moment-filter=linear`. Its process
log has no `VUID`, validation error, renderer error, or fatal finding.

The paired alpha-tested shadow scene also remains exact after the sampler
selection change:

```text
python tools/renderer_parity/run_capture_matrix.py --install-dir .install \
  --manifest assets/renderer_parity/fr01_alpha_shadow_manifest.json \
  --run-root .tmp/renderer-parity/fr01-alpha-shadow-sampler-capability \
  --timeout 180 --vulkan-validation
```

All 235,200 pixels in its 560x420 crop have zero RGB error between OpenGL and
Vulkan, with no Vulkan validation findings.

## Remaining FR-02-T14 work

This closes the capability-correct sampler slice. The sampler descriptor ABI
now also carries five native resolution-pool entries; see
`vulkan-shadow-resolution-pools-2026-07-19.md`. Delayed shrinking and
transactional replacement are documented in their respective 2026-07-19 logs.
