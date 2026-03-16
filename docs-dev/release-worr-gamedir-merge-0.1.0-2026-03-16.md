# Release `worr/` Gamedir Merge + `0.1.0` Stable Version (2026-03-16)

Task ID: `DV-08-T06`

## Summary
Published release artifacts now use a single `worr/` gamedir instead of shipping
runtime payload under `baseq2/` plus a second WORR pack under `worr/`.

This applies to:

- nightly prereleases
- stable GitHub releases
- the Windows MSI source layout

The repository release version source was also bumped to `0.1.0` in
`WORR_VERSION`.

## Problems Fixed
Before this change:

1. Published archives still exposed `baseq2/` even though the standalone WORR
   payload was already being published as `worr/pak0.pkz`.
2. The first release-layout helper only worked on Windows because Unix client
   archives cannot contain both a root executable named `worr` and a `worr/`
   directory.
3. Even if the archive layout was merged, release binaries still defaulted to
   an empty `game` cvar and `CGameDll_Load()` only searched `BASEGAME`, so a
   merged `worr/` release would not boot correctly by default.

## Implementation
### 1. Published release layout staging
Added `tools/release/stage_release_layout.py` as the release-only layout bridge
between `.install/` and published artifacts.

It now:

- copies the normal root runtime files
- merges `.install/baseq2/` into published `worr/`
- merges `.install/.release/worr/` into that same published `worr/`
- supports relocating conflicting root files (used for Unix `worr`)

`tools/release/package_platform.py` now stages this layout before creating each
client/server archive, so release manifests are generated against the final
published tree instead of the local `.install/` tree.

### 2. Unix-safe launcher handling
Linux and macOS client archives now relocate the client launcher from root
`worr` to `bin/worr`.

That avoids the file/directory name collision with the merged `worr/` gamedir
while keeping the published archive layout consistent with the user request.

Server archives keep `worr.ded` at archive root because it does not collide with
the gamedir name.

`tools/release/targets.py` now models this explicitly:

- Windows client launch path: `worr.exe`
- Linux/macOS client launch path: `bin/worr`
- Linux/macOS staged install launch path remains `worr`

Artifact verification now checks against those final published paths.

### 3. Runtime boot correctness for merged releases
Release CI now configures builds with `-Ddefault-game=worr` in both
`.github/workflows/nightly.yml` and `.github/workflows/release.yml`.

That makes published binaries boot the merged `worr/` payload by default.

`src/common/gamedll.c` was also updated so `CGameDll_Load()` now prefers the
active `fs_game` directory before falling back to `BASEGAME`. This matches the
existing `sgame` behavior and lets the merged release layout load `cgame` from
`worr/`.

### 4. Stable release publishing
`.github/workflows/release.yml` already used the shared packaging path after the
previous `DV-08-T05` work. This follow-up keeps that path, now with the merged
`worr/` gamedir layout, and publishes versioned GitHub releases from
`WORR_VERSION`.

`WORR_VERSION` is now:

```text
0.1.0
```

## Validation
Local validation completed:

```powershell
python tools/release/verify_artifacts.py --artifacts-root release-test-worrmerge --platform-id windows-x86_64
python -m py_compile tools/release/stage_release_layout.py tools/release/package_platform.py tools/release/targets.py tools/release/validate_stage.py
meson compile -C builddir-msys2-run
meson compile -C builddir-linux-ci
```

Additional synthetic Unix layout validation is recommended and was designed into
the staging helper:

- root `worr` can be relocated to `bin/worr`
- merged release output still publishes `worr/`
- manifest verification can require `bin/worr` with `worr/*`

## Files Changed
- `WORR_VERSION`
- `.github/workflows/nightly.yml`
- `.github/workflows/release.yml`
- `src/common/gamedll.c`
- `tools/release/stage_release_layout.py`
- `tools/release/package_platform.py`
- `tools/release/targets.py`
- `tools/release/validate_stage.py`
- `README.md`
- `BUILDING.md`
- `docs-user/getting-started.md`
- `docs-user/server-quickstart.md`
- `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`
