# Native Vulkan GPU MD5 Runtime Parity Gate

Date: 2026-07-19

Task ID: `FR-01-T14`

Status: the normal eligible GPU-MD5 route now has a durable, validation-clean
OpenGL/Vulkan visual gate and native runtime-path evidence. This closes the
previous runtime-evidence gap for the implemented GPU skinning slice; it does
not establish a renderer-wide performance budget or remove CPU joint-palette
interpolation.

## Scope

`assets/renderer_parity/fr01_model_gpu_md5.cfg` uses the existing generated
`worr_fr01_model_glowmap` map and its ordinary `misc_model` request for
`models/objects/dmspot/tris.md2`. Both renderers load and use the available
MD5 replacement. The manifest places `vk_md5_gpu_skinning 1` in launch cvars,
before model registration and the cvar's latched renderer initialization.

The scene deliberately disables bloom and Vulkan cel-shading and does not set
an outline or item-colourize presentation flag. Those conditions leave the
model on the ordinary eligible GPU-MD5 route rather than either intentional
CPU fallback. The map, entity, and model registration are normal game paths;
the fixture does not inject renderer-only geometry.

## Native-path telemetry

`vk_stats` now includes `entity_gpu_md5_draws` and
`entity_gpu_md5_instances`. `VK_Entity_DrawBatch` increments these values only
after it selects the indexed native GPU-MD5 layout and issues its Vulkan draw.
They are draw counters, so optional replay passes can add to them in a general
scene. This fixture disables those passes, making the expected active model
draw unambiguous.

The final Vulkan frame reported:

```text
entity_gpu_md5_draws=1 entity_gpu_md5_instances=1
```

The process log also loaded the dmspot replacement as one mesh, one joint, and
one frame. It contains no `GPU MD5 residency unavailable` diagnostic, VUID, or
Vulkan validation error.

## Paired visual result

The staged, headless 960x720 capture ran:

```text
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_model_gpu_md5_manifest.json --run-root .tmp/renderer-parity/fr01-model-gpu-md5-initial --timeout 90 --vulkan-validation --json-output .tmp/renderer-parity/fr01-model-gpu-md5-initial/results.json
```

The 34,100-pixel model crop passed with maximum RGB error `1 / 1 / 1`, mean
absolute RGB error `0.00099707 / 0.00038123 / 0.00026393`, and zero pixels
above RGB error 16. The bright skin-glow mask contains exactly 1,864 pixels in
each backend at IoU `1.0`.

## Local upload control

The same headless scene was also launched with latched
`vk_md5_gpu_skinning 0`. Its matching frame recorded no GPU-MD5 draws and
17,480 entity upload bytes. The GPU-enabled frame recorded one GPU-MD5 draw,
one instance, and 336 entity upload bytes. That is a 17,144-byte (98.08%)
reduction for this fixed one-mesh scene, while static bind-pose data remains
resident in native device-local buffers.

This is a transfer-volume observation, not a timing claim: the two standalone
frames are not a multi-sample performance benchmark and CPU joint
interpolation remains. `FR-01-T15` retains responsibility for representative
multi-joint workloads and accepted CPU/GPU budgets.

The follow-up normal-path gate covers the registered 17-joint, 198-frame
`players/male/tris.md2` replacement at source frame 100. It reports one native
GPU-MD5 draw/instance with a paired 1,145/1,215-pixel visible skin mask at IoU
`0.942387`; see `vulkan-gpu-md5-multijoint-runtime-parity-2026-07-20.md`.

## Regression and staging

```text
python -m unittest tools.renderer_parity.test_model_gpu_md5_fixture tools.renderer_parity.test_vulkan_gpu_md5_submission_source
meson compile -C builddir-win worr_vulkan_x86_64
python tools/refresh_install.py --build-dir builddir-win --install-dir .install
```

All checks passed. The staged Vulkan DLL SHA-256 is
`BCC2FC1D776518EF25DB82C87259DEF9764F5F09A2B424E1DB1B1CFF460560CA`, matching
the build output. All client launches use `win_headless 1` with input disabled;
no interactive client window was opened.

No end-user behavior or documentation changed.
