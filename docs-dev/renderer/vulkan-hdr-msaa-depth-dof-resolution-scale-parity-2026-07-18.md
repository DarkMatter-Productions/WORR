# Native Vulkan HDR MSAA Scaled-DOF Parity

Date: 2026-07-18  
Project tasks: `FR-02-T13`, `FR-01-T15`  
Status: validation-backed coverage added; broader HDR presentation coverage remains open

## Scope

This coverage combines the native Vulkan float-HDR scene target, a four-sample
renderer request, fixed half-resolution scaling, and active depth-aware DOF.
It proves the combination against the OpenGL renderer without redirecting any
Vulkan work through OpenGL.

The scene starts with shared `r_multisamples=4` and fixed half scale. Vulkan
keeps the request native, chooses its existing native one-sample companion
scene/load-pass family for the scaled, depth-sampling DOF input, retains
float scene and post-process targets until final presentation, and composes to
the native-size output. OpenGL uses its corresponding single-sample scaled
post-process FBO contract.

## Fixture contract

`assets/renderer_parity/fr01_hdr_dof.cfg` deliberately sets the OpenGL and
Vulkan HDR controls after the shared fixture defaults. This prevents a
command-line ordering error where the base fixture reset only `gl_hdr` after
launch while `vk_hdr` remained enabled. Both backends therefore receive the
same static HDR values: exposure `1.35`, white `1.15`, gamma `2.2`, and
disabled auto exposure.

`assets/renderer_parity/fr01_hdr_multisample_depth_dof_resolution_scale_manifest.json`
contains two 960x720, 640x480-crop scenes:

- active HDR, 4x MSAA request, fixed-half scale, and active DOF;
- the identical HDR/4x/fixed-half scene with `r_dof=0`.

Both gates require zero mean error and no pixel above RGB error one. The
control proves that the scale/HDR/native-MSAA resource selection itself does
not introduce a presentation delta.

## Validation

The headless validation-enabled matrix at
`.tmp/renderer-parity/fr01-hdr-msaa-depth-dof-half-resolution-final/` passes
both scenes exactly over all 307,200 crop pixels:

| Scene | MAE RGB | Pixels above RGB error 1 |
|---|---:|---:|
| Active HDR + 4x MSAA + scaled DOF | `[0, 0, 0]` | `0 / 307,200` |
| Same scene with DOF disabled | `[0, 0, 0]` | `0 / 307,200` |

`VK_LAYER_KHRONOS_validation` reported no findings. The run used a hidden
surface with input disabled; no interactive client was launched.

## Remaining work

This does not close the broader HDR task. Dynamic scale transitions, CRT and
liquid/refraction combinations, auto-exposure behavior at scale, wider menu
rectangles, and cross-adapter performance budgets remain open.
