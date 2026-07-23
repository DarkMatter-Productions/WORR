# Native Vulkan Opaque Sprite Depth-Write Parity

Date: 2026-07-19

Project task: `FR-02-T05`

Status: corrected and protected by a validation-enabled paired capture.

## Outcome

OpenGL renders every sprite with depth testing enabled and depth writes
disabled, even when the source image is fully opaque. Vulkan previously sent a
fully opaque, non-paletted sprite through the generic depth-writing opaque
entity pipeline. That could hide a later translucent sprite which OpenGL would
blend over it.

`VK_Entity_AddSprite` now routes that exact state to a native opaque,
no-depth-write pipeline. Fully opaque sprites keep blending disabled, so the
fix matches OpenGL without introducing source-alpha blend work.

## Native pipeline contract

`VK_ENTITY_BLEND_OPAQUE_NO_DEPTH_WRITE` is created with the ordinary native
entity shader, depth testing, disabled blending, and `depthWriteEnable` false.
It has normal and depth-hack variants, both created at swapchain-resource
initialization. A dynamic sprite batch carries an explicit `no_depth_write`
state so it cannot coalesce with a depth-writing opaque batch.

The pre-existing paletted cutout pipeline remains separate: it also disables
depth writes but enables the alpha-test vertex flag. Source-alpha and
`RF_TRANSLUCENT` sprites continue to use the native alpha pipeline. The change
does not create descriptors or pipelines on the frame hot path, and does not
route Vulkan rendering through OpenGL.

## Deterministic overlap fixture

`tools/renderer_parity/generate_sprite_depth_write_fixture.py` owns a fixed
view with two ordinary map `misc_model` sprites:

- a fully opaque truecolour green sprite at X=256; and
- a red `alpha 0.5`, `renderFX 32` (`RF_TRANSLUCENT`) truecolour sprite at
  X=384, behind it.

OpenGL's no-depth-write state lets the far sprite's later alpha pass blend over
the nearer opaque sprite. The map, two generated SP2 files, and textures are
staged through the regular loose no-zlib capture path. The contract and focused
source coverage are retained in:

- `assets/renderer_parity/fr02_sprite_depth_write_manifest.json`;
- `tools/renderer_parity/test_generate_sprite_depth_write_fixture.py`; and
- `tools/renderer_parity/test_vulkan_sprite_depth_write_parity_source.py`.

## Validation evidence

Final staged command:

```text
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr02_sprite_depth_write_manifest.json --run-root .tmp/renderer-parity/fr02-sprite-depth-write-staged-final --timeout 180 --vulkan-validation
```

The 360x440 crop contains 158,400 pixels. It passed exactly:

```text
maximum RGB error:                  0 / 0 / 0
mean absolute RGB error:            0 / 0 / 0
pixels above RGB error 0:           0
opaque green outer region:          56,000 / 56,000, IoU 1.0
far alpha-over-green blended region:102,400 / 102,400, IoU 1.0
Vulkan VUID/validation/fatal diagnostics: none
```

The visible blended region is RGB `128 / 132 / 56`, proving that the far
translucent sprite remains depth-test visible through the opaque sprite exactly
as in OpenGL. Broader gameplay-emitter coverage remains the next sprite scope
under `FR-02-T05`.
