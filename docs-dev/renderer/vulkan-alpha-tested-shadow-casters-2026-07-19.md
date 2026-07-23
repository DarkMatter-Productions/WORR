# Alpha-Tested Shadow Casters for Native Vulkan and OpenGL

Date: 2026-07-19

Task ID: `FR-02-T14`

## Outcome

Alpha-tested BSP materials now use the same alpha cutoff in the shadow caster
pass as in the visible material paths: a texel with alpha less than or equal
to `0.666` does not write shadow depth or moments. This covers static-world
surfaces and transformed inline-BSP (`func_*`) model surfaces.

The change is implemented independently in both raster backends. Native
Vulkan remains a Vulkan-only path; no shadow draw, material lookup, or
validation path is routed through OpenGL.

## Native Vulkan design

The normal shadow page remains efficient for the common opaque case: all
opaque world and model caster geometry is retained as one descriptor-free
draw for the page. Only alpha-tested BSP faces are deferred into material
subdraws. Each subdraw carries normalized source texture coordinates and the
existing image descriptor for its base material, and uses a small native
fragment module that discards transparent texels before either depth output or
moment output.

There are two native alpha modules, one depth-only and one moment-producing.
Their use matches the current page storage family, including EVSM moment
generation. The shadow backend owns an explicitly equivalent three-sampler
material descriptor layout because it initializes before Vulkan UI; that
layout is compatible with the image descriptor sets later allocated by UI and
does not introduce an initialization-order dependency.

Inline BSP shadow emission now preserves both transformed positions and the
untransformed source points required to calculate stable material UVs. Alias
and MD5 casters stay on the opaque stream unless a future model-material
contract requires a cutout path.

## OpenGL parity

OpenGL gained matching depth and moment alpha programs plus a compact
position/UV shadow vertex-array layout. It continues to batch all opaque
casters through the legacy position-only path. An alpha BSP face flushes that
opaque stream, binds its image, draws its cutout triangles, and then resumes
the opaque program. Thus the cost is limited to materials that actually need
the discard semantics and both renderers use the same threshold.

## Durable coverage

`assets/renderer_parity/fr01_alpha_shadow.cfg` reuses the generated alpha
fixture with a player flashlight and EVSM shadows. The map has a low-alpha
world wall between the flashlight and its backdrop, plus an alpha-tested
inline BSP box with opaque texels. It exercises the non-casting cutout and
the casting inline-material cases in one headless capture.

`assets/renderer_parity/fr01_alpha_shadow_manifest.json` and
`tools/renderer_parity/test_vulkan_shadow_alpha_caster_fixture.py` keep that
fixture contract current. `tools/renderer_parity/test_vulkan_shadow_alpha_caster_source.py`
also guards the native shader cutoff, material subdraw boundary, inline BSP
UV provenance, and matching OpenGL implementation.

## Verification

Both renderer DLLs compiled and the staged install was refreshed with the
current 535-file asset package:

```text
ninja -C builddir-win worr_opengl_x86_64.dll worr_vulkan_x86_64.dll
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py --assets-dir assets --install-dir .install
```

The dedicated capture ran without an interactive window, input, or audio:

```text
python tools/renderer_parity/run_capture_matrix.py \
  --install-dir .install \
  --manifest assets/renderer_parity/fr01_alpha_shadow_manifest.json \
  --run-root .tmp/renderer-parity/fr01-alpha-shadow \
  --timeout 180 --vulkan-validation
```

The 560x420 crop is exact across OpenGL and Vulkan: all 235,200 pixels have
zero RGB error. The Vulkan process log contains no `VUID`, validation error,
or renderer error. A paired headless EVSM flashlight-owner smoke also
completed cleanly with 25 shared frontend pages; Vulkan retained its 32-layer
active capacity bucket.

## Remaining FR-02-T14 work

This closes the alpha-tested caster-material slice. Resolution buckets remain
separate work. The shared unchanged-page content-reuse, capability-correct
sampler, delayed capacity-shrink, and transactional-replacement slices are
documented in their respective 2026-07-19 implementation logs.
