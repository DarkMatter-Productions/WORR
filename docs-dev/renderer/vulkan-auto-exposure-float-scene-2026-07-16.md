# Native Vulkan Float-Scene Automatic Exposure

Tasks: `FR-02-T13`, `FR-01-T15`

## Outcome

Native Vulkan now implements the OpenGL renderer's temporal HDR exposure
contract without reading back pixels, stalling the graphics queue, or routing
work through OpenGL. When `vk_hdr_auto_exposure 1` is selected, it restarts
the renderer to allocate the required resources; HDR disabled and static-HDR
profiles retain their previous memory footprint.

## Implementation

- The `R16G16B16A16_SFLOAT` frame-slot scene-copy image owns a full mip chain
  only for automatic exposure. Vulkan records native copy/blit reductions after
  the completed scene copy and keeps all mip layouts shader-readable before
  sampling. Liquid refraction uses a separate level-zero image view and
  descriptor, so a refractive frame does not perform the complete mip
  reduction twice.
- Each bounded frame slot owns two one-pixel float exposure images. Its prior
  value is sampled for adaptation and its other image receives the new value,
  so no command buffer reads a resource still owned by an in-flight slot.
- The existing Vulkan final-postprocess module contains a one-pixel reduction
  branch and samples the resulting exposure during ACES presentation. The
  controls mirror OpenGL: `vk_hdr_auto_min_luma`,
  `vk_hdr_auto_max_luma`, and `vk_hdr_auto_speed`; a zero speed resolves the
  current scene immediately.
- The postprocess descriptor layout now keeps a valid exposure sampler for all
  pipelines, and uses a separate descriptor set for the reduction draw. This
  avoids descriptor mutation races between the exposure draw and final pass.

## Validation

- `assets/renderer_parity/fr01_hdr_auto_exposure.cfg` fixes the same HDR
  scene, controls, and immediate adaptation settings for both renderers.
- `assets/renderer_parity/fr01_hdr_auto_exposure_manifest.json` records the
  current hardware-neutral OpenGL/Vulkan bound: maximum/mean RGB error
  `2 / 4 / 5` over 50,000 pixels, with no pixel above five codes. The remaining
  bounded difference is from backend mip-filter reduction, not presentation
  quantization or a fallback renderer path.
- The validation-layer capture at
  `.tmp/renderer-parity/hdr-auto-exposure-single-reduction-fix/` has no VUID
  or validation errors after the multi-mip image-view, descriptor-validity,
  and level-zero liquid-view fixes.

## Remaining Work

Display-HDR swapchain transfer functions, a reduction kernel with cross-driver
bitwise-equivalent mip filtering, and broader moving-scene adaptation captures
remain open under `FR-02-T13`.
