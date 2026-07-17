# Vulkan Shared Texture Filter Control Parity

Date: 2026-07-17

Task ID: FR-01-T14

Status: complete for the shared texture-filter control slice.

## Outcome

The three Video settings routes now write r_texture_filter rather than the
OpenGL-only gl_texturemode spelling. OpenGL synchronizes its legacy cvar and
reapplies existing wall and skin parameters. Vulkan synchronizes the legacy
cvar for existing configurations, creates a dedicated native material-repeat
sampler, and rebinds resident wall/skin descriptors after a safe idle.

The sampler translates all existing OpenGL filter modes, including nearest,
linear, the mipmapped nearest/linear variants, and MAG_NEAREST. Fonts, HUD
pictures, sky assets, and external post-process descriptors retain their
independent native filtering policy.

## Evidence

The forced-nearest durable fixture is
assets/renderer_parity/fr01_model_glowmap_shared_texture_filter.cfg. Its
headless Vulkan-validation run recorded r_texture_filter and gl_texturemode
at GL_NEAREST in both renderer logs. The 34,100-pixel model crop had a mean
RGB error of 0.00106 / 0.00059 / 0.00023, only one pixel over RGB error 16,
and an exact 2,352-pixel bright-glow mask with 1.0 IoU. No VUID, validation,
GPU-residency, or static-upload diagnostics were emitted.

The existing default device-limit GPU-MD2 gate was rerun from the same staged
tree after the sampler change. It retained maximum RGB error 2 / 2 / 1, zero
pixels over RGB error 16, and its exact 2,035-pixel bright-glow mask.

Source coverage in test_shared_texture_filter_control_source.py locks the
three video routes, OpenGL legacy synchronization, Vulkan's independent
material sampler, and the durable headless fixture.
