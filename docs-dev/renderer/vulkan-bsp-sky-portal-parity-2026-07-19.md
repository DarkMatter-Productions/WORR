# Native Vulkan BSP Sky-Portal Parity

Date: 2026-07-19

Task ID: `FR-01-T12`

Status: complete for the authored static sky/fog portal regression. Broader
fog and post-processing coverage remains owned by `FR-01-T12`.

## Outcome

The native Vulkan renderer now follows the OpenGL sky contract for maps whose
six compatible sky textures are packed into the Vulkan 2D texture array:

- the original BSP `SURF_SKY` geometry is the portal through which the sky is
  seen;
- the legacy six-face mapping is evaluated from that portal's world direction;
- the immutable 36-vertex cube is retained only as the Vulkan fallback when a
  source set cannot use the array path; and
- unfilled portions of a fogged portal clear to the corresponding native
  fog-coloured background.

This closes the former 42-pixel `fr01_sky_fog` static-sky discrepancy without
redirecting any Vulkan path through the OpenGL renderer.

## OpenGL-compatible behaviour

The important distinction is between a sky texture source and its visible
coverage. A compatible six-face source can be submitted as a single array
draw, but that does not make the static cube its visible geometry. OpenGL
continues to use the map's `SURF_SKY` polygons as portals and chooses a legacy
sky face from the portal direction. Filling the whole cube in the Vulkan array
case covered regions which OpenGL instead leaves to its fog-coloured scene
background.

The Vulkan fallback still uses the cube when the source textures cannot be
represented by the array (format, dimensions, or layout incompatibility). It
therefore retains the established six independent native sky-face draws and
does not change the incompatible-source contract.

## Native implementation

`vk_world.c` now keeps the original BSP sky batches distinct from the
`VK_WORLD_VERTEX_SKY` cube geometry. When the texture-array path is available,
it copies the discontiguous `VK_WORLD_BATCH_SKY` ranges once from the immutable
device-local world buffer into a compact immutable portal buffer. The copy is
done at map registration or compatible-sky selection, never in an ordinary
frame. `VK_World_RecordOpaque` binds that packed BSP portal buffer with the
dedicated native `pipeline_sky_portal` and array descriptor, then submits it in
one draw. If allocation or the one-time transfer fails, the original native
BSP-batch loop remains the functional fallback. The materialless sky-face case
remains legal and binds the native white lightmap descriptor as before.

`vk_world.vert` forwards the sky axes and `vk_world_sky.frag` reconstructs the
world-space portal direction relative to the view origin. The fragment shader
uses the inverse legacy sky axes followed by the same six face selections and
UV orientation as the existing sky texture contract. Static cube vertices still
use their face/UV attributes; portal vertices use this world-direction route.
Sky fog is applied by the existing native fragment path in both cases.

The scene colour attachment is normally cleared to black. `vk_main.c` now asks
`VK_Shadow_GetSkyFogClearColor` for a clear colour before beginning the scene
pass. The helper returns false unless the native global and sky-fog bits are
both active. In that enabled case it supplies the authored fog colour with the
legacy LDR clear quantisation compensation (`250 / 255`). Attachment clears
bypass the fragment fog calculation, so this preserves the OpenGL background
observed through an uncovered fogged sky portal. Normal clear colour remains
black whenever either fog condition is off.

## Performance and lifecycle

The portal route adds one pre-created graphics pipeline and reuses the array
descriptor plus compact current-frame world record. Array-compatible skies pack
their portal geometry during registration/sky selection and use one draw per
ordinary frame, rather than one draw for every discontiguous BSP portal. The
world buffer now also declares `VK_BUFFER_USAGE_TRANSFER_SRC_BIT`, making that
native device-local copy validation-correct. It creates no per-frame vertex
upload, descriptor allocation, image copy, CPU mesh transform, or draw-time
pipeline creation.

Pipeline creation, destruction, and recordability checks include the new
pipeline. The fallback is deliberately preserved, keeping unsupported source
sets fully native and isolated from OpenGL. The source regression test also
rejects an OpenGL renderer include in the affected Vulkan units.

## Regression evidence

The targeted strict capture was run headlessly from the staged distributable:

```powershell
python tools/renderer_parity/run_capture_matrix.py `
  --install-dir .install `
  --manifest assets/renderer_parity/fr01_sky_fog_manifest.json `
  --run-root .tmp/renderer-parity/fr01-sky-portal-fog-clear-scaled-native `
  --timeout 180 `
  --vulkan-validation
```

The 77,000-pixel sky crop passed with maximum RGB difference `1 / 1 / 1`, mean
absolute difference `0.000454545 / 0.000090909 / 0.000207792`, and zero pixels
over the RGB-two threshold. The fog-coloured coverage count was exactly 75,379
pixels on both renderers, with IoU `1.0`. The Vulkan validation log contained no
validation errors or VUID diagnostics.

The structural guardrail is
`tools/renderer_parity/test_vulkan_sky_portal_source.py`. It checks the native
portal-versus-fallback ordering, immutable-world-to-packed-portal transfer,
legacy world-direction face selection, fog-aware clear activation, and the
absence of a renderer route to OpenGL.

## Scope boundary

This result closes the static compatible-array sky portal and its authored
sky-fog background. It does not claim all fog, bloom, HDR, colour correction,
DOF, CRT, or resolution-scaling work complete. Those remaining `FR-01-T12`
areas retain their independent fixtures and performance work.
