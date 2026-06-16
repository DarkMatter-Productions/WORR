# Cgame Weapon Bob Active Pose Fix (2026-06-12)

## Related Tasks
- `DV-04-T02` - Complete client/cgame ownership map for duplicated behavior paths.
- `FR-03-T06` - Audit and complete settings page cvar wiring for Video/Audio/Input/HUD/Downloads.
- `DV-07-T04` - Add user-doc parity pass whenever user-visible cvars/features are changed.

## Summary
- Fixed `cg_weapon_bob 1` so Quake 3 mode uses the active interpolated playerstate weapon pose.
- The previous cgame-only bob cycle could leave the setting live but visually ineffective because it bypassed the same `gunoffset`/`gunangles` path that WORR already uses for the first-person weapon.
- `cg_weapon_bob 0` still returns the clean base view pose with no weapon bob, while `cg_weapon_bob 2` keeps the Doom 3-inspired cgame pose path.

## Implementation Notes
- `src/game/cgame/cg_view.cpp::CG_ApplyQuake3WeaponBob` now mirrors the original viewweapon pose setup:
  - `origin = cl.refdef.vieworg + lerp(ops->gunoffset, ps->gunoffset)`
  - `angles = cl.refdef.viewangles + lerp(ops->gunangles, ps->gunangles)`
- The Quake 3 constants still come from `src/game/sgame/player/p_view.cpp`, matching `E:\_SOURCE\_CODE\Quake-III-Arena-master\code\cgame\cg_weapons.c::CG_CalculateWeaponPosition`.
- The cgame bob state is reset when entering Quake 3 mode so switching back to Doom 3 mode starts from a clean state.
