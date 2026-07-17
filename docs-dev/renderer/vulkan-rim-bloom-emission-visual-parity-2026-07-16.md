# Vulkan Additive Rim-Bloom Emission Visual Parity

Date: 2026-07-16

Task ID: `FR-01-T12`

Status: partial implementation. Native Vulkan now extracts the OpenGL
`RF_RIMLIGHT` additive receiver into bloom without routing rendering through
OpenGL or expanding GPU-skinned models on the CPU. The retained fixture proves
the third-person player rim's visible core and bloom halo; the companion
sampled-depth receiver now exact-compares an obstructed rim where the selected
depth format supports sampling.

## OpenGL contract

`rend_gl/mesh.c` renders a translucent `RF_RIMLIGHT` model with additive
blending and disabled depth writes. When the bloom MRT is enabled, it marks the
model with both `GLS_BLOOM_GENERATE` and `GLS_BLOOM_SHELL`. The generated
OpenGL fragment shader therefore puts the rim-lit diffuse value into the bloom
target rather than relying on scene-threshold extraction:

```text
rim = pow(1 - max(dot(normalize(normal), view_direction), 0), 2)
bloom = vec4(rim_colour.rgb * rim, rim_colour.a * rim)
```

The client game produces the real receiver by adding a second
`RF_RIMLIGHT | RF_TRANSLUCENT` entity for third-person team/player rims. The
parity fixture exercises that exact path rather than inventing a Vulkan-only
effect.

## Native Vulkan implementation

`vk_entity.c` marks CPU MD2, GPU MD2, CPU MD5, and GPU MD5 rim batches with
`VK_ENTITY_VERTEX_BLOOM_RIM`, alongside the existing native rim-light flag.
`VK_Entity_RecordBloomEmission` admits only that explicitly marked additive
batch; it continues to exclude unrelated additive/alpha effects, depth-hack
and weapon batches, flares/queries, outlines, and item-colourization.

The bloom-only replay chooses a matching additive extractor for the dynamic,
GPU-MD2, and GPU-MD5 layouts. It reuses the recorded descriptors, source
buffers, index ranges, and GPU skinning data. It is recorded only with
`vk_bloom 1`, so normal bloom-disabled rendering gets no extra attachment,
pipeline bind, or draw. The fragment extract calculates the same squared rim
term and writes the source-alpha additive emission to the dedicated native
bloom attachment. Shell/rim alias batches also use the narrow
`gl_FrontFacing` discard already needed to match OpenGL alias back-face
culling, without changing the shared two-sided sprite/particle path.

The Vulkan blur retains more energy than the comparable OpenGL downsample
route at the public fixture settings. The emission source is therefore
normalised by `0.70` before blur. This is a backend-local source calibration,
not an OpenGL renderer route or a public visual control; the paired halo gate
below is the regression guard for it.

## Initial depth boundary

The ordinary bloom extract pipeline depth-tests the retained scene depth. On
some Vulkan depth formats, replaying equal alias-model depth values did not
reliably pass the required `LESS_OR_EQUAL` comparison. The additive rim
extractor consequently has a dedicated depth-disabled pipeline so the real
receiver emits reliably on those formats.

That direct-only fallback was superseded for sampleable depth formats by a
native sampled-depth rim pass. It loads the existing emission attachment,
samples the retained scene depth per fragment, and records only an active rim
receiver; the generated BSP occlusion gate exact-compares it with OpenGL. The
depth-disabled pipeline remains only as the native constrained-format fallback
when a sampled depth view cannot be created. See
`vulkan-rim-bloom-occlusion-visual-parity-2026-07-16.md` for the authoritative
implementation and evidence.

## Deterministic headless fixture

`generate_model_rim_fixture.py` creates `worr_fr01_model_rim.bsp` using the
existing renderer BSP builder. The map is a non-emissive player-start scene;
the capture config enables third-person team rim light, matched one-pass bloom
settings, and a high scene threshold. Thus neither the background nor normal
threshold extraction can create the green halo.

The fixture sets `gl_gpulerp 0` only for the OpenGL baseline capture. The
OpenGL GPU-lerp path currently exits during this headless rim-plus-bloom
scenario, while its CPU-lerp path produces the valid reference. Vulkan keeps
its native model submission; this is a baseline capture limitation, not a
Vulkan fallback or a runtime policy change.

The `[400, 280, 280, 400]` crop uses independent receiver probes:

- the saturated green third-person rim core requires at least 25,000 pixels
  per backend, at most a 1% count delta, and `0.99` IoU;
- the softer green bloom halo requires at least 3,500 pixels per backend, at
  most a 10% count delta, and `0.89` IoU;
- the crop has a maximum mean RGB error of `[0.1, 5.0, 0.1]` and permits at
  most 10% of pixels above RGB error 16.

## Retained paired evidence

The fresh OpenGL/Vulkan run at 960x720 passed with Vulkan validation and no
process failures. The 112,000-pixel crop reports maximum RGB error
`83 / 96 / 80`, mean absolute error `0.03796 / 4.47651 / 0.03628`, and
`9.55536%` pixels above RGB error 16. The deliberately receiver-focused
probes give stronger evidence than the full-frame aggregate:

- core: 25,766 OpenGL / 25,758 Vulkan pixels, `0.03105%` count delta and
  `0.99922` IoU;
- halo: 4,097 OpenGL / 3,732 Vulkan pixels, `8.90896%` count delta and
  `0.89702` IoU.

The matching no-bloom control has no qualifying halo; with bloom enabled,
the captured Vulkan frame differs from that control in 62,220 pixels and its
green channel reaches a 183-level maximum delta. This proves the native
extract, rather than thresholded scene colour, supplies the tested halo.

## Verification

```text
python tools/gen_vk_world_spv.py --validate
python tools/renderer_parity/generate_model_rim_fixture.py --validate --json
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py
python -m unittest discover -s tools/renderer_parity -p 'test_*.py'
python tools/test_package_assets.py
python tools/renderer_parity/run_capture_matrix.py --renderer opengl --renderer vulkan --manifest assets/renderer_parity/fr01_model_rim_bloom_emission_manifest.json --run-root .tmp/renderer-parity/fr01-model-rim-bloom-emission-final --timeout 120 --vulkan-validation
```

The capture runner uses the repository's hidden native-surface/headless mode;
it neither initialises client input nor opens an interactive client window.
