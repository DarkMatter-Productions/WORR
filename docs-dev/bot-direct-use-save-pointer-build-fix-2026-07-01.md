# Bot Direct Use Save Pointer Build Fix

Task: `DV-04-T03`

## Summary

The full build exposed an existing compile failure in
`src/game/sgame/bots/bot_brain.cpp`: direct comparison of a `save_use_t`
wrapper to `nullptr` was ambiguous because `save_data_t` offers both wrapped
function-pointer and wrapped-object comparison overloads. The direct-use
recovery guard now uses the wrapper's explicit boolean conversion instead.

## Change

- Replaced `interaction->use == nullptr` with `!interaction->use` in
  `Bot_CommandTryDirectUseRecoveryInteraction`.

## Verification

- Passed: `meson compile -C builddir-win`. The rebuild compiled
  `src_game_sgame_bots_bot_brain.cpp.obj` and linked `sgame_x86_64.dll`.
- Passed: `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`.
  The Windows x86_64 staged payload validated successfully.
