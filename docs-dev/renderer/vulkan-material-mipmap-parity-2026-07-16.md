# Vulkan Material Mipmap Parity

Tasks: `FR-01-T12`, `FR-01-T15`

## Problem

Vulkan material registration previously allocated every native image with only
mip level zero. The shared sampler used trilinear filtering, but a one-level
view necessarily sampled the original wall or skin texture while OpenGL's
`GL_Upload32` creates a complete mip chain for those material types. The gap
was most visible under resolution scaling: high-frequency wall checks stayed
aliased in Vulkan before the HDR final pass, while OpenGL selected a reduced
mip level.

## Native implementation

`src/rend_vk/vk_ui.c` now gives `IT_WALL` and `IT_SKIN` images a full native
mip chain. It queries `VK_FORMAT_R8G8B8A8_UNORM` for transfer-source,
transfer-destination, and linear-filter support, and uses a one-level image
when those features are unavailable. Pictures, fonts, raw video updates, and
other non-material image types deliberately remain single-level, matching the
OpenGL contract.

After uploading level zero, Vulkan transitions each completed level from
transfer-destination to transfer-source, linearly blits it into the next
level, then makes all levels fragment-shader-readable. The material image view
exposes the entire chain to the existing native trilinear sampler. Reuploads
regenerate the complete chain; sub-rectangle paths remain intended for
single-level dynamic images. A sub-rectangle update for a registered material
is rejected explicitly, because updating only level zero would otherwise leave
the derived mip levels stale. The dynamic world-lightmap atlas is explicitly
registered as a repeat-wrapped, single-level native image, matching OpenGL's
level-zero lightmap subimage updates without paying to regenerate material
mips every frame.

This remains fully native Vulkan image creation, barriers, blits, and
descriptor sampling; no OpenGL renderer path is used.

## Validation

The exact 960x720 native HDR waterwarp gate remains clean with Vulkan
validation. At fixed half scene scale, the unwarped and fullscreen-waterwarp
HDR gates now have no pixel beyond RGB2 and maxima of RGB2. Their remaining
mean absolute error is at most `0.14` on the red channel and below `0.14` on
the other channels, replacing the prior high-frequency mismatch (about 26% of
pixels beyond RGB2). Evidence:

- `.tmp/renderer-parity/hdr-underwater-unwarped-resolution-scale-mips/`
- `.tmp/renderer-parity/hdr-underwater-waterwarp-resolution-scale-mips/`
- `.tmp/renderer-parity/hdr-underwater-waterwarp-native-mips/`

`tools/renderer_parity/test_vulkan_material_mipmap_parity_source.py` prevents
material images from silently returning to a single-level allocation or
dropping the native blit/synchronization path.
