# Vulkan entity-origin diagnostic status

Task ID: `FR-01-T15`  
Date: 2026-07-16

## Outcome

`rend_vk` now provides the cheat-protected `vk_showorigins` control. When it
is non-zero, every eligible ordinary model queues three native Vulkan debug
lines from the entity origin: X red, Y green, and Z blue. Each line is sixteen
units long, depth-tested, and uses the entity's scaled axis.

The implementation is wholly native to Vulkan. It uses the renderer's Vulkan
debug-line pipeline; no Vulkan route calls or includes `rend_gl`.

## Scope and matching rules

`VK_Entity_RenderFrame` enables the overlay only after it has rejected view
models, beams, flares, inline BSP models, and invalid model handles. The
helper also rejects `RF_WEAPONMODEL`. This follows the intended OpenGL
classification contract while avoiding accidental axes for special entity
paths.

`VK_Entity_BuildTransform` supplies the scaled axes. The overlay deliberately
uses the un-interpolated entity origin, matching the OpenGL diagnostic's
origin/end-point rule rather than the renderer's interpolated mesh position.

## Validation status

- `worr_vulkan_x86_64.dll` builds successfully.
- `test_vulkan_debug_origins_source.py` verifies the cvar, render-frame gate,
  scaled-axis construction, RGB lines, weapon exclusion, and absence of an
  OpenGL include.
- A hidden 960x720 capture of the stock `dmspot` MD2 with Vulkan validation
  enabled rendered the three expected RGB masks: red 52 pixels, green 90
  pixels, and blue 90 pixels. The Vulkan validation log had no VUID or
  validation errors.

A paired OpenGL/Vulkan screenshot gate was attempted but is not retained as a
passing regression fixture. Both renderers reported their `*_showorigins`
cvar as `1`; an OpenGL probe also confirmed repeated calls to
`GL_DrawNullModel`. Nevertheless, OpenGL produced no pure red, green, or blue
axis pixels anywhere in the 960x720 capture, while Vulkan produced the masks
above. Controls removing depth testing and blending from the OpenGL submission
did not change that result and the driver reported no GL error.

Therefore this is a native Vulkan functional improvement, not a completed
visual-parity claim. Repairing the OpenGL baseline or establishing a stable
cross-renderer diagnostic capture remains open under `FR-01-T15`; `gl_showtris`
also remains unimplemented in Vulkan.

## Regression coverage

Run the source contract with:

```powershell
python -m unittest tools.renderer_parity.test_vulkan_debug_origins_source
```

For runtime work, use a hidden `win_headless 1` local fixture with cheats
latched before `map`, then enable `gl_showorigins` and `vk_showorigins` after
the map reaches the spawned state. Do not treat the current OpenGL image as a
visual baseline until its axis path rasterizes correctly.
