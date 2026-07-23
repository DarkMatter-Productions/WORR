# Vulkan Entity Refractive-Phase Elision

Date: 2026-07-19

Task IDs: `FR-01-T14`, `FR-01-T15`

Status: native Vulkan CPU-front-end optimization complete; visual and validation
evidence recorded below.

## Outcome

Liquid refraction splits native entity recording into three ordered regions:

1. opaque and alpha-back entities before the liquid snapshot;
2. late effects after transparent world liquid; and
3. alpha-front/view-weapon entities after those effects.

Previously every requested region entered `VK_Entity_RecordPhase` and scanned
the complete entity batch list even if no native batch belonged to that region.
The same unnecessary scans also occurred in the phase-specific direct-bloom and
depth-sampled-rim helpers.

`vk_entity_state_t` now records a frame-local bitmask for the submit phases
that successfully emitted a native batch. CPU-expanded triangles, indexed MD2
fallback meshes, GPU MD2 instances, GPU MD5 instances, immutable GPU inline
BSP instances, and outline batches all mark their current submit phase only
after they have appended live draw work. The mask is reset before every
refdef, so it cannot suppress a later frame's work.

`VK_Entity_HasRecordPhase` maps a recorder request to the same native ordering
contract: before-liquid includes opaque and alpha-back; post-liquid and
alpha-front each select their distinct phase; all accepts any live batch. The
scene recorder, depth-hack bloom recorder, pre-liquid bloom recorder, and
pre-liquid depth-sampled-rim query return before pipeline/descriptor setup and
batch traversal when the requested phase is empty.

Each submit phase now also records which of the regular entity ordering passes
it actually contains: opaque, item-colourize base, item-colourize overlay, or
general alpha/additive. A non-empty phase therefore skips its three known-empty
full-batch scans as well. The markers are populated by every CPU-expanded,
indexed, and GPU-instanced batch family from the same flags used by the draw
loop, so the original four-pass order is unchanged whenever a pass is live.

This does not reorder entities, alter a pipeline, change a descriptor, or
introduce an OpenGL path. It removes avoidable native CPU traversal from
ordinary refractive scenes that contain only a subset of the phases.

## Regression contract

`tools/renderer_parity/test_vulkan_entity_phase_elision_source.py` verifies:

- reset and producer-side marking of the frame-local native phase mask;
- exact before-liquid, post-liquid, and alpha-front mappings; and
- early phase and ordering-pass elision in scene and bloom paths, with no
  OpenGL renderer include.

The existing optional depth-hack/outline scan guard remains covered by
`test_vulkan_entity_optional_pass_elision_source.py`.

## Validation

The Vulkan renderer DLL builds from `builddir-win`. The staged build passed the
existing headless refraction/view-weapon capture under Vulkan validation, which
exercises the split native entity ordering. Its 128,000-pixel crop had maximum
RGB error `1 / 1 / 1` and zero pixels above RGB error one. The capture process
had no failures; its Vulkan log was also scanned for validation errors.

```text
ninja -C builddir-win worr_vulkan_x86_64.dll
python -m unittest discover -s tools/renderer_parity -p 'test_vulkan_entity_phase_elision_source.py'
python -m unittest discover -s tools/renderer_parity -p 'test_vulkan_entity_optional_pass_elision_source.py'
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_viewweapon_shell_bloom_refraction_manifest.json --run-root .tmp/renderer-parity/entity-phase-elision --timeout 180 --vulkan-validation
```

## Boundary

This is a bounded traversal optimization, not a renderer-wide timing claim.
The general transient ring, additional static/indirect effect submission, and
representative map-level CPU/GPU budgets remain open under `FR-01-T14` and
`FR-01-T15`.
