# Bootstrap Windows Client Shared-Window Adoption

Date: 2026-03-25

Task IDs: `DV-08-T12`

## Summary

The Windows client bootstrap now takes the next step toward the session-shell
design by letting the hosted engine adopt the bootstrap-owned client window
instead of always creating a second native window.

This slice is intentionally narrow:

- Windows client only
- bootstrap SDL splash window only
- existing fallback behavior preserved when adoption is unavailable

It does not yet implement the wider cross-platform host-window contract, but it
does land a real same-window handoff path for the most immediate desktop target.

## What Changed

## Bootstrap host changes

`src/updater/bootstrap.cpp` now:

- keeps the client splash `SDL_Window` alive during handoff on Windows instead
  of always destroying it before `engine_main`
- exports the underlying Win32 `HWND` to the hosted engine through the
  process environment
- traces whether shared-window handoff was taken

The bootstrap still destroys its temporary renderer/texture resources before the
engine starts, so the engine is not competing with the splash renderer for the
same native window.

## Engine-side Win32 adoption

`src/windows/client.c` now supports adopting a bootstrap-provided window:

- reads the bootstrap `HWND` from the environment
- subclasses the existing window with `Win_MainWndProc`
- acquires its own `HDC`
- restores the original window procedure on shutdown
- avoids destroying the adopted window during engine shutdown

That keeps ownership of the native window with the bootstrap shell while still
letting the engine's existing Win32 event/input/render setup run against it.

## Why subclassing was required

Using the raw `HWND` alone was not enough. The client still depends on its own
window procedure for:

- keyboard translation
- mouse/raw input
- activation/fullscreen handling
- size/position notifications

The adopted-window path therefore subclasses the SDL-created window during the
engine lifetime and chains unhandled messages back to SDL's original window
procedure.

## Scope limits

This is not the full end state from the architecture document.

Still missing:

- Unix/macOS host-window adoption
- Vulkan/SDL backend flag negotiation for non-Windows adopted windows
- a post-engine shell return path that keeps the same window alive after the
  engine exits instead of just exiting the process

This change should be treated as the Windows client wedge that proves the
bootstrap-owned-window design is workable inside the current hosted-engine
layout.

## Validation

Validated locally with:

- `meson compile -C builddir-win-bootstrap-hosted`
- `python tools/refresh_install.py --build-dir builddir-win-bootstrap-hosted --install-dir .install --base-game basew --platform-id windows-x86_64`
- client smoke through the public bootstrap:
  - `.install/worr_x86_64.exe +set basedir E:/Repositories/WORR/.install +set r_renderer opengl +wait 120 +quit`
  - exit code `0`
- bootstrap trace confirmation:
  - `%TEMP%/worr-bootstrap-trace.log` recorded `LaunchEngineAndWait shared_window_handoff=1 ...`
- regression check of the dedicated update path after the same bootstrap changes:
  - refreshed `build-audit/release-session-shell`
  - prepared `build-audit/installs/server-session-shell`
  - deleted `worr_ded_engine_x86_64.dll`
  - `python tools/release/server_bootstrap_update_smoke.py --install-dir build-audit/installs/server-session-shell --release-root build-audit/release-session-shell --action install --port 8782 --timeout 180 --wait 60`

Observed result:

- the Windows client launched successfully through the public bootstrap using
  the shared-window handoff trace path
- the dedicated bootstrap updater still repaired the deliberately broken server
  install and advanced its install manifest from `1.2.2` to `1.2.3`
