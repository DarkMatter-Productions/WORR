# Launcher + Runtime Layout Revision

Superseded by `docs-dev/bootstrap-hosted-engine-libraries-2026-03-24.md`.

Date: 2026-03-24

Task IDs: `FR-02-T03`, `DV-08-T09`

## Summary

The first bootstrap-updater rollout overloaded the public engine executable
names as launcher stubs:

- `worr_x86_64(.exe)`
- `worr_ded_x86_64(.exe)`

The real runtimes moved under `bin/`, which made the staged tree harder to
understand, made local debugging awkward, and obscured when the splash/update
path was supposed to run.

This revision makes the contract explicit again:

- `worr_x86_64(.exe)`: client runtime
- `worr_ded_x86_64(.exe)`: dedicated runtime
- `worr_launcher_x86_64(.exe)`: update-aware client launcher
- `worr_ded_launcher_x86_64(.exe)`: update-aware dedicated launcher
- `worr_updater_x86_64(.exe)`: updater/apply worker

## Why This Was Changed

The previous layout created three practical problems:

1. The public executable names no longer launched the real engine.
2. Local debug tooling had to target hidden `bin/` runtimes to stay attached.
3. Local `.install/` trees often showed no splash/update behavior because the
   launcher short-circuited before the updater worker UI was even invoked.

That design was technically functional for role-specific shipped installs, but
it did not read cleanly in a combined developer staging tree.

## New Contract

### Public runtime binaries

The engine binaries are back at the install root under their normal public
names. Direct launching and debugging now target those files again.

### Explicit launcher binaries

Bootstrap/update behavior is now opt-in via dedicated launcher binaries. If a
user or shortcut wants the splash/update flow first, it launches the launcher
binary instead of the runtime binary.

### Updater worker

`worr_updater_x86_64(.exe)` remains the temporary-copy worker used for safe
replacement of launcher/runtime files during updates.

## Implementation Notes

- `meson.build` now emits separate launcher binaries instead of renaming the
  real client/server executables to `_runtime_` variants.
- `src/updater/client_launcher_main.cpp` and
  `src/updater/server_launcher_main.cpp` now target the root runtime names.
- `src/updater/bootstrap.cpp` now invokes the updater worker whenever the
  worker binary exists, even if update metadata is missing. This means local
  launcher runs still show the splash/status path instead of bypassing it.
- `tools/stage_install.py` now stages a flat root executable layout again and
  removes the legacy `bin/` runtime directory from refreshed installs.
- Release target metadata now records explicit launcher binaries alongside the
  root runtime binaries.

## Validation

Validated locally with:

- JSON/tooling sanity checks for release-target metadata and launch presets
- release-tool unit tests under `tools/release/tests/`
- local `.install/` staging refresh after the layout change
- direct runtime smoke launches for client and dedicated targets

## Outcome

The binary set is larger by two explicit launcher binaries, but each executable
now has a single obvious role:

- runtime binaries run the engine
- launcher binaries run splash/update-first startup
- updater binary performs the apply/update work
