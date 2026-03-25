# Bootstrap-Hosted Engine Libraries

Date: 2026-03-24

Task IDs: `FR-02-T03`, `DV-08-T07`, `DV-08-T09`

## Summary

WORR now uses a cleaner bootstrap contract:

- `worr_x86_64(.exe)` is the user-facing client bootstrap executable
- `worr_ded_x86_64(.exe)` is the user-facing dedicated bootstrap executable
- `worr_updater_x86_64(.exe)` is the standalone updater/apply worker
- `worr_engine_x86_64(.dll/.so/.dylib)` is the client engine library
- `worr_ded_engine_x86_64(.dll/.so/.dylib)` is the dedicated engine library

Users launch the client and dedicated server through the bootstrap executables
only. The actual engine code is now hosted behind shared libraries instead of
public runtime executables.

## Why This Revision Was Needed

The explicit launcher/runtime split from the earlier 2026-03-24 revision fixed
one problem and introduced another:

- the updater path became visible again
- but the public binary set still read awkwardly
- local debug and launch tooling had to choose between the bootstrap path and
  the real runtime process
- the bootstrapper still spawned a temp worker even for ordinary dev-build
  startup, so the splash/update behavior remained hard to reason about

The requested contract was stricter and simpler:

- the engine artifacts should be shared libraries
- the user-facing binaries should always be the bootstrap layer

## New Binary Contract

### User-facing executables

The binaries users run are now:

- `worr_x86_64(.exe)`
- `worr_ded_x86_64(.exe)`

These are bootstrap hosts, not raw engine executables.

### Hosted engine libraries

The actual engine entry points now live in:

- `worr_engine_x86_64(.dll/.so/.dylib)`
- `worr_ded_engine_x86_64(.dll/.so/.dylib)`

The launcher loads these libraries in-process and invokes `WORR_EngineMain(...)`
directly.

### Updater worker

`worr_updater_x86_64(.exe)` still exists, but it is no longer part of ordinary
dev/no-update startup. It is now reserved for the safe file-replacement path:

- approved updates
- elevated Windows update flow
- any case where the live launcher/updater/engine files may need replacement

After a successful update, the temp worker now relaunches the public
`worr_*` / `worr_ded_*` bootstrap executable and exits, so the updated build is
again hosted by the canonical user-facing process instead of the temp worker.

## Implementation Notes

### Engine entry/export boundary

- `src/windows/system.c` and `src/unix/system.c` now export
  `WORR_EngineMain(...)` instead of defining the public executable entry point.
- `src/common/bootstrap.c` now exports
  `Com_SetBootstrapReadyCallback(...)`.
- `Com_BootstrapSignalReady()` now triggers the registered callback before
  writing the legacy ready-file token path, so the bootstrap host can dismiss
  its UI once the engine reports readiness.

### Bootstrap host behavior

- `src/updater/bootstrap.cpp` now loads the engine shared library with
  `SDL_LoadObject(...)` and resolves:
  - `WORR_EngineMain`
  - `Com_SetBootstrapReadyCallback`
- normal launcher startup now stays in the user-facing `worr_*` process
  instead of always jumping to a temp updater worker
- the splash UI now releases only its own SDL window/renderer during engine
  handoff, instead of tearing down the SDL video subsystem under the hosted
  engine
- the launcher only hands off to a copied updater worker when an approved
  update/install must be applied
- if a user declines an available update, the launcher now continues into the
  installed build instead of exiting immediately
- if `autolaunch` is disabled in `worr_update.json`, the worker installs the
  update and exits cleanly without relaunching the updated build

### Build and packaging contract

- `meson.build` now builds the client and dedicated engines as shared libraries
  and restores `worr_*` / `worr_ded_*` as the public launcher executable names
- `src/windows/meson.build` now attaches the Windows icon/manifest resources to
  the launcher executables instead of the hosted engine DLLs
- `tools/stage_install.py` now skips stale legacy `*_launcher_*` binaries when
  refreshing `.install/`
- release metadata and manifests now publish:
  - `launch_exe`
  - `engine_library`
  instead of the older `launcher_exe` / `runtime_exe` pair

## Validation

Validated locally with:

- `python -m unittest discover -s tools/release/tests -v`
- `meson setup builddir-win-bootstrap-hosted --native-file meson.native.ini -Dbase-game=basew -Ddefault-game=basew --wipe`
- `meson compile -C builddir-win-bootstrap-hosted`
- `python tools/refresh_install.py --build-dir builddir-win-bootstrap-hosted --install-dir .install --base-game basew --platform-id windows-x86_64`
- client smoke via bootstrap host:
  - `.install/worr_x86_64.exe +set basedir E:/Repositories/WORR/.install +set r_renderer opengl +wait 120 +quit`
  - exit code `0`
- dedicated smoke via bootstrap host:
  - `.install/worr_ded_x86_64.exe +set basedir E:/Repositories/WORR/.install +wait 60 +quit`
  - exit code `0`
- live-process sampling on the no-update path:
  - client sample showed only `worr_x86_64`
  - dedicated sample showed only `worr_ded_x86_64`

That confirms the normal developer startup path no longer depends on a temp
updater worker process just to launch the engine.
