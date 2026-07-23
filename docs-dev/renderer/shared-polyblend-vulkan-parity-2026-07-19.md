# Shared Polyblend Control: Vulkan/OpenGL Parity

Date: 2026-07-19

Task ID: `FR-02-T05`

Status: implemented and validated in the native nightly screen-blend lane.

## Problem

OpenGL exposed `gl_polyblend` as its traditional runtime control for the
screen blend and damage-vignette pass. Native Vulkan always queued the same
blends after its 3D/post-process work. Default output matched because the GL
control defaults to enabled, but setting `gl_polyblend 0` had no Vulkan
equivalent and therefore produced a backend-specific visual result.

## Shared control and native Vulkan path

`r_polyblend` is now the archived, renderer-neutral control, defaulting to
`1`.

- OpenGL keeps `gl_polyblend` as a synchronized legacy alias. Startup chooses
  a modified legacy value only when the shared archived value has not been
  modified; afterwards either spelling clamps and updates both to `0` or `1`.
- Native Vulkan reads `r_polyblend` directly. When disabled, `R_RenderFrame`
  does not call `VK_UI_DrawScreenBlend`, so no native UI blend or vignette
  geometry is queued.
- When enabled, Vulkan retains its existing native UI path and still applies
  `gl_damageblend_frac` to the damage-vignette shape, matching the retained
  OpenGL compatibility control.

The Vulkan path does not call, link, or redirect to OpenGL.

## Headless visual evidence

The expanded `fr02_screen_blend_manifest.json` staged matrix runs the
deterministic `cl_testblend 3` screen-plus-damage receiver in both states at
960x720 with validation enabled.

| State | Compared pixels | Vulkan/OpenGL result |
| --- | ---: | --- |
| `r_polyblend 1` | 691,200 | maximum RGB `1 / 2 / 1`, MAE `0.901800 / 0.983333 / 0.292454`, zero pixels over RGB error 2; exact 252,072-pixel blend mask at IoU `1.0` |
| `r_polyblend 0` | 691,200 | RGB-exact, zero pixels over threshold |

The direct enabled-versus-disabled capture check confirms that the control is
live in both renderers: all 691,200 pixels change in each backend. Maximum
per-channel deltas are `115 / 65 / 51` for OpenGL and `116 / 66 / 52` for
Vulkan. The disabled state is therefore not merely an equal-but-still-active
pair of backends.

All four hidden captures exited cleanly with no validation, device-lost, or
fatal-error text. They run with `win_headless 1`, disabled input, and isolated
runtime directories.

## Verification

```powershell
python -m unittest tools.renderer_parity.test_shared_polyblend_control_source
meson compile -C builddir-win worr_opengl_x86_64 worr_vulkan_x86_64
python tools/refresh_install.py --build-dir builddir-win --assets-dir assets --install-dir .install --base-game basew --archive-name pak0.pkz
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr02_screen_blend_manifest.json --run-root .tmp/renderer-parity/fr02-screen-blend-shared-control --vulkan-validation --timeout 180
python tools/renderer_parity/run_capture_matrix.py --manifest assets/renderer_parity/fr02_screen_blend_manifest.json --run-root .tmp/renderer-parity/fr02-screen-blend-shared-control --compare-only
```

## Scope

This closes the missing polyblend toggle parity. Broader gameplay/HUD and
nightly map-sequence coverage remain tracked by `FR-02-T05`.
