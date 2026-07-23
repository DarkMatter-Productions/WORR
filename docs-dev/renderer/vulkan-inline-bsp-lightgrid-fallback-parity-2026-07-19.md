# Inline BSP Model-Light Fallback Parity (2026-07-19)

Project task: `FR-01-T14`

Status: complete. Native Vulkan's disabled-lightgrid fallback now has direct
headless evidence for a transformed inline BSP receiver, not only the world
receiver and no-receiver branches.

## Why this follow-up matters

`VK_World_LightPointEx` independently mirrors OpenGL's fallback order: it
traces the world, then tests compatible inline BSP entities with
`BSP_TransformedLightPoint`, retaining the nearest lightmapped hit. Earlier
source-level coverage asserted that native path, but the runtime lightgrid
fixture only supplied a world lightmap receiver. That meant a regression in
the inline-model branch could have remained structurally present while lacking
pixel evidence.

## Fixture design

`tools/renderer_parity/generate_lightgrid_fixture.py` now emits
`assets/maps/worr_fr01_lightgrid_inline_receiver.bsp`. It deliberately has no
world lightmap. Its one inline model is a normally transformed brush entity at
the stock `dmspot` MD2 position and contributes a blue authored lightmap from
the horizontal -Z face.

The generic BSP fixture builder gained the narrow
`bmodel_light_receiver` option. It gives model 1 a detached BSP node whose
face range contains that horizontal face and gives the node two private leaves;
sharing the world leaves would make `BSP_SetParent` reject the map as cyclic.
The receiver face has X/Y 16-unit lightmap axes and dimensions appropriate for
its local bounds, so the transformed origin hits a valid sample.

The receiver is marked `RF_VIEWERMODEL`, rather than made translucent. Both
renderers still keep that entity in the refdef used by the light query, while
their ordinary entity submissions intentionally skip rendering it. This is
important because OpenGL schedules inline BSP models before its alpha entity
passes, so an alpha-zero bmodel is not a reliable invisible receiver.

## Evidence

The final staged, headless matrix is rooted at
`.tmp/renderer-parity/lightgrid-inline-receiver-final4/` and uses Vulkan
validation. Its inline state has these results over the standard 84,000-pixel
crop:

- OpenGL/Vulkan maximum RGB error: `0 / 0 / 1`.
- Pixels above RGB error 16: `0`.
- Blue receiver mask: 30,559 pixels in each backend, IoU `1.0`.
- Inline-blue to no-receiver-white transition: 46,641 changed pixels above
  error 16 in each backend.

The pre-existing green grid, red world receiver, and white no-receiver states
also remain strict gates. The full matrix completed without process failures
or Vulkan validation diagnostics.

## Validation

```text
python tools/renderer_parity/generate_lightgrid_fixture.py --asset-root assets --json
python -m unittest test_generate_lightgrid_fixture test_shared_lightgrid_control_source
python -m unittest tools.test_package_assets
meson compile -C builddir-win worr_x86_64 worr_engine_x86_64 \
  worr_opengl_x86_64 worr_vulkan_x86_64
python tools/refresh_install.py --build-dir builddir-win --install-dir .install
python tools/renderer_parity/run_capture_matrix.py \
  --install-dir .install \
  --manifest assets/renderer_parity/fr01_lightgrid_manifest.json \
  --run-root .tmp/renderer-parity/lightgrid-inline-receiver-final4 \
  --timeout 90 --vulkan-validation
```

All automated launches are headless, use isolated runtime homes, and disable
input.
