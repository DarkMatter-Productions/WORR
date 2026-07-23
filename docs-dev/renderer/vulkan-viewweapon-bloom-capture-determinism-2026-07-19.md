# Vulkan Viewweapon Bloom Capture Determinism

Date: 2026-07-19

Task IDs: `FR-01-T12`, `FR-02-T05`

## Outcome

The retained native Vulkan/OpenGL first-person shell-bloom gate is again
deterministic and strict. This changes the test fixture only; it does not add
an OpenGL fallback or alter native Vulkan rendering.

## Finding

The ordinary instant-Quad receiver used frame-count waits but left game time
realtime. OpenGL and Vulkan run in separate headless processes and can spend
different wall-clock time in renderer initialization and map loading. The
held weapon then reaches a different animated viewmodel pose before the
screenshot. Its large screen-space silhouette difference was capable of
failing the 212,800-pixel crop despite both native renderer paths correctly
implementing the weapon projection and depth-hack bloom replay.

The transparent-warp companion did not have this weakness: it already used a
fixed time and paused the liquid phase before capture.

## Fixture contract

`assets/renderer_parity/fr01_viewweapon_shell_bloom_emission.cfg` now:

- explicitly sets `cl_gunfov 90`, zero gun offsets, and `hand 0`;
- sets `fixedtime 16` before `map`; and
- restores `fixedtime 0` after the screenshot.

`tools/renderer_parity/test_generate_viewweapon_shell_bloom_fixture.py`
asserts that pose/timing contract as well as the retained strict manifest
limits. This makes later edits that remove the fixed timestep fail the normal
fixture regression suite.

## Validation

After a canonical `.install` refresh, the hidden native-surface captures ran
with the Vulkan validation layer:

```text
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_viewweapon_shell_bloom_emission_manifest.json --run-root .tmp/renderer-parity/fr01-viewweapon-shell-bloom-fixedtime-final --timeout 180 --vulkan-validation
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_viewweapon_shell_bloom_refraction_manifest.json --run-root .tmp/renderer-parity/fr01-viewweapon-shell-bloom-refraction-current --timeout 180 --vulkan-validation
```

The direct-emission crop measured maximum RGB `103 / 90 / 75`, MAE
`0.057890 / 0.049892 / 0.050822`, and 73 pixels over RGB error 16
(`0.034305%`), satisfying its unchanged MAE `0.1` and over-threshold `0.1%`
limits. The refraction crop remained maximum RGB `1 / 1 / 1` with no pixel
over one. Both runs completed with no process failure or Vulkan validation
diagnostic.

## Boundary

This makes the existing first-person receiver dependable in the nightly
parity lane. Broader weapon, animation, and powerup scene variety remains
under `FR-02-T05`.
