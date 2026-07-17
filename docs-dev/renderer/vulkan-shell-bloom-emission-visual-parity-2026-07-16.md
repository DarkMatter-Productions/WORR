# Vulkan Translucent-Shell Bloom-Emission Visual Parity

Date: 2026-07-16

Task ID: `FR-01-T12`

Status: native Vulkan bloom extraction now covers the OpenGL translucent
colour-shell material path and fixes the shell's hidden-back-face alpha blend.
The route retains GPU MD2/MD5 submission and has a paired, headless visual
regression gate. Remaining alpha/depth-hack material families are still open
work.

## OpenGL contract

`rend_gl/mesh.c` treats `RF_SHELL_MASK` meshes as a shell material: it selects
the white shell texture, supplies the shell colour through the mesh colour,
uses the ordinary alpha blend state for `RF_TRANSLUCENT`, and enables
`GLS_BLOOM_SHELL` when the OpenGL bloom MRT is active. The fragment path puts
the post-intensity diffuse shell RGB into the bloom output. Consequently the
same shell alpha that blends the visible shell also blends its bloom source;
the source must not be thresholded from the already-composited scene.

## Native Vulkan implementation

`VK_ENTITY_VERTEX_BLOOM_SHELL` identifies shell batches during entity
collection. It is set for CPU MD2, GPU MD2, CPU MD5, and GPU MD5 shell
submission. The per-frame bloom replay now admits an alpha batch only when it
has that flag; it continues to reject unrelated alpha/additive, depth-hack,
weapon, flare/query, outline, and item-colourize work. Opaque glowmap batches
continue through their existing extractor.

`vk_entity.c` owns matching bloom-only alpha pipelines for the dynamic, GPU
MD2, and GPU MD5 vertex layouts. Their blend state is the same standard
source-alpha blend used by the normal shell draw. The shader's
`VK_ENTITY_BLOOM_EXTRACT` variant writes
`base.rgb * in_color.rgb` for a shell, performs the normal intensity
operation, retains `base.a * in_color.a`, and applies the existing global and
height-fog transmission. Thus the dedicated cleared emission attachment gets
the OpenGL-equivalent alpha-weighted shell source without adding a bloom MRT,
shader output, or draw to the normal scene pass.

The replay reuses the submitted batch, descriptor set, native depth buffer,
GPU MD2 source buffers, MD5 skinning data, and compact per-frame instance
streams. It adds one selective native draw only while `vk_bloom 1`; it does
not expand GPU-skinned models on the CPU or route work through OpenGL. Inline
BSP shell models already use the established native dynamic fallback for
special flags, so they use the dynamic alpha extractor rather than a separate
GPU-BSP shell path.

The shared native entity pipeline must remain two-sided for sprites,
particles, and other dynamic primitives. Alias meshes are different: OpenGL
leaves back-face culling enabled for them. A translucent shell disables depth
writes, so accepting its hidden back-facing fragments blended the shell twice
in Vulkan. The shell flag now performs that narrowly scoped native cull with
`gl_FrontFacing` in the fragment stage. It affects only shell batches in both
the normal and bloom-extract variants, preserving the shared two-sided
pipeline for unrelated primitive families.

## Deterministic receiver scene

`generate_model_shell_fixture.py` builds
`maps/worr_fr01_model_shell.bsp` from the existing deterministic renderer BSP
builder. The scene adds a scaled stock MD2 `misc_model` with a translucent
blue `RF_SHELL_MASK` render effect. This is a direct shell receiver, avoiding
client-game powerup policy while exercising the exact OpenGL
`GLS_BLOOM_SHELL` material route.

The fixture replaces both the background and inline-BSP texture with the
non-emissive blue backdrop. The capture config enables matched bloom controls,
disables unrelated colour/CRT/DOF/glowmap effects, and sets both scene bloom
thresholds to `100`. This prevents ordinary scene thresholding from creating
an apparent result: the saturated core and blue-backdrop halo must originate
from the shell emission replay. The generated BSP is listed in
`DEFAULT_LOOSE_ASSET_PATHS`, so the no-window install layout used by capture
also sees it without relying on package mounting.

The manifest crop is `[570, 450, 330, 180]` and has three independent guards:

- RGB mean limits `[1.6, 2.7, 0.07]` and no pixels beyond RGB error 16;
- at least 52,000 matching saturated-blue shell-core pixels, with exact
  backend mask agreement;
- at least 1,750 soft halo pixels per backend on the blue backdrop, bounded
  to a 2% count delta and `0.98` mask IoU.

The disabled-bloom control is materially different: it reports mean absolute
error `1.44175 / 2.52407 / 13.34325` and `35.94108%` pixels beyond RGB error
16; neither backend has a qualifying halo and the saturated core mask is
absent in OpenGL. That makes this gate sensitive to the native authored source
rather than only the ordinary shell surface.

## Paired evidence

The bloom-enabled paired run reports 59,400 crop pixels, maximum RGB error
`6 / 10 / 5`, mean absolute error `1.44175 / 2.52407 / 0.05362`, and zero
pixels beyond RGB error 16. The bright shell core contains 53,091 pixels in
both backends with IoU `1.0`. The soft halo contains 1,781 OpenGL and 1,758
Vulkan pixels with IoU `0.98263`. Vulkan validation and process failures are
absent in the final retained run.

## Verification

```text
python tools/gen_vk_world_spv.py --validate
python tools/renderer_parity/generate_model_shell_fixture.py --validate
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py
python -m unittest tools.renderer_parity.test_vulkan_bloom_source tools.renderer_parity.test_generate_model_shell_fixture tools.renderer_parity.test_model_shell_bloom_emission_fixture
python tools/test_package_assets.py
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_model_shell_bloom_emission_manifest.json --run-root .tmp/renderer-parity/fr01-model-shell-bloom-emission-final --vulkan-validation --json-output .tmp/renderer-parity/fr01-model-shell-bloom-emission-final/report.json
```

The capture runner enables the repository's hidden native-surface/headless
mode. It does not initialise client input, capture the mouse, or launch an
interactive client window.

## Remaining boundary

This closes the direct translucent colour-shell emission source and its
hidden-back-face alpha blend delta. Direct depth-hack/view-weapon replay is
covered separately in
`vulkan-depthhack-viewweapon-bloom-visual-parity-2026-07-16.md`. Remaining
`FR-01-T12` bloom work includes broader additive/material policy,
replacement-material coverage, OpenGL's mip-pyramid hierarchy, HDR/tone
mapping, and resolution scaling.
