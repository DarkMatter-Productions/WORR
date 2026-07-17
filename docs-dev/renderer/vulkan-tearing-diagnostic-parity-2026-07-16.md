# Vulkan Tearing Diagnostic Parity

Date: 2026-07-16

Task ID: `FR-01-T15`

Status: native functional diagnostic implemented; temporal screenshot phase is
not a direct cross-renderer pixel baseline.

## Outcome

`rend_vk` now provides cheat-protected `vk_showtearing`. When enabled, it
records a final native presentation render pass after all scene, post-process,
RmlUi, and no-world entity-overlay work. That pass clears the full image,
alternating white and red on successive submitted frames, immediately before
the screenshot copy and present operation.

This matches the purpose and ordering of OpenGL's `gl_showtearing` / 
`GL_DrawTearing`: make a tear boundary obvious by alternating complete output
frames. It is native Vulkan work and neither calls nor includes `rend_gl`.

## Implementation

The mode reuses the existing swapchain-local `presentation_render_pass` and
the current frame slot's presentation framebuffer. It creates no pipeline,
descriptor, texture, or allocation on the render hot path. The normal color
and depth clear values are submitted after the UI overlay, and the existing
screenshot request follows that render pass, so captures observe the same
final presentation state that reaches the display.

The red/white phase is intentionally not a visual-parity image gate. Each
backend may perform a different number of startup frames before a headless
capture command is consumed, while both correctly alternate the same two
colours thereafter. A full-frame red versus white result proves an opposite
phase, not a renderer presentation defect.

## Runtime evidence

`assets/renderer_parity/fr01_tearing_debug.cfg` enables cheats, both renderer
controls, loads `worr_fr01_glowmap`, and captures a 960 by 720 frame. A
validation-enabled run produced the expected full-frame alternate colours:
OpenGL was white and Vulkan was red in that run. Vulkan validation reported no
VUID or validation errors. The opposite phase is expected for this temporal
diagnostic and is not retained as a false broad-threshold screenshot manifest.

## Verification

```powershell
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py --assets-dir assets --install-dir .install --base-game basew
python -m unittest tools.renderer_parity.test_vulkan_tearing_source
```

For runtime inspection, execute `renderer_parity/fr01_tearing_debug.cfg` in a
hidden `win_headless 1` isolated home and inspect each full-frame capture for
either valid colour. Do not assert that the two renderers must select the same
temporal phase.

## Remaining boundary

This closes the direct Vulkan implementation of the tearing diagnostic only.
It does not close `gl_showtris`, the broken headless OpenGL origin-axis
baseline, broader material/UI parity, or the representative performance
budgets required by `FR-01-T15`.
