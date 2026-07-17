# Native Vulkan Blur-Kernel Uniform Optimization

Date: 2026-07-16

Task IDs: `FR-01-T12`, `FR-01-T15`

Status: implemented, visually validated, and retained. Native Vulkan now
precomputes paired Gaussian blur coefficients once for each reusable frame slot
instead of evaluating Gaussian exponentials in every bloom or depth-of-field
fragment.

## Change

The retained paired-bilinear blur path in `vk_bloom.frag` already cut texture
samples approximately in half. Unlike OpenGL's generated shader, however, it
still evaluated each pair's Gaussian weights and sub-texel offset per pixel.

Vulkan now builds the same unnormalised pairs on the CPU from the shared
sigma/radius equation. Each pair is a std140 `vec4`: bilinear sample offset,
pair weight, and two padding values. The shader indexes the bounded 51-pair
table and performs the same weighted samples and normalization as before. The
51-entry maximum covers the clamped radius of 50, including an unpaired final
tap.

Each bounded Vulkan frame context owns one host-coherent uniform buffer and
descriptor set. The renderer only updates the current slot after its fence has
been waited and only when its effective sigma changes. Set 0 remains the
existing scene/emission descriptor contract; the kernel is native Vulkan set 1
and is bound only by the blur pipeline. This keeps final composition, scene
ownership, pass count, and all OpenGL paths unchanged.

## Visual evidence

The complete validation-enabled eight-scene depth-of-field matrix at
`.tmp/renderer-parity/dof-uniform-kernel-full` passed. Every active 307,200
pixel crop has maximum RGB error `1 / 1 / 1` with zero pixels beyond one; the
four `r_dof=0` controls remain pixel-identical.

The validation-enabled active translucent-shell bloom gate at
`.tmp/renderer-parity/bloom-uniform-kernel-smoke` also passed its existing
strict contract: maximum RGB error `6 / 10 / 5`, no pixels beyond error 16,
core IoU `1.0`, and halo IoU `0.98375`. No Vulkan validation errors were found
in either visual or performance run.

## Performance evidence

All values are warmup-trimmed means from the same 960x720 Intel Iris Xe,
Windows 11, driver `31.0.101.5590 (2024-06-10)` lanes used by the preceding
paired-tap measurements. They are diagnostic phase timings, not renderer-wide
OpenGL superiority claims.

| Native Vulkan lane | Paired taps | Uniform coefficients | Change |
| --- | ---: | ---: | ---: |
| Active DOF GPU post mean | 0.70000 ms | 0.67683 ms | -3.3% |
| Active DOF complete GPU mean | 1.33777 ms | 1.37933 ms | +3.1% |
| Active shell bloom GPU post mean | 0.81801 ms | 0.76663 ms | -6.3% |
| Active shell bloom complete GPU mean | 2.18236 ms | 2.13732 ms | -2.1% |

The DOF post phase improves while its complete timestamp is slightly higher;
that mixed complete result is not promoted to a budget. The active shell bloom
lane improves both its targeted post phase and complete GPU time with unchanged
nine total / four post-process draws, so the implementation is retained. The
next measured target remains selective authored-emission replay and
post-process render-pass submission, not a visual-quality reduction.

The current provenance roots are
`.tmp/renderer-parity/fr01-renderer-perf-dof-uniform-kernel` and
`.tmp/renderer-parity/fr01-renderer-perf-bloom-shell-uniform-kernel`.

## Verification

```text
python tools/gen_vk_world_spv.py --validate
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/package_assets.py
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python -m unittest tools.renderer_parity.test_vulkan_bloom_source tools.renderer_parity.test_vulkan_dof_control_source
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --executable .install/worr_x86_64.exe --manifest assets/renderer_parity/fr01_dof_manifest.json --run-root .tmp/renderer-parity/dof-uniform-kernel-full --vulkan-validation
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --executable .install/worr_x86_64.exe --manifest assets/renderer_parity/fr01_model_shell_bloom_emission_manifest.json --run-root .tmp/renderer-parity/bloom-uniform-kernel-smoke --vulkan-validation
```

All runtime collection is hidden/headless (`win_headless 1`) with input
disabled.
