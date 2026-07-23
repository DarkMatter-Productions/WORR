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

## Paired validation

- `worr_vulkan_x86_64.dll` builds successfully.
- `test_vulkan_debug_origins_source.py` verifies the cvar, render-frame gate,
  scaled-axis construction, RGB lines, weapon exclusion, and absence of an
  OpenGL include.
- A hidden 960x720 capture of the stock `dmspot` MD2 with Vulkan validation
  enabled renders the expected RGB masks: 52/56 red pixels, 90/86 green
  pixels, and 90/90 blue pixels for Vulkan/OpenGL respectively.
- The retained `fr01_model_origins_manifest.json` crop has red/green/blue mask
  IoUs `0.92857`, `0.95556`, and `1.0`. Four endpoint pixels differ by more
  than one RGB code because the APIs rasterize diagonal line endpoints
  differently; no other diagnostic pixel difference exceeds one code.
- The Vulkan validation log contains no VUID or validation errors.

The previous OpenGL baseline failure was a real transform defect. Its
`GL_DrawNullModel` built vertices in world space and then applied
`glr.entmatrix`, which contains the same entity translation, rotation, and
scale. The axes were therefore double-transformed and could be off-screen.
OpenGL now submits local `{0, 0, 0}`-to-`{16, 0, 0}` / `{0, 16, 0}` /
`{0, 0, 16}` segments through that matrix, matching Vulkan's native world
segments without routing either renderer through the other.

Broader `vk_showtris` material/effect coverage remains a separate diagnostic
follow-up.

## Regression coverage

Run the source contract with:

```powershell
python -m unittest tools.renderer_parity.test_vulkan_debug_origins_source
```

For runtime work, use the retained hidden fixture:

```powershell
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_model_origins_manifest.json --run-root .tmp/renderer-parity/fr01-model-origins-final --vulkan-validation
```
