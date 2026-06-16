# OpenGL Shadowmap Opacity Tuning

Date: 2026-06-12

Tasks: `FR-02-T10`, `DV-07-T05`

## Summary

OpenGL shadowmap receivers now darken partial filtered shadow coverage before
applying the existing full-strength visibility mix. The previous receiver path
was already using `GL_SHADOW_DEFAULT_STRENGTH` at `1.0`, so increasing that
constant would not make PCF/PCSS/VSM/EVSM shadows more opaque: the shader clamps
the strength mix to `0..1`.

## Implementation

- Added `GL_SHADOW_VISIBILITY_EXPONENT_GLSL` in `src/rend_gl/gl.h`.
- Applied `pow(clamp(result, 0.0, 1.0), 2.0)` to sampled shadow visibility in
  `src/rend_gl/shader.c` before the existing `mix(1.0, result, strength)`.
- Fully lit samples remain `1.0` and fully blocked samples remain `0.0`; only
  filtered partial coverage is darkened so local shadows read less transparent
  and closer to Quake II Rerelease.

## Verification

- `ninja -C builddir-win worr_opengl_x86_64.pdb`
- `python tools/check_shadowmapping_guardrails.py`
