# Native Vulkan Entity Stream Capacity Growth

Date: 2026-07-15

Task ID: `FR-01-T14`

Status: partial implementation. The CPU-expanded entity/effect submission path
remains a target for indexed static meshes, instances, GPU MD5 skinning, and a
frame-safe transient ring. Its mapped vertex stream now has stable geometric
capacity, removing resize-driven allocation stalls as scene complexity varies.

## Outcome

`VK_Entity_EnsureVertexBuffer` used to destroy and recreate its persistent
host-visible Vulkan vertex buffer at the exact byte size of every new
high-water frame. A particle burst, many entities, or a complex MD5 scene
could therefore trigger immediate buffer/memory recreation whenever it exceeded
the previous maximum.

The native stream now starts at 64 KiB and doubles capacity until it contains
the requested vertex bytes. Size arithmetic is guarded against overflow. The
buffer remains persistently mapped and the command path still copies and
reports only the live vertex range, so GPU memory capacity does not inflate the
per-frame upload statistic or draw range.

This preserves the renderer's ordering and synchronization contract. Each
bounded frame context owns device-local entity draw buffers plus persistent
mapped staging buffers, so the current slot is waited before reuse and can
never overwrite a preceding submission. The standard MD2/MD5 index companion
stream now preserves its native 16-bit format and uses per-draw vertex offsets,
halving its live upload bytes without changing indexed triangle order. The
change does not itself claim GPU-driven submission before the related
indexed/static-mesh, instance, and skinning work is complete.

The later `FR-01-T14` follow-up coalesces the CPU-expanded vertex and index
buffers in each frame context into one native geometry arena with independently
grown regions. It retains the same live-byte telemetry while reducing active
buffer, copy, and barrier work; see
`vulkan-entity-geometry-arena-2026-07-19.md`.

The later GPU-model follow-up similarly coalesces the MD2, inline-BSP, MD5
instance, and MD5 joint-palette streams in each frame context into one aligned
native instance arena. Its regions preserve independent geometric capacity and
the MD5 descriptor binds the palette's native offset; see
`vulkan-entity-instance-arena-2026-07-20.md`.

## Performance characteristics

The change turns repeated high-water growth from O(number of incremental
spikes) Vulkan buffer/memory allocations into amortized O(log growth) capacity
changes. Steady-state frames retain the same single memcpy and draw batches,
but bursty scenes avoid destroy/unmap/free/create/allocate/map work on their
hot frame. The allocation is entirely native Vulkan and contains no OpenGL
renderer dependency.

## Headless validation

`tools/renderer_parity/test_vulkan_entity_stream_growth_source.py` checks the
minimum capacity, geometric/overflow-safe growth, allocation-size wiring,
live-range upload accounting, and native-only boundary. It runs without a
window:

```text
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install
python -m unittest tools/renderer_parity/test_vulkan_entity_stream_growth_source.py
```

Future performance evidence must use the GPU timestamps from `FR-01-T15` on
an identical burst scene before and after indexed/instanced/ring-buffer work.
