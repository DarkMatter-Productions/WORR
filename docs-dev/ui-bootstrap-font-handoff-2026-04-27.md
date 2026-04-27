# UI Bootstrap Font and Handoff Polish

Date: 2026-04-27

Task IDs: `DV-08-T12`, `FR-06-T03`

## Summary

- Made the in-game weapon bar use the TTF font pipeline as its primary text path for ammo counts and static tile labels.
- Routed the cgame weapon bar through the shared UI TTF bridge; this is the path used by the actual in-game weapon bar.
- Added system TTF fallback loading for client fonts so missing packaged font assets no longer silently force kfont/conchars.
- Replaced the bootstrapper's SDL debug-font text with SDL3_ttf-rendered text where available.
- Raised the bootstrap legal footer to a readable fine-print size, using a responsive 12-16 px range.
- Shortened the no-update bootstrap minimum display time so normal launches hand off promptly.
- Removed the engine-side redraw of the bootstrap logo during startup transition; the engine now fades the real active frame in from an opaque black base.
- Added an opaque engine-side clear under every non-transparent menu frame so old splash/backbuffer pixels cannot survive behind translucent menu art.
- Made the bootstrap transition marker one-shot so renderer resets cannot re-arm the handoff fade later in the session.
- Disabled Windows shared-HWND handoff for the bootstrap shell so Windows 11 taskbar previews and OS-level PrintScreen sample the renderer-owned engine window instead of the bootstrapper surface.
- Added capture-friendly Windows fullscreen behavior so fullscreen resolves to borderless by default and focus loss to Win11 Snipping Tool does not minimize the game.
- Extended high-visibility black text backgrounds across shared HUD/menu text paths, including cgame imports and legacy menu string wrappers.

## Implementation

### Weapon Bar TTF Text

`src/client/weapon_bar.cpp` loads a small cached set of weapon-bar-specific font handles from `fonts/AtkinsonHyperLegible-Regular.otf`, falling back through `fonts/qconfont.kfont` and `conchars.png` only when the TTF path is unavailable.

The helper functions that previously measured text as fixed 8x8 cells or drew stretched renderer chars now use `Font_MeasureString`, `Font_LineHeight`, and `Font_DrawString` first. This keeps static weapon bar labels and timed weapon-bar ammo counts on the same TTF-backed path as the rest of the readable HUD text.

The live in-game weapon bar is drawn by `src/game/cgame/cg_wheel.cpp`, so its weapon-bar-specific measurement and draw helpers now use `UI_FontMeasureStringSized`, `UI_FontLineHeightSized`, and `UI_FontDrawStringSized`. The old stretched conchar draw remains as a fallback only if the UI font bridge cannot provide a font.

`src/client/font.cpp` now attempts a system TTF fallback after filesystem font lookup fails. On Windows this uses Segoe UI before Arial; on macOS/Linux it uses common platform sans fonts. This keeps the TTF path active even when the local staged assets do not include the requested project font yet.

The TTF measurement path now measures from the same per-glyph advance data used by the renderer cache instead of trusting the SDL3_ttf shaped text object's aggregate width. This keeps center/right menu alignment consistent when a platform fallback font is used.

### Bootstrapper Text

`src/updater/bootstrap.cpp` now initializes SDL3_ttf for the splash UI and looks for a project font near the install root before falling back to OS UI fonts such as Segoe UI on Windows. If no TTF can be opened, it still falls back to SDL debug text so the bootstrapper remains diagnosable.

The legal footer now uses a responsive fine-print size of 12-16 px. Headline, detail, progress, and button text also use responsive TTF sizes instead of scaled debug glyphs.

### Startup Handoff

The Windows bootstrapper no longer hands its native splash window to the engine. The prior shared-HWND path left Windows 11 desktop composition, taskbar thumbnails, and OS-level PrintScreen attached to a bootstrap-owned surface, which could appear as a stale splash or black thumbnail even after the engine rendered.

The splash window is also created as a tool window rather than an app-window taskbar surface. The non-shared path keeps the splash visible until the hosted engine signals its first completed frame, then destroys the splash UI and leaves the renderer-owned engine window as the only app frame. This matches the OS's expectations for taskbar preview and capture while preserving a clean startup transition.

`src/client/screen.cpp` keeps a short startup transition, but it now draws an opaque black transition base before the menu and fades that active engine frame in for 180 ms instead of redrawing the bootstrap logo over the menu. That makes the main menu appear quickly and fully, and leaves no bootstrap artwork in the engine frame stream.

The menu clear is no longer limited to the startup transition. Full, non-transparent menus now receive an opaque black renderer-owned backbuffer clear before menu draw every frame. This covers the OpenGL path where `R_BeginFrame` does not clear by default and prevents stale bootstrap/loading imagery from showing through translucent menu fills or logo art after the transition is complete.

The `WORR_BOOTSTRAP_TRANSITION` marker is consumed inside the client screen code the first time it is observed. That keeps later screen shutdown/reinitialization or renderer mode changes from restarting a bootstrap handoff fade during the same engine session.

