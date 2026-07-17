# Native Vulkan Multi-Level Bloom Parity

Date: 2026-07-16

Task IDs: `FR-01-T12`, `FR-01-T15`

Status: implemented and validation-tested. Vulkan now provides OpenGL's
multi-level bloom composition natively; it does not route any Vulkan frame
through OpenGL.

## Change

OpenGL exposes `gl_bloom_levels` (one through six). After its blur result is
copied into a mipmapped texture, the final shader blends levels 0..N-1 with
normalised weights `1, 1/2, 1/4, ...`. The previous Vulkan path only exposed
the level-zero blur image, so it could not reproduce the wider halo produced
by values above one.

The Vulkan renderer now exposes the archived `vk_bloom_levels` cvar, default
`1`, and follows the same maximum-six-level / smallest-dimension clamp. The
base ping target owns a six-or-fewer-level native image; its pong target and
all DOF targets remain single-level. A level-zero render view keeps the
prefilter and separable blur passes isolated from uninitialised higher levels,
while the final post-process descriptor uses a full-chain sampling view.

After the final level-zero blur, Vulkan transitions only the requested levels,
uses `vkCmdBlitImage(..., VK_FILTER_LINEAR)` to build them, and returns them
to shader-read layout before the final composite. It also transitions unused
levels to shader-read layout once so the full-chain descriptor is valid on all
frame slots. The final shader uses explicit `textureLod` samples and OpenGL's
same bounded six-level, halving-weight normalisation. Swapchain formats without
linear source/destination blit support retain level-one native bloom rather
than falling back to OpenGL.

The shared clamp sampler now permits view-selected mip levels. Existing UI and
post-process views which expose only level zero retain their original sampling
range.

## Visual and validation evidence

`fr01_model_shell_bloom_levels_manifest.json` is a dedicated level-three,
threshold-isolated shell receiver. Its validation-enabled capture at
`.tmp/renderer-parity/bloom-mip-levels-r3` passed:

| Check | Result |
| --- | ---: |
| Crop maximum RGB difference | `6 / 10 / 5` |
| Mean RGB difference | `1.44175 / 2.52407 / 0.07135` |
| Pixels above RGB error 16 | `0` |
| Saturated shell-core IoU | `1.0` (`53,091` pixels on each backend) |
| Multi-level halo IoU | `0.98643` (`1,768` GL / `1,744` Vulkan pixels) |

The Vulkan process log contains no validation errors or warnings. The fixture
retains a bounded, level-specific error allowance rather than reusing the
single-level shell threshold: every compared pixel remains below the material
error limit and the halo-specific IoU/count contracts prove the added wide
halo is present.

## Performance evidence

Both telemetry captures are headless 960x720 runs on Intel Iris Xe Graphics,
driver `31.0.101.5590`, with 120 samples per renderer and 20 discarded warmup
samples. The following values are the resulting 100-sample Vulkan means;
they are a quality-mode cost comparison, not a cross-renderer performance
claim.

| Shell bloom mode | Complete GPU | GPU post | CPU | Draws |
| --- | ---: | ---: | ---: | ---: |
| `vk_bloom_levels 1` | `1.42587 ms` | `0.52069 ms` | `1.47982 ms` | 9 |
| `vk_bloom_levels 3` | `2.67156 ms` | `1.09102 ms` | `1.46449 ms` | 9 |
| Level-three delta | `+1.24569 ms` | `+0.57033 ms` | `-0.01533 ms` | 0 |

The hardware blits add no fullscreen draw submissions, and the compatibility
default remains level one. The wider-halo quality mode is deliberately
opt-in; its measured GPU cost means it is not promoted as a performance
improvement. The next bloom performance work should reduce the full-screen
postprocess cost without removing this parity control.

Provenance roots:

- `.tmp/renderer-parity/fr01-renderer-perf-bloom-shell-level-one-current`
- `.tmp/renderer-parity/fr01-renderer-perf-bloom-shell-levels`

## Verification

```text
python tools/gen_vk_world_spv.py --validate
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/package_assets.py
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python -m unittest tools.renderer_parity.test_vulkan_bloom_source
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_model_shell_bloom_levels_manifest.json --run-root .tmp/renderer-parity/bloom-mip-levels-r3 --vulkan-validation
python tools/renderer_parity/run_renderer_perf_capture.py --install-dir .install --config renderer_parity/fr01_renderer_perf_bloom_shell_levels.cfg --fixture assets/renderer_parity/fr01_model_shell_bloom_levels_manifest.json --scenario-id fr01-bloom-shell-three-levels --run-root .tmp/renderer-parity/fr01-renderer-perf-bloom-shell-levels --hardware-id "Intel Iris Xe Graphics" --driver "31.0.101.5590" --vulkan-validation --min-samples 100
```

All runtime launches use `win_headless 1` with input disabled.
