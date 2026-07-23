# Vulkan Full-Screen Blend Visual Parity

Date: 2026-07-19

Task ID: `FR-02-T05`

## Outcome

Native Vulkan now has a retained, validation-backed visual gate for the
full-screen powerup/liquid screen blend and damage vignette. The test drives
the normal renderer-facing refdef fields through the debug cgame blend source;
it does not reuse an OpenGL renderer path.

## Contract

OpenGL renders `screen_blend` as a full-view alpha blend and `damage_blend`
as either another full-view blend or an eight-vertex smooth vignette. Native
Vulkan preserves that ordering in `VK_UI_DrawScreenBlend`: it queues the
screen layer, then the damage vignette, after scene/entity rendering and
before the regular HUD queue. The two backends use the same shared
`gl_damageblend_frac` value (`0.2`) for the ring width.

`cl_testblend 3` provides deterministic cgame-owned inputs for both layers:
screen blend `[1.0, 0.5, 0.25, 0.5]` and damage blend
`[0.25, 0.5, 0.7, 0.5]`. The fixture locks a 16 ms game timestep and uses a
static repository-owned bmodel map so independently launched headless
renderers cannot select different animation or simulation states.

## Retained gate

`assets/renderer_parity/fr02_screen_blend.cfg` and
`fr02_screen_blend_manifest.json` capture the entire 960x720 frame. The
strictness is intentionally bounded only by the observable blend-rounding
difference between the backends:

- maximum RGB difference: `1 / 2 / 1`;
- MAE: `0.901800 / 0.983333 / 0.292454` (limits `1.0 / 1.0 / 0.4`);
- no pixel exceeds RGB error two; and
- the non-vignetted screen-blend inner receiver has exactly 252,072 pixels in
  each backend at IoU `1.0`.

`tools/renderer_parity/test_screen_blend_fixture.py` locks the cvar/timing
setup, manifest limits, and native Vulkan/OpenGL source ownership.

The companion `fr02_screen_blend_lava` scene starts the normal `q2dm1` game
map, teleports the live player into its lava receiver, and captures the real
screen blend with no debug blend override. It runs twice under a 16 ms fixed
timestep with the same result: MAE `1.213372 / 0.938103 / 0.671026`, 85 pixels
over RGB error 24 (`0.012297%`), and a 383,047/382,803-pixel exact orange
`[214, 64, 0]` receiver mask at IoU `0.999363`. The bounded full-frame
difference comes from animated lava world pixels beneath the same stable
gameplay overlay.

## Verification

```text
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
python -m unittest discover -s tools/renderer_parity -p 'test_screen_blend_fixture.py'
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr02_screen_blend_manifest.json --run-root .tmp/renderer-parity/fr02-screen-blend-final --timeout 180 --vulkan-validation
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr02_screen_blend_lava_manifest.json --run-root .tmp/renderer-parity/fr02-screen-blend-lava-final --timeout 180 --vulkan-validation
```

The staged hidden-surface run completed with no process failure or Vulkan
validation diagnostic.

## Boundary

The two gates exercise both composition primitives and a real lava screen
blend. Underwater, powerup, and damage-event combinations remain broader
`FR-02-T05` scene coverage.
