# First-Person Viewweapon Shadow Receiver Fix

Date: 2026-06-13

Task IDs: `FR-02-T09`, `FR-02-T10`, `FR-02-T11`, `DV-02-T06`, `DV-07-T05`

## Summary

First-person view weapons now receive shadowmap visibility instead of opting out
of shadow sampling. The first-person player body is still submitted as a hidden
`RF_VIEWERMODEL | RF_CASTSHADOW` caster, and `RF_WEAPONMODEL` view weapons are
still excluded from shared caster collection, so the held weapon receives the
local body shadow without becoming a caster itself.

## Implementation

- OpenGL no longer sets `RECEIVER_NO_SHADOWS` for weapon receivers in the
  dynamic-light UBO. The owner-weapon receiver key remains in place so the
  existing owner spotlight cone relaxation still lets the torso-mounted
  flashlight illuminate the held weapon.
- Native Vulkan no longer maps `RF_WEAPONMODEL` to
  `VK_ENTITY_VERTEX_NO_SHADOW`. Fullbright weapon effects, shells, beams,
  particles, and other explicit no-shadow paths still opt out through their
  existing flags.
- The shadowmapping guardrail script now blocks the old viewweapon receiver
  no-shadow snippets from returning.

## Verification Plan

- Build both non-RTX renderers.
- Run a first-person scene with `r_shadowmaps 1` under a selected shadowmapped
  light and confirm the local body shadow can darken the held weapon.
- Confirm the first-person weapon still does not appear as a caster in
  `r_shadow_draw_debug 8`.
