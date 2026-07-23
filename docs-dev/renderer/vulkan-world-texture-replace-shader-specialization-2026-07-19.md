# Native Vulkan World Texture-Replace Shader Specialization

Date: 2026-07-19

Task IDs: `FR-01-T13`, `FR-01-T15`

## Change

Opaque world texture-replace pipelines now use dedicated native vertex and
fragment shader pairs. The no-fog pair consumes only position, UV, and flags;
the fog-aware pair additionally carries world position for the unchanged
global/height fog equation. This avoids the animated receiver shader's
lightmap, colour, normal, flow, refraction, and sky interface on material
classes that cannot consume it. `vk_world_texture_replace_specialization`
defaults to one and retains the original generic pair as a pipeline-creation
control. No OpenGL route is used.

## Evidence

The validation-enabled paired world fixture is exact in both active paths:
the no-fog and global-fog 235,200-pixel crops have maximum RGB `0 / 0 / 0` and
zero pixels over threshold. The log root
`.tmp/renderer-parity/world-texture-replace-specialization-visual` contains no
VUID, validation error, device loss, or fatal renderer error.

The same-binary 100-sample dense-scene A/B leaves one world texture-replace
draw, 16 entity texture-replace draws, 17 total draws, and 4,800 upload bytes
unchanged. On Intel Iris Xe driver `31.0.101.5590`, specialization-on reduces
GPU-frame mean from `0.39753` to `0.37706` ms (5.1%) and opaque-world mean
from `0.17087` to `0.16804` ms (1.7%). P50 frame time is effectively flat
(`0.377` versus `0.376` ms), so this is retained as a small local reduction,
not a renderer-wide GPU budget claim.

## Verification

```text
python tools/gen_vk_world_spv.py --validate
python -m unittest discover -s tools/renderer_parity -p 'test_vulkan_world_fast_lit_source.py'
ninja -C builddir-win worr_vulkan_x86_64.dll
```
