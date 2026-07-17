# Vulkan Bloom Debug Preview Parity

Date: 2026-07-16

Task ID: `FR-01-T15`

Status: paired visual diagnostic gate passed.

## Outcome

`rend_vk` now exposes the cheat-protected `vk_showbloom` cvar. When native
Vulkan bloom is active, it presents the completed level-zero blurred bloom
image directly. This is the native Vulkan equivalent of OpenGL's
`gl_showbloom`: it is a diagnostic presentation mode, not a fallback route
through `rend_gl`.

The normal bloom build still runs, including threshold/knee/firefly handling,
authored-emission extraction, and the configured blur passes. If CRT is
enabled, its later native CRT presentation pass still receives the preview,
which matches the OpenGL ordering. If bloom is inactive or unsupported,
`vk_showbloom` has no effect and the regular final composition is retained.

## Native implementation

The native bloom ping image has two views: `view` covers its complete mip
chain for normal multi-level composition; `render_view` names level zero only.
When `vk_showbloom` is active, the final triple-image descriptor uses
`bloom_ping.render_view` as its scene input. The frame-local descriptor state
also records the preview bit, so toggling the cvar cannot reuse a descriptor
that still names the regular scene source.

`vk_postprocess.frag` reserves a final-pass `bloom_final.w` value below
`-1.5` for the direct preview. That branch samples the scene input and returns
before water warp, bloom composition, HDR/auto exposure, colour correction,
split toning, or LUT grading. The existing `-1.0` sentinel remains dedicated
to the one-pixel auto-exposure reduction. This keeps both contracts explicit
without expanding the already-full final push-constant block.

## Deterministic evidence

The repository-owned fixture is:

- config: `assets/renderer_parity/fr01_bloom_debug.cfg`
- manifest: `assets/renderer_parity/fr01_bloom_debug_manifest.json`
- map: `worr_fr01_glowmap`
- capture: full 960 by 720 frame (691,200 pixels)

The configuration enables cheats before the renderer-specific controls, uses
matched OpenGL/Vulkan bloom settings (one iteration, four-times downscale,
zero threshold/knee, intensity and saturations one), and disables DOF, CRT,
and colour correction. It then enables both `gl_showbloom` and
`vk_showbloom`.

The validation-enabled paired headless run at
`.tmp/renderer-parity/fr01-bloom-debug-tight` passed with:

```text
maximum RGB error:                 1 / 1 / 1
mean absolute RGB error:           0.000804 / 0.000736 / 0.001541
pixels over zero error:            1,938 / 691,200 (0.280382%)
Vulkan validation/process errors:  none
```

The manifest guards this result with a zero per-pixel threshold, maximum mean
RGB error `[0.001, 0.001, 0.002]`, and at most `0.5%` differing pixels. This
is intentionally tight enough to detect a final-transform, wrong-mip, or
wrong-source regression while allowing the observed one-level raster rounding.

## Verification

```powershell
python tools/gen_vk_world_spv.py --validate
ninja -C builddir-win worr_opengl_x86_64.dll worr_vulkan_x86_64.dll
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py --assets-dir assets --install-dir .install --base-game basew
python -m unittest tools.renderer_parity.test_vulkan_bloom_source tools.renderer_parity.test_bloom_debug_fixture
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_bloom_debug_manifest.json --run-root .tmp/renderer-parity/fr01-bloom-debug-tight --vulkan-validation
```

## Remaining boundary

This closes the bloom-buffer diagnostic presentation path only. It does not
claim complete bloom-material coverage or renderer-wide visual parity, and it
does not resolve the still-open `gl_showorigins` baseline failure or missing
native Vulkan `showtris` mode under `FR-01-T15`.
