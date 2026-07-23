# Vulkan GPU-MD5 Multi-Joint Runtime Parity

Date: 2026-07-20

Task IDs: `FR-01-T14`, `FR-01-T15`

Status: complete for representative multi-joint normal-path coverage. This
extends validation coverage; it does not change MD5 skinning semantics or claim
a renderer-wide timing result.

## Outcome

The ordinary GPU-MD5 gate previously proved the normal one-joint dmspot
replacement path. The deterministic `worr_fr01_model_md5_multijoint` fixture
now submits the registered `players/male/tris.md2` replacement through the same
ordinary `misc_model` game-entity route. The model has one mesh, 17 joints, and
198 animation frames. Its map entity selects source frame 100, exercising a
non-zero frame's normal CPU joint-palette resolve and native Vulkan weighted
vertex reconstruction.

The fixture latches `vk_md5_gpu_skinning=1` before renderer registration while
keeping ordinary MD5 loading/use enabled in both renderers. Bloom and cel
replays stay disabled so the `VK_STATS` GPU-MD5 draw/instance counter is an
unambiguous submission signal. It remains fully native Vulkan; no path routes
through OpenGL.

## Validation

The final headless validation capture uses a 9,000-pixel model crop and its
visible magenta player-skin mask. Magenta is the shared resolved replacement
appearance in this isolated asset set, so it gives a high-contrast shape gate
without introducing renderer-specific art or hooks.

```text
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_model_gpu_md5_multijoint_manifest.json --run-root .tmp/renderer-parity/model-gpu-md5-multijoint-final-staged --timeout 90 --vulkan-validation --json-output .tmp/renderer-parity/model-gpu-md5-multijoint-final-staged/results.json
```

The paired result passes with no process failure, VUID, validation diagnostic,
GPU-residency fallback, or GPU-MD5 submission fallback:

| Measure | Result |
|---|---|
| Model crop | 9,000 pixels; maximum RGB `255 / 0 / 255`, mean absolute RGB `1.762556 / 0 / 1.762556`, and 101 pixels (`1.122222%`) above RGB error 16. |
| Magenta multi-joint skin mask | OpenGL 1,145 pixels; Vulkan 1,215 pixels; 5.761317% count delta; IoU `0.942387`. |
| Native runtime evidence | Vulkan loads `players/male/tris.md2` as one mesh, 17 joints, and 198 frames, then reports `entity_gpu_md5_draws=1` and `entity_gpu_md5_instances=1`. |
| Upload telemetry | Vulkan reports 1,360 entity-domain upload bytes for the fixture frame; this is telemetry, not a timing claim. |

All launches use `win_headless 1` with client input disabled; no interactive
client window was started. The final staged Vulkan DLL SHA-256 is
`BCCF2E7408413CB3E2C2F7EFCE48687BBA23DA4A313CA5A86172478D8BC3CEA7`, matching
the build output.

## Fixture ownership

`generate_model_md5_multijoint_fixture.py` owns the BSP and the paired test
asserts its model, non-zero frame, normal GPU-MD5 launch gate, and manifest
shape probe. The generated map is mirrored as a required loose staged asset for
runtimes without zlib package support.

## Remaining work

The gate proves representative multi-joint normal-path parity, not animation
interpolation across a moving entity, item-colourize/outline fallbacks, a
general transient ring, indirect submission, or a representative-map GPU
budget. Those remain under `FR-01-T14` and `FR-01-T15`.

No end-user documentation changed.
