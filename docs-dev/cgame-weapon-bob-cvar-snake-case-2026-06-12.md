# Cgame Weapon Bob Cvar Snake Case Alias (2026-06-12)

## Related Tasks
- `DV-04-T02` - Complete client/cgame ownership map for duplicated behavior paths.
- `FR-03-T06` - Audit and complete settings page cvar wiring for Video/Audio/Input/HUD/Downloads.
- `DV-07-T04` - Add user-doc parity pass whenever user-visible cvars/features are changed.

## Summary
- Promoted `cg_weapon_bob` to the primary archived client-game cvar for first-person weapon bob mode selection.
- Kept the previous `cg_weaponBob` spelling as a non-archived compatibility alias so existing configs and console habits still route to the same setting.
- Moved the cgame Effects menu binding and the user cvar reference to `cg_weapon_bob`.

## Behavior
- `cg_weapon_bob 0`: disables cgame viewweapon bob and resets bob state.
- `cg_weapon_bob 1`: enables Quake 3-style viewweapon bob based on `E:\_SOURCE\_CODE\Quake-III-Arena-master\code\cgame\cg_weapons.c::CG_CalculateWeaponPosition`.
- `cg_weapon_bob 2`: keeps the existing Doom 3-inspired mode as the default.
- Setting `cg_weaponBob` updates `cg_weapon_bob` immediately; setting `cg_weapon_bob` mirrors the value back to `cg_weaponBob`.

## Implementation Notes
- `src/game/cgame/cg_entity_api.cpp` now registers both cvars in one helper and syncs changes with cvar `changed` callbacks.
- `src/game/cgame/cg_view.cpp` reads `cg_weapon_bob` first and falls back to `cg_weaponBob` if needed.
- `src/game/cgame/ui/worr.json` uses `cg_weapon_bob` for the Effects menu dropdown.
- `docs-user/client.asciidoc` documents the primary name and calls out the legacy alias.
