# Native Vulkan Scene/Presentation Pass Split

Date: 2026-07-16

Task ID: `FR-02-T13`

Status: first renderer-graph refactor phase implemented and validation-tested.

## Change

The Vulkan context no longer uses one ambiguous `render_pass`/`liquid_render_pass`
pair for both 3D scene work and swapchain presentation. It now owns explicit
families:

- `scene_render_pass` and `scene_load_render_pass` for world, normal entities,
  sky, debug primitives, and transparent-liquid load work;
- `presentation_render_pass`, `presentation_load_render_pass`, and
  `presentation_overlay_render_pass` for final post-processing, CRT, UI, and
  presentation overlays.

World, entity, and debug pipelines now create against the scene family. UI
creates against the presentation family; post-process/CRT create against the
presentation load pass. The command recording path selects the corresponding
pass explicitly for opaque 3D, liquid refraction, final composition, CRT, UI,
and menu overlay phases.

For this phase the two families deliberately retain compatible LDR attachment
formats. That makes the change output-neutral while removing the hidden
pipeline/render-pass dependency that prevented a safe floating scene target.
No Vulkan path is redirected through OpenGL.

## Why this precedes the float target

Float scene attachments are not compatible with the swapchain presentation
passes. Without this split, changing the current common pass format would
invalidate world/entity/debug pipelines or force UI/menu-preview work into
the tone-mapped 3D target. The next phase can now change only the scene family
and its frame-slot framebuffer resources, while keeping presentation/UI
ownership explicit.

## Verification

```text
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/package_assets.py
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_split_toning_manifest.json --run-root .tmp/renderer-parity/scene-presentation-split --vulkan-validation
```

The retained fixed-view split-tone gate exact-compares 50,000 pixels at
`[9, 10, 34]`, with maximum/mean RGB error zero and no Vulkan VUID or
validation error. The staged installation contains the current native Vulkan
binary and packaged assets.
