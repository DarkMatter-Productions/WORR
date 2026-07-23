# Native Vulkan Entity Frustum Culling

Date: 2026-07-19  
Tasks: `FR-01-T14`, `FR-01-T15`

## Outcome

Vulkan now natively culls ordinary off-screen inline BSP, MD2, and MD5 model
submissions before entity batching. `vk_cull_models` is a default-on (`1`)
runtime control for same-binary A/B comparison and driver triage; it never
routes rendering through OpenGL.

The generated mixed inline-BSP scene proves the culling-enabled Vulkan image
is pixel-identical to OpenGL and, independently, that the disabled Vulkan
control remains pixel-identical to OpenGL. The capture pair therefore proves
the optimization retains the visible result while reducing Vulkan submission
work.

## Native design

`VK_Entity_LoadMD2` derives and retains a local-space min/max pair for every
source MD2 frame while it decodes the existing frame data. The cached bounds
include the MD2 frame translate, eliminating a per-entity vertex scan. At
submission time `VK_Entity_ModelBoundsForRefdef` resolves the current and old
frame with the active refdef semantics and unions their bounds. MD5 retains the
same conservative source-model bounds contract used by the existing alias
frontend.

`VK_Entity_CullModelBounds` applies the existing entity transform to all eight
corners and encloses them in a world-space AABB. It then tests that AABB only
against the four side planes built from the refdef FOV/view axes, matching the
ordinary OpenGL model-culling contract. There is intentionally no near/far
test. This world-AABB approach is conservative for rotations and non-uniform
scale: it can retain extra work, but cannot reject a potentially visible
model. View-weapon entities (`RF_WEAPONMODEL`), sprites, beams, particles, and
the existing special effect paths remain outside this optimization. Origin-axis
debug drawing remains ordered before the model cull, as it is in the OpenGL
path.

The four culling planes are constructed once per entity frame rather than for
every model. Ordinary unrotated unit-scale entities use a direct translated
AABB; rotated or scaled entities retain the conservative eight-corner path.
This keeps the culling decision unchanged while avoiding repeated FOV
trigonometry and unnecessary transforms in the common case.

The new `VK_STATS` field `entity_models_culled` is aggregated by
`analyze_renderer_perf.py`, so a performance run reports the actual rejected
work instead of inferring it from timing alone.

## Fixture and visual evidence

`tools/renderer_parity/generate_bmodel_cull_fixture.py` creates
`maps/worr_fr01_bmodel_cull.bsp`: one base inline model, nine ordinary visible
`func_wall` instances, and 42 ordinary instances deliberately outside the
120-degree fixed view. `sv_novis` coverage from the shared fixture setup keeps
the off-screen entities available to the renderer.

The validation-enabled matrix uses both the default path and a per-scene
`vk_cull_models=0` launch override. Each scene compares Vulkan with the
unchanged OpenGL reference over the 520x420 crop `[220, 150, 520, 420]`:

| Mode | Pixels | Maximum RGB error | Pixels over RGB 8 | Visible grid mask |
|---|---:|---:|---:|---|
| Default culling | 218,400 | `0 / 0 / 0` | 0 | 146,908 pixels/backend, IoU `1.0` |
| `vk_cull_models=0` | 218,400 | `0 / 0 / 0` | 0 | 146,908 pixels/backend, IoU `1.0` |

Both Vulkan paths are thus exact with the same OpenGL result. Vulkan
validation was enabled for both matrix scenes.

## Same-binary telemetry

The headless paired collector used the staged binary, 120 samples per backend,
20 discarded warm-up samples, and 100 retained samples. Hardware provenance:
Intel(R) Iris(R) Xe Graphics; 13th Gen Intel(R) Core(TM) i7-13700H; Windows 11
Home 10.0.26200; Intel `31.0.101.5590 (2024-06-10)`. Both captures used the
same fixture/configuration, with only `vk_cull_models` differing.

| Vulkan metric (retained mean unless stated) | Disabled | Enabled | Change |
|---|---:|---:|---:|
| `entity_models_culled` | 0 | 42 | +42 rejected models/frame |
| Entity draws | 15 | 9 | -40.0% |
| Total draws | 16 | 10 | -37.5% |
| Upload bytes | 6,720 | 1,344 | -80.0% |
| Opaque-entity GPU time | 0.09343 ms | 0.07495 ms | -19.8% |
| GPU-frame mean | 0.30982 ms | 0.30797 ms | -0.6% |
| CPU render mean | 0.53862 ms | 0.49622 ms | -7.9% |
| CPU render p50 | 0.5085 ms | 0.489 ms | -3.8% |
| CPU-frame mean | 0.71477 ms | 0.65938 ms | -7.7% |

The culler achieves its intended submission/transfer reduction and lowers the
mean entity GPU phase and CPU render time in this deliberately off-screen-heavy
lane. This is a provenance-bound same-binary fixture result, not a
renderer-wide product performance budget. It supplies truthful cull telemetry
for broader-map profiling under `FR-01-T15`.

## Validation

```powershell
meson compile -C builddir-win worr_vulkan_x86_64
python -m unittest tools.renderer_parity.test_vulkan_entity_model_culling_source tools.renderer_parity.test_generate_bmodel_cull_fixture tools.renderer_parity.test_vulkan_gpu_bmodel_submission_source tools.renderer_parity.test_analyze_renderer_perf tools.test_package_assets
python tools/refresh_install.py --build-dir builddir-win --assets-dir assets --install-dir .install --base-game basew --archive-name pak0.pkz
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_bmodel_cull_manifest.json --run-root .tmp/renderer-parity/fr01-bmodel-cull-visual --vulkan-validation --timeout 180
```

`run_renderer_perf_capture.py` performed the enabled/disabled headless
captures with `win_headless=1`, disabled input/audio, isolated homes, and
Vulkan validation. `analyze_renderer_perf.py` accepted each 100-sample capture
manifest with fixture, configuration, hardware/driver, and telemetry hashes.
