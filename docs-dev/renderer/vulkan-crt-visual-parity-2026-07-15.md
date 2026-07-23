# Vulkan CRT Visual Parity

Date: 2026-07-15

Task ID: `FR-01-T12`

Status: the fixed shared-control CRT presentation case has retained
OpenGL-versus-native-Vulkan visual evidence.

## Presentation-coordinate correction

The native `vk_crt.frag` implementation already matched the OpenGL CRT
reconstruction, but its alternating scanline factor evaluated at the opposite
output-row phase. Vulkan uses a negative-height final-pass viewport to retain
the engine's top-left presentation convention; without compensation,
OpenGL's bright and dark scanlines were vertically interchanged in the final
screenshot.

`crt_scanline_mod` offsets by one complete expanded source scanline before its
row-parity calculation. At native resolution this is the original one-output-
pixel correction; at reduced resolution it preserves both the first row and
bright/dark parity of every expanded scanline block. It affects only the
alternating CRT scanline phase; reconstruction, mask, gamma conversion, and
the unfiltered UI overlay ordering remain native and unchanged. The shader is
regenerated into `vk_crt_spv.h`, and the source regression locks that phase
expression.

## Retained paired scene

`renderer_parity/fr01_crt.cfg` starts from the fixed first-frame view and
disables bloom, DOF, colour correction, LUT, and split toning. It then enables
the shared CRT controls with bright boost `1.5`, hard pixel and scanline
falloff `-8`, linear-gamma reconstruction, and no shadow mask.

`fr01_crt_manifest.json` exact-compares the 250 by 200 background crop at
`(100, 100)`. The final Vulkan-validation run has zero RGB error over all
50,000 pixels. Its 25,000-pixel dark-scanline mask `[14, 28, 55]` has equal
backend counts and IoU `1.0`, so row-phase regressions cannot silently pass.

The same manifest also drives `fr01_crt_masked.cfg` with shadow-mask layout
`2`. It exact-compares the same 50,000-pixel crop and locks an 8,400-pixel
green-mask bright-scanline region `[24, 74, 78]` at IoU `1.0`. This exercises
the native RGB mask branch as well as the corrected alternating scanline phase.

```text
python tools/renderer_parity/run_capture_matrix.py --install-dir .install \
  --manifest assets/renderer_parity/fr01_crt_manifest.json \
  --run-root .tmp/renderer-parity/fr01-crt-final \
  --vulkan-validation
```

The command uses the repository's hidden native-surface runner with isolated
homes and disabled input/sound; no interactive client is opened.

## Remaining scope

This validates the unmasked fixed-scale CRT presentation case and shadow-mask
layout `2`. Other shadow-mask layouts, UI-boundary captures, gameplay DOF,
HDR, bloom, and resolution scaling remain open `FR-01-T12` coverage.
