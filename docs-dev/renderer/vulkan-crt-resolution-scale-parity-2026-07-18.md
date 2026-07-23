# Vulkan CRT Resolution-Scale Parity

Date: 2026-07-18

Task ID: `FR-01-T12`

## Result

Native Vulkan CRT now exactly matches OpenGL at fixed half and quarter scene
resolution. The same scale-aware path also exactly matches at the shared 10%
minimum for both fixed and forced-adaptive scaling. Each retained 250 by 200
capture compares all 50,000 pixels with zero RGB error under Vulkan validation,
including a 25,000-pixel dark-scanline block probe at IoU `1.0`.

## Native implementation

OpenGL first scales the scene into its full-resolution CRT target and applies
the CRT shader afterwards. Vulkan now follows that native ordering without an
OpenGL route:

- A scaled or float Vulkan scene lazily allocates the existing full-resolution
  presentation-copy image only while `r_crtmode` is active.
- The renderer retires submitted frame slots before creating or destroying that
  per-frame image and descriptor, so a CRT toggle cannot race an in-flight
  command buffer.
- The normal scaled LDR fast blit remains allocation-free when CRT is disabled.
- The Vulkan final composite writes the upscaled scene, copies it into the
  native presentation image, then runs the CRT pass before the sharp UI
  overlay.

The previous scaled path began the CRT presentation load pass without that
image on linear scenes. That produced a validation layout error and left the
CRT result unavailable. The pass is now descriptor-gated if a constrained
surface cannot provide a presentation copy.

The CRT shader also expresses its negative-viewport phase offset in expanded
source-scanline units (`phase = scale`). This keeps the bright/dark parity and
the first output row correct as one source row covers two or four output rows.

## Retained fixtures

- `assets/renderer_parity/fr01_resolution_scale_crt_half_manifest.json`
  enables fixed `0.5` width/height scaling.
- `assets/renderer_parity/fr01_resolution_scale_crt_quarter_manifest.json`
  enables fixed `0.25` width/height scaling.
- `assets/renderer_parity/fr01_resolution_scale_crt_tenth_manifest.json`
  enables the shared fixed `0.1` width/height minimum.
- `assets/renderer_parity/fr01_resolution_scale_adaptive_crt_tenth_manifest.json`
  forces the shared adaptive controller down to its `0.1` floor.

Run each fixture separately because they intentionally reuse the same minimal
CRT scene and screenshot name:

```text
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_resolution_scale_crt_half_manifest.json --run-root .tmp/renderer-parity/fr01-crt-half-final --vulkan-validation
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_resolution_scale_crt_quarter_manifest.json --run-root .tmp/renderer-parity/fr01-crt-quarter-final --vulkan-validation
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_resolution_scale_crt_tenth_manifest.json --run-root .tmp/renderer-parity/fr01-crt-tenth-final --vulkan-validation
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_resolution_scale_adaptive_crt_tenth_manifest.json --run-root .tmp/renderer-parity/fr01-crt-adaptive-tenth-final --vulkan-validation
```

The capture runner is hidden/headless, with isolated homes and disabled input
and sound.
