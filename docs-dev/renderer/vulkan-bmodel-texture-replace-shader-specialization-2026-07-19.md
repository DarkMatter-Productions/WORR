# Native Vulkan Inline-BSP Texture-Replace Shader Specialization

Date: 2026-07-19

Task IDs: `FR-01-T14`, `FR-01-T15`

Status: implemented, validation-tested, and retained. Ordinary opaque
inline-BSP faces that follow OpenGL's `GLS_TEXTURE_REPLACE` material behavior
now use small native Vulkan shader pairs instead of paying the generic entity
receiver shader's lightmap, colour, normal, and material-branch cost.

## Change

`vk_entity_gpu_bmodel_texture_replace.vert` keeps the exact model-space
position transform and Vulkan depth conversion used by
`vk_entity_gpu_bmodel.vert`. Its texture-replace interface reads only position,
UV, face flags, origin, scaled axes, and entity flags. It preserves the merged
flag value used by the intensity convention.

The fog-aware vertex variant exports world position because height fog needs
it. The no-fog variant exports only UV and flags. Both use the dedicated
`vk_entity_gpu_bmodel_texture_replace.frag` source, which samples the base
texture, applies the existing optional intensity multiplier, and retains the
generic global/height fog formula byte-for-byte in meaning. The no-fog
fragment avoids the fog interface entirely. This fixes the generic shader
interface mismatch that validation correctly detected during implementation;
no placeholder varyings are emitted.

`VK_Entity_CreatePipelineEx` selects the pair for the two existing opaque
GPU-bmodel texture-replace blend modes. The compact shader ABI is embedded by
`tools/gen_vk_world_spv.py`; it does not redirect to OpenGL. The default-on
archived `vk_bmodel_texture_replace_specialization` switch keeps the old
generic vertex/fragment pair as a focused driver regression and performance
control. It affects only pipeline creation and leaves the existing
`vk_bmodel_texture_replace` material eligibility feature unchanged.

## Visual and validation evidence

All captures ran headlessly at 960x720 with Vulkan validation enabled.

| Fixture | Result |
| --- | --- |
| `fr01_bmodel_instances_unlightmapped_manifest.json` | 218,400-pixel crop exact: maximum RGB `0 / 0 / 0`, zero pixels over threshold, and the inline-BSP probe has IoU `1.0` with 179,742 pixels on both renderers. |
| `fr01_bmodel_instances_fog_manifest.json` | 218,400-pixel crop: maximum RGB `1 / 1 / 1`, MAE `0.003846 / 0.030508 / 0.018503`, and zero pixels over threshold 8. |

The validation logs in
`.tmp/renderer-parity/bmodel-texture-replace-vertex-specialization-visual-final`
and
`.tmp/renderer-parity/bmodel-texture-replace-full-specialization-fog-visual`
contain no VUID, validation error, device loss, or fatal renderer error.

## Performance evidence

A same-binary A/B used `fr01_renderer_perf_bmodel_instances.cfg`, the fixed
37-instance unlightmapped BSP grid, 960x720, 120 samples per renderer,
20-frame warm-up, and 100 retained samples on Intel Iris Xe driver
`31.0.101.5590`. The two runs differ only in
`vk_bmodel_texture_replace_specialization`; both retain 16 entity
texture-replace/no-fog draws, 17 total draws, and 4,800 upload bytes per
sample.

| Native Vulkan measurement | Specialization on | Generic shader control | Change |
| --- | ---: | ---: | ---: |
| GPU opaque entity mean | 0.17040 ms | 0.29725 ms | -42.7% |
| GPU scene mean | 0.33764 ms | 0.40683 ms | -17.0% |
| GPU frame mean | 0.36495 ms | 0.42585 ms | -14.3% |
| CPU frame mean | 0.46322 ms | 0.45768 ms | +1.2% |

The GPU gain is the decision criterion for this shader specialization. The
small CPU difference is within a presentation-pacing-sensitive headless
measure and is not promoted as a CPU claim. The specialized run remains
slower than its paired OpenGL GPU-frame mean (`0.36495 ms` versus `0.31029`
ms), so this is a proven local Vulkan improvement rather than a renderer-wide
GPU-superiority budget pass.

The evidence roots are
`.tmp/renderer-parity/fr01-renderer-perf-bmodel-texture-replace-full-on` and
`.tmp/renderer-parity/fr01-renderer-perf-bmodel-texture-replace-full-off`.

## Verification

```text
python tools/gen_vk_world_spv.py --validate
python -m unittest discover -s tools/renderer_parity -p 'test_vulkan_gpu_bmodel_submission_source.py'
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_bmodel_instances_fog_manifest.json --run-root .tmp/renderer-parity/bmodel-texture-replace-full-specialization-fog-visual --timeout 90 --vulkan-validation
```
