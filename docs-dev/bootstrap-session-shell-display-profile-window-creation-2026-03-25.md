# Bootstrap Session-Shell Display Profile Window Creation

Date: 2026-03-25

Task IDs: `DV-08-T12`

## Summary

The client bootstrap no longer opens a fixed 960x540 placeholder splash and
only later hands a native window handle to the engine. It now resolves the
client's real session display profile before UI creation and builds the
bootstrap shell window in that same display mode from the start.

For the supported in-process client path, the sequence is now:

- resolve window/fullscreen state from the install's runtime config
- create the bootstrap shell window in that resolved state
- render updater/sync UX in that window
- hand the same window object to the hosted engine

This closes the main gap in the earlier "shared-window" work. The window is
now both shared and correctly shaped for the actual session.

## Problem

The previous implementation still created a generic SDL splash window:

- title `WORR`
- fixed size `960x540`
- always windowed
- unrelated to `r_fullscreen`, `r_geometry`, `r_display`, or
  `r_monitor_mode`

That meant the handoff could technically reuse the same `HWND` on Windows, but
the user still saw the wrong surface during bootstrap. In fullscreen client
installs there was no meaningful "same window/fullscreen object" continuity,
because the bootstrap-owned window was not the real session surface.

## Design

The bootstrap now treats window creation as part of session-state resolution
instead of as a splash-only concern.

### Display inputs

The bootstrap resolves client display state from, in order:

1. install root or `+set basedir` override
2. `basew/config.cfg`
3. `basew/autoexec.cfg`
4. forwarded `+set ...` command-line overrides

The relevant cvars are:

- `r_geometry`
- `r_fullscreen`
- `r_fullscreen_exclusive`
- `r_monitor_mode`
- `r_display`

### Session-shell modes

The bootstrap maps those inputs to one of four window modes:

- `windowed`
- `borderless_fullscreen`
- `exclusive_fullscreen`
- `span_borderless`

This keeps the bootstrap decision model aligned with the engine's existing
video policy instead of inventing a separate launcher-only mode system.

### SDL3 window creation

The bootstrap UI now uses SDL3's property-based window creation path:

- create a hidden window with `SDL_CreateWindowWithProperties(...)`
- create the SDL renderer while the window is still hidden
- apply the resolved window/fullscreen mode
- call `SDL_ShowWindow(...)` only after the renderer and mode are ready

That avoids the old "create a visible splash and then mutate it later"
behavior and follows SDL3's documented path for renderer-backed windows that
should avoid unnecessary recreation/flicker.

## Implementation

### `src/updater/bootstrap.cpp`

Added a new client session-shell display-profile pipeline:

- config parsing helpers for runtime cvars and forwarded `+set` overrides
- runtime-root resolution from `+set basedir`
- SDL display-selection helpers for `r_display` and `r_monitor_mode`
- fullscreen/window placement resolution before splash UI creation
- exclusive fullscreen mode selection using SDL display modes

`SplashUi` now:

- accepts a `SessionShellWindowConfig`
- resolves display bounds after SDL video initialization
- creates the window with `SDL_CreateWindowWithProperties(...)`
- applies windowed, borderless fullscreen, exclusive fullscreen, or span
  borderless placement before showing the window
- traces the resolved session shell mode and whether it had to fall back

`RunBootstrapFlow(...)` now loads the client session-shell config before
constructing the UI handle, so the shell window is correct from the first
visible frame.

### `tools/release/client_bootstrap_sync_smoke.py`

The client sync smoke now supports `--expect-window-mode`, allowing local
validation to assert that the bootstrap used the intended session-shell mode
in addition to the existing in-process sync and shared-window handoff markers.

## Validation

Validated locally with:

- `meson compile -C builddir-win-bootstrap-hosted worr_x86_64 worr_updater_x86_64`
- `python -m unittest discover -s tools/release/tests -v`
- `python tools/refresh_install.py --build-dir builddir-win-bootstrap-hosted --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools/release/package_platform.py --input-dir .install --output-dir build-audit/release-session-shell --platform-id windows-x86_64 --repo themuffinator/WORR --channel stable --version 1.2.3 --commit-sha session-shell --build-id local-session-shell`
- `python tools/release/prepare_role_install.py --input-dir .install --output-dir build-audit/installs/client-session-shell-modern4 --platform-id windows-x86_64 --role client --repo themuffinator/WORR --channel stable --version 1.2.3 --commit-sha session-shell-client --build-id local-session-shell-client`
- deleted `build-audit/installs/client-session-shell-modern4/worr_engine_x86_64.dll`
- `python tools/release/client_bootstrap_sync_smoke.py --install-dir build-audit/installs/client-session-shell-modern4 --release-root build-audit/release-session-shell --expect-file worr_engine_x86_64.dll --expect-window-mode exclusive_fullscreen --port 8788 --timeout 180 --wait 120`
- manual no-update launch validation:
  - `build-audit/installs/client-session-shell-modern4/worr_x86_64.exe --bootstrap-skip-update-check +set basedir E:/Repositories/WORR/build-audit/installs/client-session-shell-modern4 +set r_renderer opengl +wait 120 +quit`

Observed results:

- the bootstrap trace records `LoadClientSessionShellWindowConfig ... mode=exclusive_fullscreen`
- the splash trace records `SplashUi window_mode=exclusive_fullscreen ... fallback=0`
- the client repair smoke still records:
  - `RunBootstrapFlow apply_in_process=1`
  - `RunBootstrapFlow in_process_sync_launch_engine`
  - `LaunchEngineAndWait shared_window_handoff=1 ...`
- the no-update launch path also records:
  - `SplashUi window_mode=exclusive_fullscreen ... fallback=0`
  - `LaunchEngineAndWait shared_window_handoff=1 ...`

## Remaining Boundary

This resolves the visible session-shell continuity problem for the in-process
client path.

It does not remove the fundamental Windows process-boundary limitation for
bootstrap self-replacement. If a sync plan must replace the running launcher
binary itself, the system still needs an external worker and relaunch path.

The practical implication is:

- the stable path now behaves like a true session shell
- launcher self-replacement remains the remaining exception, not the norm
