# Vulkan inline-BSP lightmap parity and fast path

Task IDs: `FR-01-T14`, `FR-01-T15`  
Date: 2026-07-16

## Outcome

Vulkan now matches OpenGL for both unlightmapped and authored-lightmapped
opaque inline BSP faces, and its native static-light receiver specialization
has a measured dense-instance gain. The specialization is entirely within
`rend_vk`; it never redirects a Vulkan draw through OpenGL.

On the local Intel Iris Xe validation configuration, the specialization lowered
the dense grid's Vulkan median entity GPU phase from **1.109 ms** to
**0.346 ms** (68.8%), scene GPU phase from **1.925 ms** to **1.160 ms**
(39.8%), and completed-frame GPU time from **1.953 ms** to **1.189 ms**
(39.1%). This is an isolated inline-BSP result, not a claim that renderer-wide
GPU parity with OpenGL is complete.

## Parity defect and correction

The original dense ordinary-bmodel fixture deliberately used `r_fullbright 1`.
When a matching `r_fullbright 0` control was introduced, it exposed an
uncovered case: opaque inline faces with no authored lightmap were being sent
through Vulkan's CPU-lit entity fallback. With `gl_modulate 2`, that fallback
brightened them while OpenGL selected `GLS_TEXTURE_REPLACE` and drew the base
texture at identity.

`VK_ENTITY_VERTEX_TEXTURE_REPLACE` now marks that exact native condition:

- The immutable GPU inline-BSP geometry builder and its CPU-expansion fallback
  set it for an opaque face with no valid lightmap.
- `vk_entity.frag` outputs the base material directly, still applying the same
  texture-intensity and fog stages as OpenGL.
- The branch bypasses fallback entity lighting, sun receivers, and dynamic
  lights, matching OpenGL's texture-replace contract rather than changing
  `gl_modulate` semantics globally.

The marker is intentionally face-local; transparent inline faces retain the
complete material path.

## Authored lightmap coverage

The existing grid cannot exercise a static-light specialization because its
box faces have `lightofs=-1`. A separate generated map now supplies six
authored lightmap ranges for the inline box:

- `tools/renderer_parity/generate_bmodel_lightmapped_instance_fixture.py`
  produces `maps/worr_fr01_bmodel_instances_lightmapped.bsp`.
- The common BSP generator assigns the legacy Y/-Z extents for all six box
  faces and writes style 0 for every authored inline lightmap.
- `fr01_bmodel_instances_unlightmapped_manifest.json` remains the regression
  gate for the texture-replace correction.
- `fr01_bmodel_instances_lightmapped_manifest.json` gates the actual
  static-light receiver path; `fr01_perf_bmodel_lm.cfg` is intentionally short
  enough for the engine command-file limit.

The generated map is listed in `DEFAULT_LOOSE_ASSET_PATHS`. This matters for
the no-zlib headless runtime: packaging it only inside `pak0.pkz` caused the
`map` command to fail even though screenshot capture continued.

## Native fast path

The existing GPU inline-BSP fast fragment module is selected only for opaque
faces with an authored lightmap and no active fullbright, shadow-sun, or
dynamic-light receiver state. It preserves the static lightmap, brightness,
intensity, and fog formula while omitting the general material/receiver work
that cannot contribute under those conditions.

Because its native submission gate also excludes transparent and alpha-tested
materials, the specialized fragment has no alpha discard. That restores early
depth testing for covered opaque inline-BSP fragments without weakening the
complete fallback contract. The repeated paired timing and visual evidence is
in `docs-dev/renderer/vulkan-bmodel-fast-lit-early-depth-2026-07-16.md`.

`vk_bmodel_fast_lit` is an archived Vulkan-only switch, default `1`:

- `1`: select the specialized opaque inline-BSP pipeline when its per-frame
  eligibility checks pass.
- `0`: force the existing complete native Vulkan pipeline. This is a focused
  regression and driver-workaround control, not an OpenGL fallback.

