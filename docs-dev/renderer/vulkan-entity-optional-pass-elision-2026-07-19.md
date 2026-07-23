# Vulkan Entity Optional-Pass Elision

Date: 2026-07-19

Task ID: `FR-01-T14`

Status: native entity recording now skips empty depth-hack and outline pass
scans. The common gameplay path has fewer CPU-side batch iterations while all
active variants retain their existing Vulkan pipelines and ordering.

## Outcome

`VK_Entity_RecordPhase` separates regular entity draws, depth-hack draws, and
stencil-outline draws. Previously it scanned every recorded batch for the
latter two classes even on a frame that had produced neither class. That
empty work ran after the normal scan for every no-depth-hack/non-outline scene.

`vk_entity_state_t` now records two frame-local facts while entities are
submitted:

```text
frame_has_depth_hack_batches
frame_has_outline_batches
```

The regular entity source identifies depth-hack/weapon submissions, while
the outline batch append path marks a successfully emitted outline. The
renderer executes the respective optional traversal only when its flag is
set. The flags reset before each refdef, so a prior frame cannot incorrectly
enable an empty traversal. No pipeline, descriptor, draw order, or OpenGL
fallback is introduced or changed.

This is intentionally a CPU-front-end optimization. It removes four full
batch scans for the regular depth-hack pass and one for the outline pass when
the features are absent; it does not claim a GPU timing gain without a
comparable before/after capture.

## Validation

The source contract is retained in
`tools/renderer_parity/test_vulkan_entity_optional_pass_elision_source.py`.
It verifies the per-frame reset, both native producers, both guarded paths,
and absence of an OpenGL renderer route.

The staged DLL was built and the following hidden native-surface captures ran
with Vulkan validation enabled:

| Receiver | Result |
|---|---|
| Normal map entities, live refraction and zero-refraction control | `fr01_liquid_entity_ordering_manifest.json` passed both 307,200-pixel scenes. The live scene remained at MAE `0.885107 / 0.636576 / 0.732604`; the control remained maximum RGB `1 / 1 / 1` with zero pixels over one. |
| Active outline | `alias_outline_md2` passed its green-outline probe at IoU `0.924337`, with 2,370 OpenGL and 2,564 Vulkan probe pixels. |
| Depth-hack view weapon | `viewweapon_shell_bloom_after_refraction` passed 128,000 pixels at maximum RGB `1 / 1 / 1` and zero pixels over one. |

All process logs from the three capture roots were free of fatal renderer and
Vulkan validation errors. The final staged deliverable is `.install/`.

```text
python -m unittest discover -s tools/renderer_parity -p 'test_vulkan_entity_optional_pass_elision_source.py'
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/gen_vk_world_spv.py --validate
python tools/test_package_assets.py
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_liquid_entity_ordering_manifest.json --run-root .tmp/renderer-parity/liquid-entity-ordering-optional-pass-elision --timeout 180 --vulkan-validation
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_alias_outline_manifest.json --scene alias_outline_md2 --run-root .tmp/renderer-parity/entity-optional-pass-elision-outline --timeout 180 --vulkan-validation
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_viewweapon_shell_bloom_refraction_manifest.json --run-root .tmp/renderer-parity/entity-optional-pass-elision-depth-hack --timeout 180 --vulkan-validation
```

## Remaining boundary

The entity traversal still needs the broader `FR-01-T14` work: a general
transient ring, additional static residency/indirect submission where proven
valuable, and broader MD5/bmodel runtime coverage. Any renderer-wide CPU or
GPU superiority claim requires reproducible representative-map budgets under
`FR-01-T15`.
