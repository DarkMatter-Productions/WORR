# TTF Fullscreen Font Pixel-Scale Refresh

Date: 2026-04-28

Task ID: `FR-06-T03`

## Summary

- Fixed fullscreen/resize handling so TTF-backed client fonts keep their assigned font type while refreshing the rasterized pixel height from the current framebuffer size.
- Removed a stale-cache path where some client font users could keep an older typeface/fallback decision until a later reload happened for unrelated reasons.
- Corrected shared screen/cgame multiline font stepping so TTF-backed text advances by the active font line height instead of legacy `8px` rows.
- Fixed a cgame HUD font-role mix-up where different HUD draw helpers could select different TTF families (`scr.font` vs. the screen/UI readable font path), allowing mode/layout changes to present as a font-family swap.
- Fixed a cgame menu bootstrap issue where the JSON-selected UI font could remain on the preconfigured default handle until a later renderer restart, allowing top-level menu labels to switch TTF family when toggling fullscreen/windowed mode.

## Findings

1. The client font pixel-scale helpers in screen/UI/console code were derived from the integer virtual-screen scale bucket (`base_scale_int`).
   - That preserved virtual layout, but it also meant fullscreen/window-size changes inside the same bucket did not refresh TTF raster size.
   - The result was resolution-dependent behavior where the visible font raster did not consistently track the real framebuffer size.
2. Font identity and font raster refresh were not invalidated consistently across all client consumers.
   - `screen` and `ui_font` already had better reload coverage.
   - `console` did not react to the shared font settings generation.
   - The legacy client weapon-bar font cache did not react to shared font settings generation either.
   - This could leave mixed font-kind state in-session until a later reload happened because of an unrelated resize or cvar path.
3. The shared `SCR_DrawStringMultiStretch(...)` bridge still advanced multiline rows using `CONCHAR_HEIGHT * scale` even when the active path was TTF-backed.
   - Measurement used font metrics, but drawing still stepped with legacy row height.
4. The cgame SCR font bridge was not using a single HUD font role consistently.
   - `CG_SCR_DrawFontString(...)`, `CG_SCR_MeasureFontString(...)`, and `CG_SCR_FontLineHeight(...)` were using the readable screen/UI font path.
   - `CG_SCR_DrawCenterFontString(...)` and `CG_SCR_MeasureCenterFontString(...)` were using the HUD font path first.
   - Because those helpers were both used for HUD/cgame text, layout or mode-sensitive call paths could swap one TTF family for another without any user font-setting change.
5. The cgame JSON menu font assignment was happening too late to affect the already-loaded UI font handle on first startup.
   - `UI_FontInit()` ran before `SCR_Init()` and before cgame menu JSON globals were parsed.
   - The JSON globals then set `ui_font`, but they did so via a `FROM_CODE` cvar path that does not invoke cvar changed callbacks.
   - That left the UI font module on its startup default face until some later mode restart forced a full font reload, which made the same menu labels appear to change family when switching between fullscreen and windowed modes.

## Implementation

- `src/client/client.h`
  - Added `CL_CalcFontPixelScale(...)` as a shared client-side helper for TTF raster pixel-scale calculation.
  - The helper preserves the existing `cl_font_skip_virtual_scale` override behavior, but otherwise uses the real framebuffer-derived base scale instead of the integer virtual bucket.
- `src/client/screen.cpp`
  - Updated `SCR_GetFontPixelScale()` to use the shared helper.
  - Updated `SCR_DrawStringMultiStretch(...)` to advance rows using `Font_LineHeight(...)` whenever the active screen font path is in use, keeping draw spacing aligned with TTF measurement.
- `src/client/ui_font.cpp`
  - Updated UI font reload sizing to use the shared framebuffer-derived pixel-scale helper.
  - Added `ui_font` modified-count tracking so UI font reload checks also detect code-driven menu/config cvar changes, not just dimension or shared font-generation changes.
- `src/client/console.cpp`
  - Updated console font reload sizing to use the shared framebuffer-derived pixel-scale helper.
  - Added `con_font_settings_generation` tracking so console fonts also reload when shared typeface/fallback/hinting settings change, rather than only when the console font cvars themselves change.