`src/windows/client.c` now registers `win_fullscreen_capture_friendly` with a default of `1`. When enabled, Windows fullscreen requests use borderless fullscreen even if `r_fullscreen_exclusive` is set, and borderless fullscreen is not minimized on focus loss. This lets Win11 PrintScreen/Snipping Tool take foreground while the fullscreen game remains visible underneath. Setting `win_fullscreen_capture_friendly 0` restores the legacy exclusive-mode path for compatibility testing.

### High-Visibility Text Backgrounds

`src/client/font.cpp` exposes the shared black-background decision through `Font_DrawBlackBackgroundEnabled()`. The shared font renderer now treats either `cl_font_draw_black_background 1` or `ui_acc_contrast 1` as enabling the black text background. This keeps the cgame accessibility contrast option from being limited to centerprint-specific bars.

Text paths that can bypass `Font_DrawString` now route back through the shared screen/UI font wrappers where practical. This includes cgame renderer string imports, legacy client menu strings, legacy menu status text, printable menu character draws, and fallback `SCR_DrawStringStretch` calls. Non-text legacy glyphs still use the conchar path, with a small black background when the accessibility option is enabled.

## Verification Notes

- `meson setup --reconfigure builddir-win-bootstrap-hosted`
  - Confirmed the bootstrapper configuration requires and resolves SDL3_ttf.
- `meson compile -C builddir-win-bootstrap-hosted worr_x86_64 worr_engine_x86_64 cgame_x86_64`
  - Built the bootstrap launcher, client engine, and cgame weapon-bar paths touched by this change.
- `meson compile -C builddir-win-bootstrap-hosted worr_x86_64 worr_engine_x86_64 cgame_x86_64`
  - Rebuilt after the persistent menu clear and high-visibility text wrapper changes.
- `python tools\refresh_install.py --build-dir builddir-win-bootstrap-hosted --install-dir .install --base-game basew --platform-id windows-x86_64`
  - Refreshed and validated the local Windows `.install` staging root.
- `WORR_BOOTSTRAP_TRACE=1 .install\worr_x86_64.exe +set basedir E:\Repositories\WORR\.install +set r_renderer opengl +set cl_debug_fonts 1 +font_dump_glyphs fonts\NotoSansKR-Regular.otf 8 +wait 120 +quit`
  - Completed a staged launcher/client smoke run successfully.
  - Bootstrap trace reported `LaunchEngineAndWait shared_window_handoff=0`.
  - `.install\basew\fontdump\glyphs_008.txt` reported `kind: ttf` for the UI font path used by the cgame weapon bar helpers.
- `WORR_BOOTSTRAP_TRACE=1 .install\worr_x86_64.exe +set basedir E:\Repositories\WORR\.install +set r_renderer opengl +set r_fullscreen 1 +set r_fullscreen_exclusive 1 +set win_fullscreen_capture_friendly 1 +set cl_debug_fonts 1 +font_dump_glyphs fonts\NotoSansKR-Regular.otf 8 +wait 120 +quit`
  - Completed a staged forced-fullscreen smoke run successfully.
  - Bootstrap trace reported `mode=borderless_fullscreen`, `exclusive=1`, and `capture_friendly=1`, confirming Win11-friendly fullscreen routing.
  - `.install\basew\fontdump\glyphs_009.txt` reported `kind: ttf`.
- `WORR_BOOTSTRAP_TRACE=1 .install\worr_x86_64.exe +set basedir E:\Repositories\WORR\.install +set r_renderer opengl +set r_fullscreen 0 +set cl_font_draw_black_background 0 +set ui_acc_contrast 1 +set cl_debug_fonts 1 +font_dump_glyphs fonts\NotoSansKR-Regular.otf 8 +wait 120 +quit`
  - Completed a staged launcher/client smoke run with the cgame contrast option providing the black-background text path.
  - Bootstrap trace reported `LaunchEngineAndWait shared_window_handoff=0`.
  - `.install\basew\fontdump\glyphs_010.txt` reported `kind: ttf`.
- `WORR_BOOTSTRAP_TRACE=1 .install\worr_x86_64.exe +set basedir E:\Repositories\WORR\.install +set r_renderer opengl +set r_fullscreen 1 +set r_fullscreen_exclusive 1 +set win_fullscreen_capture_friendly 1 +set cl_font_draw_black_background 0 +set ui_acc_contrast 1 +set cl_debug_fonts 1 +font_dump_glyphs fonts\NotoSansKR-Regular.otf 8 +wait 120 +quit`
  - Completed a staged forced-fullscreen smoke run after the persistent menu-clear and text-wrapper changes.
  - Bootstrap trace reported `mode=borderless_fullscreen`, `exclusive=1`, and `capture_friendly=1`.
  - `.install\basew\fontdump\glyphs_011.txt` reported `kind: ttf`.
