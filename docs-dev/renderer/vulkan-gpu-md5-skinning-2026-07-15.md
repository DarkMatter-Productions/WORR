# Native Vulkan GPU MD5 Vertex Skinning

Date: 2026-07-15

Task ID: `FR-01-T14`

Status: partial implementation. Eligible MD5 replacement meshes now retain
their static mesh and weight data in native device-local Vulkan memory and run
weighted vertex position and normal skinning in a Vulkan vertex shader. This
removes the ordinary per-frame MD5 skinned-vertex and index upload. It does not
claim GPU animation-frame joint interpolation, full model/effect static
residency, or a measured renderer-wide performance win.

## Implemented path

At MD5 replacement registration, each mesh creates immutable device-local
vertex and 16-bit index buffers. The vertex record contains the bind-pose
normal, texture coordinates, and a validated range into one immutable global
weight buffer. The global buffer holds every weight's bind-space position,
bias, and joint index. Static transfers use the existing native Vulkan staging
command buffer with transfer-to-vertex-input/index-read/shader-read barriers.

For an eligible entity, the CPU still resolves its compact interpolated joint
palette. It uploads that palette alongside a 144-byte per-mesh instance record
containing origin, transform/normal axes, shell value, packed colour, and
fragment flags. The palette is a frame-local device-local storage buffer and
the immutable weights are a storage buffer. `vk_entity_gpu_md5.vert` traverses
each vertex's weights, reconstructs position and normal from the uploaded joint
palette, applies the existing shell expansion and entity transform, and emits
the same fragment interface as the ordinary Vulkan entity shader.

Consecutive compatible submissions of the same MD5 mesh, material, presentation
state, and rendering phase coalesce into instanced indexed Vulkan draws. This
is native Vulkan only; no GPU MD5 path routes through OpenGL.

## Visual and functional boundaries

The GPU path retains the existing MD5 animation-frame selection, joint
interpolation, skins, alpha/additive routing, shells, glowmaps, depth-hack,
weapon projection, lighting flags, and descriptor contract. The current CPU
joint-palette interpolation is intentional: it preserves the established MD5
animation resolve while moving the per-vertex weighted work and dynamic skinned
mesh upload to the GPU.

`RF_ITEM_COLORIZE` and `RF_OUTLINE` remain on the CPU-skinned indexed path
because their base/overlay/stencil/shell passes reuse generated triangle ranges.
Any unavailable static resource, descriptor, pipeline, or frame stream also
uses the established native CPU fallback. `vk_md5_gpu_skinning` is an archived,
latched Vulkan-only cvar that defaults to `1`.

## Expected cost change

For eligible MD5 meshes, current-frame transfer is reduced from one fully
skinned `vk_vertex_t` per source vertex plus a dynamic index range to one joint
record per resolved joint and one 144-byte mesh instance. Bind-pose vertex,
weight, and index data transfer only at replacement registration. The vertex
shader performs the weighted reconstruction once per submitted vertex, and
compatible adjacent instances share one instanced draw.

The CPU still interpolates joints, so this slice does not yet remove every MD5
animation cost. A paired normal-path OpenGL/Vulkan capture now proves native
GPU-MD5 submission under validation; representative `FR-01-T15` budgets remain
required before claiming an end-to-end performance gain. See
`vulkan-gpu-md5-runtime-parity-2026-07-19.md`.

The frame-local MD5 instance and joint-palette records now occupy their aligned
regions in the shared native model-instance arena. The descriptor retains the
palette region's explicit offset and capacity; see
`vulkan-entity-instance-arena-2026-07-20.md`.

The runtime coverage now also includes the registered 17-joint player
replacement at a non-zero source frame; see
`vulkan-gpu-md5-multijoint-runtime-parity-2026-07-20.md`.

## Headless validation

```text
python tools/gen_vk_world_spv.py --validate
ninja -C builddir-win worr_vulkan_x86_64.dll
python -m unittest tools/renderer_parity/test_vulkan_gpu_md5_submission_source.py
```

The focused source test verifies native static weight/mesh ownership, weighted
GPU vertex skinning, current-frame palette/instance streams, descriptor-set
binding, instanced indexed batching, and the explicit special-pass CPU
fallback. No interactive client window was launched.
