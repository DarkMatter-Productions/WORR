# Vulkan Depth-Hack View-Weapon Bloom Visual Parity

Date: 2026-07-16

Task ID: `FR-01-T12`

Status: native Vulkan now extracts direct bloom emission from depth-hacked
view-weapon shells and other supported depth-hack direct emitters without
routing the work through OpenGL. The ordinary no-refraction receiver has a
strict paired visual gate; a separate transparent-liquid fixture validates the
late compatible load pass with Vulkan validation enabled.

## OpenGL contract and original gap

`CL_AddViewWeapon` first submits the held model with `RF_DEPTHHACK` and
`RF_WEAPONMODEL`. When the player has a shell powerup, it submits a second,
translucent copy at 30% alpha with the corresponding shell flag. OpenGL's
alias path enables `GLS_BLOOM_GENERATE` for the entire mesh draw and selects
`GLS_BLOOM_SHELL` for shell/rim receivers. Its bloom MRT therefore receives
the shell's post-intensity diffuse colour through the same source-alpha blend
as the visible shell.

Vulkan already preserved the depth-hack viewport (`maxDepth = 0.25`) and the
weapon projection in its normal entity pass, but its bloom extractor rejected
every `depth_hack` and `weapon_model` batch. That silently omitted direct
shell/glowmap emission from first-person weapons.

## Native Vulkan implementation

`VK_Entity_BatchHasDirectBloomEmission` limits the new replay to direct,
authored sources: shell, rim, or glowmap batches. It continues to reject
flares, queries, outlines, item-colourize overlays, and ordinary alpha work.
The replay binds the existing dynamic/GPU-MD2/GPU-MD5 native extract pipelines
and the submitted descriptor/instance streams; it reuses
`frame_push_weapon` for `RF_WEAPONMODEL` and records with the same depth-hack
viewport as the visible draw. No CPU skin expansion, extra normal-scene draw,
or OpenGL path is involved.

For no-refraction frames the replay is appended to the existing cleared bloom
extract pass, so it costs only the eligible native draws. For transparent
liquid frames, the ordinary extract must happen before the liquid pass while
the view weapon is drawn afterwards. Vulkan therefore creates a compatible
colour-load bloom render pass and records it only after liquid rendering, only
when `VK_Entity_HasDepthHackBloomEmission()` finds an eligible submitted
batch. The source image transitions from shader-read to colour-attachment and
back through the normal post-process path. Ordinary bloom frames do not begin
this extra render pass.

The high-threshold first-person measurement showed that OpenGL's effective
depth-hack shell MRT response is one tenth of the otherwise equivalent world
shell source. `VK_ENTITY_VERTEX_BLOOM_DEPTHHACK` applies that `0.10`
normalization only inside the bloom extractor. It does not alter visible shell
colour, alpha, bloom cvars, world-shell receivers, or the generic blur kernel.

## Deterministic evidence

`generate_viewweapon_shell_bloom_fixture.py` owns
`maps/worr_fr01_viewweapon_shell_bloom.bsp`. Its worldspawn grants an instant
30-second Quad, so the normal cgame powerup policy deterministically creates
the real first-person blue shell; no renderer-only test entity is used. The
paired config disables unrelated presentation effects and uses bloom threshold
100 so the captured contribution is authored shell emission rather than
ordinary scene threshold bloom.

The strict manifest crop is `[400, 340, 560, 380]`. The retained normalized
run measured mean absolute RGB error `0.02055 / 0.01820 / 0.01680`; only 10 of
212,800 pixels exceeded RGB error 16 (`0.00470%`). Its limits are MAE `0.1`
per channel and `0.1%` pixels over 16. Before depth-hack normalization, the
same receiver measured `0.79435 / 0.67351 / 0.66109` MAE; the change is a
calibration of the native source, not a relaxed threshold.

The bloom-on/off control confirms a non-zero direct contribution in both
backends. On the high-threshold receiver, the OpenGL bloom-on/off MAE is
`0.00762 / 0.00664 / 0.00665`; normalized Vulkan is
`0.00534 / 0.00468 / 0.00479`. The matching contribution is deliberately
small, as it is a translucent first-person shell rather than the saturated
world-shell fixture.

`generate_viewweapon_shell_bloom_refraction_fixture.py` builds the same
powerup receiver into a `SURF_WARP | SURF_TRANS33 | SURF_FLOWING` map. The
config pauses before capture, preventing backend startup duration from
selecting different animated-warp phases. Its strict water-only crop is
`[160, 120, 320, 400]`: the retained paired run has maximum RGB error
`1 / 1 / 1`, MAE `0.47960 / 0.09655 / 0.43227`, and no pixel above RGB error
1. The view-weapon source keeps the late colour-load bloom render pass active
after liquid rendering; the validation-layer run has no process failure or
validation error. The crop deliberately excludes the animated first-person
model because the separate view-weapon manifest is the direct-emission visual
gate.

## Performance and safety

- The no-refraction route adds no render-pass transition or fullscreen work.
- The transparent-liquid load pass is gated by both active bloom and an
  eligible direct depth-hack source; it is not recorded for ordinary frames.
- It reuses submitted GPU-MD2/GPU-MD5 buffers, existing native pipelines, and
  existing per-frame bloom attachments.
- Render-pass/framebuffer compatibility and image-layout transitions were
  exercised under the Vulkan validation layer in the headless harness.

## Verification

```text
python tools/gen_vk_world_spv.py --validate
python tools/renderer_parity/generate_viewweapon_shell_bloom_fixture.py --validate
python tools/renderer_parity/generate_warp_flow_fixture.py --validate
python tools/renderer_parity/generate_viewweapon_shell_bloom_refraction_fixture.py --validate
python -m unittest discover -s tools/renderer_parity -p 'test_*.py'
python tools/test_package_assets.py
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_viewweapon_shell_bloom_emission_manifest.json --run-root .tmp/renderer-parity/fr01-viewweapon-shell-bloom-final --vulkan-validation
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_viewweapon_shell_bloom_refraction_manifest.json --run-root .tmp/renderer-parity/fr01-viewweapon-shell-bloom-refraction-final --vulkan-validation
```

All capture commands run with the repository's hidden native-surface mode:
they do not create an interactive client window, initialise client input, or
capture the mouse.

## Remaining boundary

This closes direct depth-hack shell/glowmap bloom replay for the supported
native entity layouts. Broader material-family emission, OpenGL's
mip-pyramid hierarchy, HDR/tone-mapping parity, and paired liquid-refraction
coverage beyond this transparent warp receiver remain open under `FR-01-T12`
and related renderer tasks.