The per-frame `VK_STATS` telemetry records `entity_fast_lit_draws` and
its `entity_fast_lit_no_fog_draws` subset, plus `world_fast_lit_draws`
and its `world_fast_lit_no_fog_draws` subset,
plus static-world candidate and blocker counts for
the cvar, fullbright, receiver-lighting, pipeline, and material gates; the
analyzer reports their mean, p50, and p95 values. The latest coverage result
is recorded in
`docs-dev/renderer/vulkan-static-world-fast-lit-coverage-2026-07-16.md`.
The matching inline-BSP no-fog receiver evidence is in
`docs-dev/renderer/vulkan-bmodel-fast-lit-no-fog-2026-07-16.md`.

## Unlightmapped texture-replace specialization

The original unlightmapped correction retained the complete entity fragment
program after fixing its OpenGL-equivalent material contract. Vulkan now
specializes that exact opaque branch natively. A submission-only marker selects
small fog-aware and no-fog modules that do only the base-material,
optional-intensity, and applicable-fog work. The gate permits the otherwise
inert fullbright/shadow/dynamic-light entity subset because the OpenGL
texture-replace branch never reads it; all effect-bearing flags remain on the
complete native shader.

The current-binary 100-sample dense-grid pair selects 16 no-fog
texture-replace draws per frame and reduces opaque-entity GPU p50 from
0.5055 ms to 0.1620 ms (68.0%), while retaining 18 draws and 4,800 upload
bytes. A generated fogged unlightmapped 37-instance fixture selects 16
fog-aware texture-replace draws and zero no-fog draws, with OpenGL/Vulkan
maximum RGB error 1/1/1 over 218,400 crop pixels. The detailed evidence is in
docs-dev/renderer/vulkan-bmodel-texture-replace-fast-path-2026-07-16.md.
This remains a localized entity specialization, not a renderer-wide
GPU-budget acceptance.

## Static-world material specializations

The same fixed view exposed a remaining full-screen world cost after the
inline-BSP gain. Its background has an authored lightmap and `_glow` companion,
so it correctly did not qualify for the ordinary non-glow static-light
pipeline. Vulkan now has native fog-aware/no-fog companions for the
lightmapped receiver classes, controlled by archived default-on
`vk_world_fast_lit` and `vk_world_fast_lit_no_fog`:

- plain authored lightmaps: base texture, lightmap, brightness/modulate,
  intensity, and fog;
- authored lightmap plus glow map: the same formula plus OpenGL-equivalent
  glow-alpha lift of the lightmap before brightness/modulate;
- unlightmapped texture-replace: base texture, intensity, and fog only.

Each class falls back to the complete native Vulkan opaque pipeline if the
main switch is `0` or a driver rejects its optional pipeline. The
lightmapped classes retain their fog-aware shader whenever global or height
fog is active, and otherwise select their no-fog companion unless
`vk_world_fast_lit_no_fog 0` explicitly requests the fog-aware control. The
glow-map variant
intentionally keeps the legacy double-sided world raster state: some legacy
BSPs have face winding inconsistent with their plane flag, while the general
world path is double-sided. This preserves the visual contract; its fragment
specialization supplies the measured saving. The focused no-fog result is
recorded in
`docs-dev/renderer/vulkan-static-world-fast-lit-no-fog-2026-07-16.md`.

## Validation evidence

All captures used `win_headless 1`, disabled input/audio, a 960x720 fixed
view, and validation-enabled Vulkan.

