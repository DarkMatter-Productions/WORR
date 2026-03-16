# `basew` Gamedir + Arch-Suffixed Runtime Layout (2026-03-16)

Task IDs: `DV-08-T05`, `DV-08-T06`, `DV-08-T07`

## Summary
WORR runtime packaging now uses a single canonical game directory, `basew`,
across local staging, nightly/stable archives, and the Windows MSI.

This same change set also standardizes shipped binary names around explicit
architecture suffixes:

- `worr_x86_64(.exe)`
- `worr_ded_x86_64(.exe)`
- `worr_updater_x86_64.exe`
- `cgame_x86_64`
- `sgame_x86_64`

Repo assets now have one authored source (`assets/`) and one staged runtime
representation (`.install/basew/pak0.pkz`).

## Problems Fixed
Before this revision:

1. `.install/` still used `baseq2/`, while published archives had already
   drifted toward a `worr/` release-only layout.
2. `tools/stage_install.py` copied loose asset trees into `.install/<game>/`
   and `tools/package_assets.py` then repackaged the same content again.
3. Unix client archives previously needed a `bin/worr` workaround because the
   executable name collided with the published `worr/` gamedir.
4. Root executables still used unsuffixed names (`worr`, `worr.ded`,
   `worr_updater`) while renderer libraries already used arch-suffixed names.
5. Updater defaults, manifests, CI, and local tooling still referenced the old
   `baseq2`/`worr` split and the old executable names.

## Implementation
### 1. Canonical `basew` gamedir
- `meson_options.txt` now defaults `base-game` to `basew`.
- CI configure steps now explicitly pass `-Dbase-game=basew -Ddefault-game=basew`.
- Local staging, release layout staging, validation, and packaging defaults now
  all target `basew`.
- Runtime gameplay defaults that still advertised or resolved `baseq2`/`worr`
  as the packaged WORR gamedir were updated to `basew`.

### 2. Single staged asset pack
- `tools/stage_install.py` no longer copies repo assets loosely into `.install/`.
- `assets/` remains the canonical source tree.
- `tools/package_assets.py` now emits `.install/basew/pak0.pkz`.
- `tools/refresh_install.py` stages runtime files once, packages `pak0.pkz`
  once, and validates the resulting `.install/basew/` tree directly.

This removes the old `.install/baseq2/worr-assets.pkz` plus
`.install/.release/worr/pak0.pkz` dual-pack staging model.

### 3. Release layout simplification
- Published archives no longer translate `baseq2/` into `worr/`.
- `tools/release/stage_release_layout.py` now preserves a single `basew/`
  gamedir in the published output.
- Because the client executable is now `worr_x86_64`, Linux/macOS no longer
  need the older `bin/worr` relocation workaround.

### 4. Arch-suffixed binaries everywhere
- Meson executable targets now build:
  - `worr_x86_64`
  - `worr_ded_x86_64`
  - `worr_updater_x86_64`
- Meson game module targets now build:
  - `cgame_x86_64`
  - `sgame_x86_64`
- `src/common/gamedll.c` now loads game modules with the same underscore +
  arch naming convention used by the built DLLs/shared libraries.
- Windows version resources were updated so original/internal filenames match
  the new runtime binary names.

### 5. Updater + tooling alignment
- `src/updater/worr_updater.c` now defaults to:
  - `launch_exe = worr_x86_64.exe`
  - `preserve = basew/...`
  - `worr_updater_x86_64.exe`
- `tools/package_release.py` preserve defaults now target `basew`.
- `tools/release/targets.py` release manifests now require:
  - arch-suffixed root binaries
  - `basew/pak0.pkz`
  - `basew/cgame*` / `basew/sgame*` as appropriate
- `.vscode/tasks.json` and `.vscode/launch.json` now point at the new staged
  gamedir and executable names.

## Validation
Validation performed locally after the change set:

```powershell
python -m py_compile tools/package_assets.py tools/package_release.py tools/refresh_install.py tools/release/targets.py tools/release/validate_stage.py tools/release/stage_release_layout.py
```

Recommended follow-up validation for the active build trees:

```powershell
meson setup builddir --reconfigure -Dbase-game=basew -Ddefault-game=basew
meson compile -C builddir
python tools/refresh_install.py --build-dir builddir --install-dir .install --base-game basew --platform-id windows-x86_64
python tools/release/package_platform.py --input-dir .install --output-dir release-test --platform-id windows-x86_64 --repo themuffinator/WORR --channel nightly --version 0.1.0 --commit-sha local --build-id local
python tools/release/verify_artifacts.py --artifacts-root release-test --platform-id windows-x86_64
```

Expected layout after refresh/package:

- `.install/worr_x86_64(.exe)`
- `.install/worr_ded_x86_64(.exe)`
- `.install/worr_updater_x86_64.exe` (Windows bootstrapper builds)
- `.install/basew/pak0.pkz`
- release archives containing `basew/*` and no published `worr/` gamedir

## Files Changed
- `meson_options.txt`
- `meson.build`
- `src/common/gamedll.c`
- `src/game/sgame/g_local.hpp`
- `src/game/sgame/gameplay/g_main.cpp`
- `src/game/sgame/gameplay/g_spawn.cpp`
- `src/game/sgame/player/p_client.cpp`
- `src/game/cgame/ui/ui_page_servers.cpp`
- `src/client/ui/servers.cpp`
- `src/updater/worr_updater.c`
- `src/windows/res/*.rc`
- `src/unix/res/worr.default`
- `tools/stage_install.py`
- `tools/package_assets.py`
- `tools/refresh_install.py`
- `tools/package_release.py`
- `tools/release/targets.py`
- `tools/release/validate_stage.py`
- `tools/release/stage_release_layout.py`
- `.github/workflows/nightly.yml`
- `.github/workflows/release.yml`
- `.vscode/tasks.json`
- `.vscode/launch.json`
- `README.md`
- `BUILDING.md`
- `docs-user/getting-started.md`
- `docs-user/server-quickstart.md`
- `docs-dev/auto-updater-bootstrapper.md`
- `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`
