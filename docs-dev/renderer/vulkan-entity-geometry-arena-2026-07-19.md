# Vulkan Transient Entity Geometry Arena

Date: 2026-07-19

Task IDs: `FR-01-T14`, `FR-01-T15`

Status: complete for the CPU-expanded dynamic vertex/index slice. This is a
native Vulkan consolidation; it preserves existing batch order, GPU-resident
model paths, and CPU fallbacks.

## Outcome

Each bounded Vulkan frame context previously owned independent device-local and
mapped staging buffer pairs for CPU-expanded entity vertices and 16-bit
indices. The two streams now share one per-frame native geometry arena. Its
vertex and index regions retain independent geometric capacities, so growth of
one does not shrink the other, while the index region begins at an explicitly
16-bit-aligned offset.

The arena is created with vertex, index, and transfer-destination usage. Its
paired staging buffer is persistently mapped. `VK_Entity_RenderFrame` copies
only the live vertex and index ranges into their respective regions, retaining
the existing upload telemetry byte counts. `VK_Entity_RecordUploads` emits one
`vkCmdCopyBuffer` with one or two regions and one transfer-to-vertex-input
barrier carrying both vertex-attribute and index-read access. Dynamic batches
bind the same buffer at offset zero for vertices and the indexed-region offset
for their native `uint16_t` indices.

This removes one device-local allocation, one staging allocation, one upload
command, and one buffer barrier for every active CPU-expanded frame context
without changing live byte volume. The separate GPU-MD2, GPU-MD5, and inline
BSP instance streams remain intentionally independent because their layouts,
descriptor contracts, and residency lifetimes differ.

## Safety and behavior boundaries

The arena is frame-local, so the established bounded-frame fence wait still
protects it from overwrite while an earlier submission is in flight. A capacity
growth destroys and rebuilds the one arena only after that normal frame-slot
reuse point. Overflow checks cover both the independently grown capacities and
their combined aligned allocation size.

No Vulkan path delegates geometry processing to OpenGL. The change applies
only to the pre-existing CPU-expanded path used by legacy effects and special
model passes; it is not a general renderer-wide ring allocator, indirect draw
implementation, or a timing-budget claim.

## Validation

The existing owned outline matrix provides the relevant CPU-expanded coverage:
both outlined MD2 and MD5 models use the dynamic indexed stream because
outlines retain their intentional CPU fallback. The final fully staged
headless capture ran under Vulkan validation:

```text
python tools/refresh_install.py --build-dir builddir-win --install-dir .install
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_alias_outline_manifest.json --run-root .tmp/renderer-parity/entity-geometry-arena-final-staged --timeout 90 --vulkan-validation --json-output .tmp/renderer-parity/entity-geometry-arena-final-staged/results.json
```

Both scenes passed with no process failure, VUID, or validation error:

| Scene | OpenGL green pixels | Vulkan green pixels | IoU |
|---|---:|---:|---:|
| MD2 outline | 2,370 | 2,564 | 0.924337 |
| MD5 outline | 2,304 | 2,570 | 0.896498 |

The final post-alignment MD5 rerun produced the same 2,304/2,570 probe counts
and `0.896498` IoU under validation.

Focused structural and build checks passed:

```text
meson compile -C builddir-win worr_vulkan_x86_64
python -m unittest discover -s tools/renderer_parity -p test_vulkan_entity_stream_growth_source.py
python -m unittest discover -s tools/renderer_parity -p test_vulkan_gpu_md5_submission_source.py
python -m unittest discover -s tools/renderer_parity -p test_vulkan_entity_optional_pass_elision_source.py
python -m unittest discover -s tools/renderer_parity -p test_vulkan_entity_phase_elision_source.py
```

All launches use `win_headless 1` with client input disabled; no interactive
client window was started. The final staged Vulkan DLL SHA-256 is
`72FC05892C7AA11BCCBF06852D51EEEBF9DA8F2B8A1274ACD261A61396BDA7C2`, matching
the build output.

## Remaining work

`FR-01-T14` still needs a general transient-ring design where measurement
supports it, along with broader static/indirect effect submission. `FR-01-T15`
owns representative-map CPU/GPU measurements before treating this reduction in
command/allocation work as a product-level performance result.

No end-user documentation changed.