- `src/client/weapon_bar.cpp`
  - Updated the legacy client weapon-bar font cache to use the shared framebuffer-derived pixel-scale helper.
  - Added shared font-settings-generation invalidation so cached weapon-bar fonts do not hold onto an old typeface/fallback choice after global font-setting changes.
- `src/client/cgame.cpp`
  - Added a shared HUD-font resolver for the cgame SCR bridge.
  - Updated HUD line-height, measure, and draw helpers to use the HUD font role consistently instead of mixing HUD and UI-readable TTF families by helper.
  - Kept multiline center/right alignment per-line while switching the draw path away from the UI-font-specific helper.
- `src/client/font.cpp`
  - Centralized fallback cvar initialization with `font_ensure_fallback_cvars()`.
  - Expanded `Font_SettingsGeneration()` so it now tracks:
    - `ui_high_visibility_text`
    - `ui_text_typeface`
    - `cl_font_fallback_kfont`
    - `cl_font_fallback_legacy`
    - `cl_font_ttf_hinting`
  - This gives all font consumers a more accurate shared invalidation signal for actual font-kind-affecting changes.
- `src/game/cgame/ui/ui_json.cpp`
  - Switched JSON menu-global `ui_font` application to `Cvar_SetEx(..., FROM_CONSOLE)` so the engine-side font changed callback runs immediately during menu bootstrap instead of waiting for a later renderer restart.
- `src/game/cgame/ui/worr.json`
  - Changed the menu-global font reference from legacy `$scr_font` to canonical `$cl_font` so menu font selection resolves through the primary HUD font cvar rather than the legacy alias.

## Result

- Resize/fullscreen changes now refresh TTF raster pixel height based on the current framebuffer profile instead of waiting for an integer virtual-scale bucket boundary.
- The assigned font kind for a given path stays stable across resize events unless the user actually changes a font-affecting setting.
- HUD/cgame string helpers now stay on the HUD font role instead of accidentally switching to the readable UI TTF family when a different helper or layout branch is used.
- Multiline shared HUD/cgame text now uses the active font's line height consistently during draw as well as measure.
- The main menu now binds its configured TTF family during initial bootstrap, so top-level menu labels no longer start on one face and then flip to another after a fullscreen/windowed mode change.

## Validation

- `meson compile -C builddir-win worr_x86_64 worr_engine_x86_64 cgame_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `.install\worr_x86_64.exe +set basedir E:\Repositories\WORR\.install +set r_renderer opengl +set r_fullscreen 1 +set win_fullscreen_capture_friendly 1 +set logfile 1 +set logfile_flush 1 +set logfile_name font_fullscreen_resize_smoke +set cl_debug_fonts 1 +wait 120 +quit`
  - Completed successfully.
  - `E:\Repositories\WORR\.install\basew\logs\font_fullscreen_resize_smoke.log` confirmed SDL3_ttf initialization plus TTF loads for:
    - `fonts/NotoSansKR-Regular.otf`
    - `fonts/RussoOne-Regular.ttf`
    - `fonts/AtkinsonHyperLegible-Regular.otf`
    - `fonts/RobotoMono-Regular.ttf`
- `.install\worr_x86_64.exe +set basedir E:\Repositories\WORR\.install +set r_renderer opengl +set r_fullscreen 1 +set logfile 1 +set logfile_flush 1 +set logfile_name font_menu_startup_trace_v2 +set cl_debug_fonts 1 +wait 120 +quit`
  - Completed successfully.
  - `E:\Repositories\WORR\.install\basew\logs\font_menu_startup_trace_v2.log` showed the menu UI font bootstrap reloading from the startup default to `fonts/RussoOne-Regular.ttf` during initial startup, before the first mode change.
- `.install\worr_x86_64.exe +set basedir E:\Repositories\WORR\.install +set r_renderer opengl +set r_fullscreen 1 +set logfile 1 +set logfile_flush 1 +set logfile_name font_menu_mode_trace_v2 +set cl_debug_fonts 1 +wait 120 +set r_fullscreen 0 +vid_restart force +wait 180 +quit`
  - Completed successfully.
  - `E:\Repositories\WORR\.install\basew\logs\font_menu_mode_trace_v2.log` showed the menu UI font resolving to `fonts/RussoOne-Regular.ttf` both before and after the forced fullscreen-to-windowed renderer restart, instead of correcting only after the mode switch.
