# Native Vulkan Fence Cutout Parity

Date: 2026-07-19

Project task: `FR-02-T05`

The new deterministic `worr_fr02_fence_cutout` map places a binary-alpha
checker fence in front of an opaque backdrop using the ordinary `SURF_ALPHATEST`
world-material path. `generate_fence_cutout_fixture.py` owns the BSP and its
PNG/TGA material inputs; the map is also staged loose because headless map
loads require it outside the packaged asset archive.

`fr02_fence_cutout_manifest.json` compares a 560x420 (235,200-pixel) crop.
Its final validation-enabled OpenGL/Vulkan capture is RGB-exact, with zero
pixels above zero. The opaque fence mask is exactly 109,737 pixels and the
revealed backdrop mask exactly 109,722 pixels in both renderers, each at IoU
1.0. The check therefore covers both sides of the `<= 0.666` native fragment
discard threshold under regular repeated material sampling.

Validation:

```text
python tools/renderer_parity/generate_fence_cutout_fixture.py --asset-root assets --validate
python -m unittest tools.renderer_parity.test_generate_fence_cutout_fixture
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr02_fence_cutout_manifest.json --run-root .tmp/renderer-parity/fr02-fence-cutout-final --timeout 180 --vulkan-validation
```

No Vulkan VUID, validation error, or fatal diagnostic was present. This adds
the previously missing fence case to the existing world/inline-BSP,
paletted-sprite, and alpha-shadow cutout evidence; no Vulkan path is routed to
OpenGL.
