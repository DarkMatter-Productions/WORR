# Vulkan alpha-tested world and inline-BSP parity

Date: 2026-07-16

Task ID: FR-01-T15

## Outcome

Native Vulkan now uses OpenGL's alpha-test cutoff for static world geometry:
an alpha-tested texel is discarded when alpha is less than or equal to 0.666.
The prior Vulkan world shader discarded only alpha below 0.01, which was not
the GLS_ALPHATEST_ENABLE contract used by the OpenGL wall shader.

Inline BSP entities already used the matching alpha-test threshold. The new
owned scene verifies both native paths in one deterministic capture:

- the static world material has alpha 32 and must be discarded;
- a larger opaque static backdrop behind it must become visible; and
- an alpha-tested inline BSP model uses an opaque material and remains visible.

The final paired result is exact over all 235,200 crop pixels, with zero RGB
error, zero pixels over threshold, and no capture or Vulkan-validation
failure. Vulkan remains entirely native; no draw, texture, or validation path
is redirected to OpenGL.

## Native material contract

OpenGL generates the wall fragment test as diffuse alpha less than or equal to
0.666 followed by discard. The static Vulkan world shader now applies the
same comparison when VK_WORLD_VERTEX_ALPHATEST is present. The regular Vulkan
entity shader retains its existing equivalent inline-BSP test.

The opaque fast material paths deliberately do not accept alpha-tested
materials:

- the static texture-replace and lightmap specializations require an
  alpha-free vertex-flag set; and
- the inline-BSP texture-replace and fast-light gates reject alpha-tested
  source faces before their simplified fragments are selected.

Consequently the cutout scene also guards the fast-path boundary: cutouts use
the complete native fragment programs that retain discard semantics, while
ordinary opaque surfaces remain eligible for the optimized programs.

## Durable generated fixture

tools/renderer_parity/generate_alpha_test_fixture.py generates:

- maps/worr_fr01_alpha_test.bsp;
- textures/parity/fr01_alpha_bg in TGA and PNG form with alpha 32;
- textures/parity/fr01_alpha_backdrop in TGA and PNG form with alpha 255; and
- textures/parity/fr01_alpha_box in TGA and PNG form with alpha 255.

Both renderers prefer the PNG override for the map's nominal WAL names; the
TGA twins retain coverage of the same generated material data. The map gives
the front world and inline-BSP texinfos SURF_ALPHATEST. Its opaque backdrop is
unflagged and lies behind the front face.

The shared first-frame BSP generator now supports this optional second world
face. Its default output remains byte-identical for every existing fixture.
When a backdrop is requested it consistently extends the node face count,
leaf-face list, and world model face range. This matters because OpenGL
traverses node-owned faces, whereas Vulkan records the declared world-model
range. The alpha fixture test parses those records so a one-sided generated
topology cannot hide a renderer difference.

The backdrop is deliberately larger in world space than the nearer cutout
face. A fully discarded foreground otherwise reveals no defined scene color
outside a smaller distant plane, which is unsuitable for an image-parity
assertion. The final map therefore compares the renderer behavior that matters:
both discard the low-alpha foreground and reveal the same opaque static
receiver.

## Coverage and evidence

assets/renderer_parity/fr01_alpha_test_manifest.json has one hidden
960x720 scene with an isolated runtime home, disabled input/audio, and Vulkan
validation:

| Probe | OpenGL | Vulkan | Result |
| --- | ---: | ---: | --- |
| Whole 560x420 crop | 235,200 pixels | 235,200 pixels | Zero RGB error |
| Revealed static backdrop | 219,459 pixels | 219,459 pixels | Count delta 0%, IoU 1.0 |
| Visible alpha-tested inline BSP | 15,741 pixels | 15,741 pixels | Count delta 0%, IoU 1.0 |

The generated-asset test checks the two alpha-test texinfos, the unflagged
backdrop texinfo, the expanded node face count, and the PNG alpha values.
The Vulkan source test locks the OpenGL cutoff, static-world cutoff, entity
cutoff, flag propagation, and the inline-BSP general-path selection rule.

This upgrades world and inline-BSP cutout evidence from source-only to a
durable paired runtime gate. It does not close the broader alpha-cutout row:
fences still need dedicated coverage under `FR-02-T05`. Paletted sprite
cutouts now have a separate retained gate in
`vulkan-sprite-paletted-cutout-parity-2026-07-19.md`, and alpha-tested shadow
casters are covered by `vulkan-alpha-tested-shadow-casters-2026-07-19.md`.

## Reproduction

1. Generate and validate the related maps:

       python tools/renderer_parity/generate_bmodel_first_frame_fixture.py --asset-root assets --validate
       python tools/renderer_parity/generate_alpha_test_fixture.py --asset-root assets
       python tools/renderer_parity/test_generate_alpha_test_fixture.py

2. Regenerate native shaders, build, and refresh the staged install:

       python tools/gen_vk_world_spv.py --validate
       ninja -C builddir-win worr_vulkan_x86_64.dll
       python tools/stage_install.py --build-dir builddir-win --install-dir .install
       python tools/package_assets.py --assets-dir assets --install-dir .install

3. Run the manifest headlessly with validation:

       python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_alpha_test_manifest.json --run-root .tmp/renderer-parity/fr01-alpha-test --vulkan-validation
