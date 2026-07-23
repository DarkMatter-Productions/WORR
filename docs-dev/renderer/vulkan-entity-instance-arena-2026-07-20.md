# Vulkan Per-Frame Model Instance Arena

Date: 2026-07-20

Task IDs: `FR-01-T14`, `FR-01-T15`

Status: complete for the bounded GPU-model-instance slice. This is a native
Vulkan consolidation that preserves existing submission order, per-stream
capacity growth, and CPU-expanded fallbacks.

## Outcome

Each bounded Vulkan frame context previously owned four independent current-frame
device-local/staging pairs: GPU MD2 instances, inline-BSP instances, GPU MD5
instances, and the GPU MD5 joint palette. They now share one native instance
arena and one persistently mapped staging arena. The later default-on instanced
particle path adds its compact particle records as a fifth independent region
without adding another per-frame buffer pair. The simple-beam lane likewise
adds its compact segment records as a sixth independent region.

The MD2, inline-BSP, particle, simple-beam, MD5-instance, and MD5-palette regions retain independent
geometric capacities. Each region starts on at least a 16-byte boundary; the
MD5 palette additionally observes the physical device's
`minStorageBufferOffsetAlignment`. Replacing a grown arena happens only at the
existing frame-slot reuse point, after that slot's fence has made all earlier
uses safe to retire.

The device-local arena carries vertex, storage-buffer, and transfer-destination
usage. `VK_Entity_RenderFrame` writes only live records at their native offsets.
`VK_Entity_RecordUploads` issues one multi-region `vkCmdCopyBuffer` and one
transfer-to-vertex-input/vertex-shader buffer barrier. GPU MD2, inline BSP,
particle, simple-beam, and GPU MD5 vertex bindings use the appropriate arena offsets. The MD5 descriptor
set now binds the palette region with its explicit offset and capacity, so its
joint indices retain their existing region-relative shader contract.

This reduces the active model-instance path from four device-local allocations,
four mapped staging allocations, four copies, and four barriers per frame
context to one of each. It preserves the established live upload-byte telemetry
and does not make a timing or renderer-wide performance claim.

## Safety and behavior boundaries

The geometry arena remains separate because it needs native 16-bit index
binding, while static MD2/MD5 mesh resources and the immutable MD5 weight buffer
remain separate because their residency lifetimes outlive a frame slot. The
general CPU-expanded effect stream remains supported through the existing
geometry arena. No Vulkan rendering path delegates to OpenGL.

This is not yet a renderer-wide transient ring allocator, indirect submission
implementation, GPU animation interpolation redesign, or a replacement for
representative-map performance measurements.

## Validation

The final fully staged headless captures ran with Vulkan validation after a
complete staging refresh:

```text
python tools/refresh_install.py --build-dir builddir-win --install-dir .install
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_model_glowmap_md2_gpu_manifest.json --run-root .tmp/renderer-parity/entity-instance-arena-md2 --timeout 90 --vulkan-validation --json-output .tmp/renderer-parity/entity-instance-arena-md2/results.json
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_bmodel_instances_manifest.json --run-root .tmp/renderer-parity/entity-instance-arena-bmodel --timeout 90 --vulkan-validation --json-output .tmp/renderer-parity/entity-instance-arena-bmodel/results.json
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_model_gpu_md5_manifest.json --run-root .tmp/renderer-parity/entity-instance-arena-md5 --timeout 90 --vulkan-validation --json-output .tmp/renderer-parity/entity-instance-arena-md5/results.json
```

All scenes passed with no process failure, VUID, or validation diagnostic:

| Native consumer | Result |
|---|---|
| GPU MD2 vertex-instance region | 34,100-pixel crop; maximum RGB `2 / 2 / 1`, zero pixels above RGB error 16, exact 2,035-pixel glow mask (IoU `1.0`). |
| Inline-BSP vertex-instance region | 218,400-pixel crop; RGB-exact, zero pixels above RGB error 8, exact 179,742-pixel grid mask (IoU `1.0`). |
| GPU MD5 vertex-instance and storage-palette regions | 34,100-pixel crop; maximum RGB `1 / 1 / 1`, zero pixels above RGB error 16, exact 1,864-pixel glow mask (IoU `1.0`). The Vulkan log confirms one GPU-MD5 draw/instance. |
| GPU particle vertex-instance region | Covered by `vulkan-instanced-particle-submission-2026-07-20.md`: the fog and three additive fields pass their validation-backed paired gates. |
| GPU simple-beam vertex-instance region | Covered by `vulkan-instanced-simple-beam-submission-2026-07-20.md`: the fogged target-laser gate passes its validation-backed paired threshold. |

Focused build and source checks passed:

```text
meson compile -C builddir-win worr_vulkan_x86_64
python -m unittest tools/renderer_parity/test_vulkan_entity_stream_growth_source.py
python -m unittest tools/renderer_parity/test_vulkan_gpu_md5_submission_source.py
```

All automated launches use `win_headless 1` with client input disabled; no
interactive client window was started. At the conclusion of this model-arena
validation slice, the staged Vulkan DLL SHA-256 was
`BCCF2E7408413CB3E2C2F7EFCE48687BBA23DA4A313CA5A86172478D8BC3CEA7`, matching
that build output. The later particle-instancing slice records its own current
artifact hash in `vulkan-instanced-particle-submission-2026-07-20.md`.

## Remaining work

`FR-01-T14` still owns a general transient-ring design where measurements
justify it, broader static/indirect effect submission, and additional MD5
runtime coverage. `FR-01-T15` owns representative-map CPU/GPU budgets before
the command/allocation reduction is treated as a product-level performance
result.

No end-user documentation changed.
