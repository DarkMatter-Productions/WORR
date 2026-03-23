# Multi-Monitor Settings Submenu and Primary-Display Default (2026-03-22)

## Related Task
- `FR-03-T09` - Complete multi-monitor settings hierarchy and monitor targeting behavior for fullscreen modes.

## Summary
- Added a dedicated multi-monitor settings submenu to both the legacy menu script and the JSON/cgame menu definition.
- Added `r_monitor_mode` as the archived renderer cvar that controls fullscreen monitor targeting:
  - `0` = primary monitor
  - `1` = selected monitor from `r_display`
  - `2` = span all monitors
- Changed monitor selection defaults so fullscreen now targets the primary monitor unless the user explicitly selects another monitor or span mode.

## Implementation Details

### Renderer cvar wiring
- Registered `r_monitor_mode` and legacy alias `vid_monitor_mode` in `src/client/renderer.cpp`.
- Hooked monitor-mode changes into the same display-dependent refresh path used by `r_display`, so changing monitor targeting rebuilds the modelist and reapplies fullscreen layout.
- Exported `r_monitor_mode` through `inc/client/video.h` for native video backends.

### SDL backend
- `src/unix/video/sdl.c` now resolves fullscreen placement through a monitor policy instead of “current window display”.
- Primary monitor is the default policy.
- Selected-monitor mode still accepts the existing `r_display` index/name input and falls back to the primary monitor with a warning on invalid values.
- Span-all mode now uses a borderless fullscreen window sized to the union of every connected SDL display and exposes a desktop-only fullscreen modelist entry, since per-monitor exclusive modes do not apply to a spanning desktop window.
- Borderless fullscreen on SDL now also uses monitor-aware bounds, which keeps selected-monitor and primary-monitor behavior aligned with the new submenu.

### Win32 backend
- `src/windows/client.c` now resolves target monitors through monitor enumeration instead of always using the active window monitor.
- Exclusive fullscreen mode lists and mode switches now operate against the chosen monitor’s device name via `EnumDisplaySettingsA` / `ChangeDisplaySettingsExA`.
- Borderless fullscreen now supports:
  - primary monitor
  - selected monitor
  - spanning the full virtual desktop across all monitors
- Span-all mode intentionally routes through the borderless path, because Win32 exclusive mode is per-display rather than multi-display.

### Menu changes
- `video` now links to a dedicated `multimonitor` submenu instead of embedding monitor controls inline.
- `options` includes a direct `multi-monitor` entry so the setting is accessible from the main settings hierarchy.
- The new submenu groups:
  - monitor mode
  - fullscreen type
  - selected display field

## Validation Notes
- The implementation was designed so invalid `r_display` values never strand fullscreen on a missing monitor; the backends warn and recover to the primary display.
- Span-all mode uses borderless fullscreen because that is the portable/common path available to both SDL and Win32 for a desktop-spanning presentation.