| Gate | Result |
| --- | --- |
| Unlightmapped grid, `r_fullbright 0` | 218,400 compared pixels; zero RGB error; matching 179,742-pixel material mask. |
| Authored-lightmapped grid, default fast path | 218,400 compared pixels; zero RGB error; matching 179,742-pixel material mask. |
| Authored-lightmapped grid, forced fallback | 218,400 compared pixels; zero RGB error; matching 179,742-pixel material mask. |
| Native fast-path timing | 110 post-warmup samples; 16 specialized entity draws/frame with `vk_bmodel_fast_lit=1`, zero with `=0`. |
| Static-world glow-map timing | 110 post-warmup samples; one specialized world draw/frame with `vk_world_fast_lit=1`, zero with `=0`; default and forced-world-fallback visual scenes retain zero RGB error. |

The paired timing runs used Intel Iris Xe Graphics, driver
`31.0.101.5590 (2024-06-10)`, and held the fixture, renderer controls, sample
count, and hardware constant. Only `vk_bmodel_fast_lit` differed.

| Vulkan p50 metric (ms) | Fallback (`0`) | Specialized (`1`) | Change |
| --- | ---: | ---: | ---: |
| Completed GPU frame | 1.953 | 1.189 | -39.1% |
| GPU scene phase | 1.925 | 1.160 | -39.8% |
| Opaque entity GPU phase | 1.109 | 0.346 | -68.8% |
| CPU frame | 1.019 | 1.015 | -0.4% |

Both variants held 18 Vulkan draws and 4,800 upload bytes per frame. OpenGL's
full-frame GPU p50 was about 0.403 ms in these captures, so further Vulkan
scene-cost work remains necessary before accepting a renderer-wide GPU budget.

The final static-world measurement used the same map, renderer controls,
hardware, driver, validation layer, and 110 post-warmup samples; only
`vk_world_fast_lit` differed. The inline-BSP specialization remained active in
both rows (16 entity fast draws/frame), isolating the world receiver change.

| Vulkan p50 metric (ms) | World fallback (`0`) | World specialized (`1`) | Change |
| --- | ---: | ---: | ---: |
| Completed GPU frame | 1.182 | 0.775 | -34.4% |
| GPU scene phase | 1.153 | 0.746 | -35.3% |
| Opaque world GPU phase | 0.806 | 0.393 | -51.2% |
| CPU frame | 0.555 | 0.589 | +6.1% |

The specialized row is visually exact and has the same 18 draws and 4,800
uploaded bytes per frame. OpenGL's corresponding full-frame GPU p50 was
0.401 ms, so this narrows the focused lane but is not renderer-wide Vulkan GPU
parity or an accepted cross-renderer budget.

## Reproduction

```powershell
python tools/renderer_parity/generate_bmodel_lightmapped_instance_fixture.py --asset-root assets
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py --assets-dir assets --install-dir .install --base-game basew

python tools/renderer_parity/run_capture_matrix.py `
  --install-dir .install --executable .install/worr_x86_64.exe `
  --manifest assets/renderer_parity/fr01_bmodel_instances_lightmapped_manifest.json `
  --vulkan-validation
```

For timing, run `tools/renderer_parity/run_renderer_perf_capture.py` twice
with `renderer_parity/fr01_perf_bmodel_lm.cfg` and launch cvars
`vk_bmodel_fast_lit=0/1` for the inline-BSP receiver or
`vk_world_fast_lit=0/1` for the world receiver, then pass each paired log set
to `analyze_renderer_perf.py`. Require matching fixture/configuration and
hardware/driver manifests before comparing results.

## Regression coverage

- test_generate_bmodel_instance_fog_fixture.py validates the generated fogged
  unlightmapped dense-instance asset.

- `test_generate_bmodel_lightmapped_instance_fixture.py` verifies generated
  face offsets and style records.
- `test_vulkan_gpu_bmodel_submission_source.py` verifies the native
  texture-replace marker and default-on fast-path control.
- `test_vulkan_world_fast_lit_source.py` verifies the plain-lightmap,
  glow-map, and texture-replace native world specializations and their
  generated SPIR-V modules.
- `test_package_assets.py` verifies loose staging of the generated map.
- Targeted timing, parity, SPIR-V validation, and the Vulkan DLL build all
  passed during this change.
