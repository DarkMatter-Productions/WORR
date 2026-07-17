# Vulkan Picmip Upload Residency Parity

Date: 2026-07-17

Task ID: FR-01-T14

Vulkan now applies shared r_picmip before allocating and uploading eligible
wall/skin material images, then builds its native mip chain from that reduced
base level. It follows r_picmip_filter, r_nomip, and legacy
gl_downsample_skins eligibility rules. This matches OpenGL texture-quality
behavior while reducing native texture residency and transfer work.

The paired r_picmip 1 / r_picmip_filter 0 stock-MD2 gate produced maximum RGB
error 6 / 6 / 5, mean error 0.04663 / 0.04085 / 0.03317, zero pixels above RGB
error 16, and an exact 1,120-pixel glow-mask IoU. Vulkan validation was clean.
