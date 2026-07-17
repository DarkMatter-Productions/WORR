# Vulkan Scaled-Scene Blit Fast Path

Date: 2026-07-16

Tasks: `FR-01-T12`, `FR-01-T15`

## Outcome

Native Vulkan no longer invokes the sampled fullscreen final compositor for
the identity LDR resolution-scale case. When the 3D scene is below
presentation resolution and no visual post-process operation is needed, the
renderer now linearly blits its frame-slot offscreen scene image directly to
the acquired presentation image with `vkCmdBlitImage`. Native-resolution UI is
then recorded through the unchanged presentation overlay pass.

This is a Vulkan-only optimization. It does not redirect any Vulkan work to
OpenGL.

## Eligibility and correctness boundaries

The swapchain requests `VK_IMAGE_USAGE_TRANSFER_DST_BIT` only when the surface
advertises it. The fast path additionally requires the selected format to
support source/destination blits and linear filtering. Otherwise the existing
shader compositor is retained.

At record time the path is limited to a scaled non-float scene, an actual
scene/output extent mismatch, and no liquid refraction. The post-process
module declines it whenever waterwarp, colour correction, split toning, LUT,
HDR, bloom, depth of field, or CRT is active. Those controls remain on their
native Vulkan shader paths so their authored sampling, tone mapping, and
ordering are unchanged.

The blit brackets the offscreen target with color-attachment to transfer-source
and transfer-source back to color-attachment barriers. The acquired image is
transitioned from present (or undefined on first use) to transfer-destination,
then back to present before the existing overlay pass. This preserves the
overlay pass's layout contract and means HUD/menu rendering remains sharp at
swapchain resolution.

## Measured result

The fixed-half LDR visual gate exact-compares all 50,000 crop pixels under
Vulkan validation: every RGB error metric is zero and the backdrop probe has
IoU `1.0`. Evidence is in
`.tmp/renderer-parity/resolution-scale-fixed-native-blit-final/`.

The validation-enabled, adapter-matched 120-sample dense-inline-BSP capture
uses the same Iris Xe / i7-13700H / Windows 11 / Intel `31.0.101.5590`
environment as the immediately preceding shader-composite capture. After a
20-sample warm-up trim, Vulkan changes as follows:

| Vulkan metric | Shader composite | Native blit | Change |
|---|---:|---:|---:|
| GPU post mean | 0.49611 ms | 0.27388 ms | -44.8% |
| GPU frame mean | 1.01409 ms | 0.77369 ms | -23.7% |
| Fullscreen post draws | 1 | 0 | -1 |

The post phase still includes native presentation/overlay work, so its
remaining time is not attributed solely to the blit. The same capture's
OpenGL GPU-frame mean was 0.26110 ms; Vulkan is therefore still slower in this
scenario. This improvement is intentionally not converted into a cross-backend
GPU budget.

## Validation

- `ninja -C builddir-win worr_vulkan_x86_64.dll`
- `python tools/stage_install.py --build-dir builddir-win --install-dir .install`
- `python tools/renderer_parity/run_capture_matrix.py --install-dir .install --renderer opengl --renderer vulkan --manifest assets/renderer_parity/fr01_resolution_scale_fixed_manifest.json --run-root .tmp/renderer-parity/resolution-scale-fixed-native-blit-final --vulkan-validation`
- `python tools/renderer_parity/run_renderer_perf_capture.py --install-dir .install --config renderer_parity/fr01_renderer_perf_bmodel_instances.cfg --fixture assets/maps/worr_fr01_bmodel_instances.bsp --scenario-id fr01-bmodel-instance-grid-full-frame-gpu-half-native-blit --run-root .tmp/renderer-parity/fr01-renderer-perf-full-frame-gpu-half-native-blit --vulkan-validation --min-samples 100 --launch-cvar r_resolutionscale=1 --launch-cvar r_resolutionscale_fixedscale_w=0.5 --launch-cvar r_resolutionscale_fixedscale_h=0.5`

All runtime launches are hidden (`win_headless 1`) with input disabled.
