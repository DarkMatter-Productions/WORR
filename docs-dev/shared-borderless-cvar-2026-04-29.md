# Shared Borderless Window Cvar (2026-04-29)

## Related Tasks
- `FR-03-T09` - Complete multi-monitor settings hierarchy and monitor targeting behavior for fullscreen modes.
- `DV-08-T12` - Convert the client bootstrap into a long-lived session shell that owns the display/window lifecycle.

## Summary
- Added `r_borderless` as the shared archived renderer/window cvar for borderless behavior across renderer backends.
- Replaced the menu-facing fullscreen type selector with the new tri-state `r_borderless` setting:
  - `0` = use exclusive fullscreen where the platform backend supports it
  - `1` = use borderless fullscreen in place of exclusive fullscreen
  - `2` = keep windowed mode borderless too
- Defaulted `r_borderless` to `1` so fullscreen requests use the capture-friendly borderless path by default.

## Implementation Details

### Shared cvar
- Registered `r_borderless` in `src/client/renderer.cpp` and exported it through `inc/client/video.h`.
- The cvar is normalized as an integral value and clamped to `[0, 2]` whenever it changes.
- `r_fullscreen_exclusive` remains as a no-archive compatibility mirror:
  - `r_borderless 0` mirrors to `r_fullscreen_exclusive 1`
  - `r_borderless 1` or `2` mirrors to `r_fullscreen_exclusive 0`
  - runtime changes to the legacy cvar still translate back into `r_borderless 0` or `1`

### Runtime behavior
- Win32 and SDL fullscreen paths now consult `r_borderless` before choosing exclusive fullscreen.
- `r_borderless 1` makes fullscreen mode use the existing monitor-aware borderless fullscreen path.
- `r_borderless 2` keeps ordinary windowed mode borderless while preserving the configured `r_geometry` size and position.
- X11 applies `_MOTIF_WM_HINTS` decoration hints when `r_borderless 2` is active in windowed mode.
- The bootstrap session shell now resolves `r_borderless` from config/command-line overlays so the splash/session window matches the engine's intended borderless state.

## Menu Changes
- Updated both menu definitions:
  - `src/client/ui/worr.menu`
  - `src/game/cgame/ui/worr.json`
- The Video menu now exposes `borderless` with `off`, `fullscreen`, and `always` values.
- The Multi-Monitor Settings submenu keeps the same selector for users tuning monitor targeting and fullscreen placement together.

## Compatibility Notes
- `win_fullscreen_capture_friendly` is retained as a no-archive legacy cvar but no longer overrides the shared setting.
- Existing `r_fullscreen_exclusive` commands remain usable at runtime, but new configuration should use `r_borderless`.
- No protocol, demo, or server compatibility changes are involved.
