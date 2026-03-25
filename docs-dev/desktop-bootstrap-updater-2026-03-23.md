# Desktop Bootstrap Updater

Superseded in binary naming/layout and normal launch flow by
`docs-dev/bootstrap-hosted-engine-libraries-2026-03-24.md`. This document
describes the first launcher/runtime split where the public executable names
were bootstrap stubs and the real runtimes lived under `bin/`.

Date: 2026-03-23

Task IDs: `DV-08-T09`, `FR-08-T04`, `DV-03-T06`, `DV-08-T01`, `DV-08-T03`

## Summary

WORR now uses a RenderDoc-style desktop bootstrap updater pattern across the
client and dedicated server launchers instead of the older Windows-only
standalone updater model. The shipped launchers (`worr_x86_64`, `worr_ded_x86_64`)
are now bootstrap processes, the real engine binaries live under `bin/`, and a
separate updater worker (`worr_updater_x86_64`) applies updates from a temporary
copy so the live launcher/runtime files can be replaced safely.

The implementation keeps the RenderDoc shape that matters for WORR:

- update discovery happens before runtime initialization
- install approval is explicit
- file replacement happens in a separate worker process
- the main application only starts after update handling is complete

The actual implementation is WORR-specific:

- GitHub Releases plus the WORR release index are the source of truth
- the client uses a branded splash based on `assets/art/logo.png`
- the dedicated server keeps a console-first UX
- the updater is built with SDL3, libcurl, JsonCpp, and `miniz`, not Qt

## Binary Layout

The desktop binaries are split into launcher/runtime pairs:

- client launcher: `worr_x86_64(.exe)`
- dedicated launcher: `worr_ded_x86_64(.exe)`
- client runtime: `bin/worr_runtime_x86_64(.exe)`
- dedicated runtime: `bin/worr_ded_runtime_x86_64(.exe)`
- apply worker: `worr_updater_x86_64(.exe)`

`tools/stage_install.py` now stages runtime binaries containing `_runtime_`
under `.install/bin/` while keeping the user-facing launchers and updater at
the install root.

## Bootstrap Flow

### Client

- The launcher creates the WORR splash immediately.
- The splash uses an embedded RGBA copy of `assets/art/logo.png` generated at
  build time by `tools/generate_png_rgba_header.py`.
- Update discovery, install prompt, download progress, extraction, and apply
  progress all run inside the splash.
- The splash remains visible until the client runtime signals readiness after
  the first successful frame.

### Dedicated Server

- The dedicated launcher uses the same backend policy and metadata.
- Status and approval prompts stay in the console.
- The launcher only exits after the dedicated runtime signals that startup
  completed.

### Runtime Readiness Contract

The launchers hand a one-shot ready-file contract to the runtime through:

- `WORR_BOOTSTRAP_READY_FILE`
- `WORR_BOOTSTRAP_READY_TOKEN`
- `WORR_BOOTSTRAP_BASEDIR`

The client writes readiness after `R_EndFrame()` in `src/client/screen.cpp`.
The dedicated runtime writes readiness after `Qcommon_Init()` in
`src/windows/system.c` and `src/unix/system.c`.

The bootstrap waits up to 15 seconds for that token before giving up and
leaving the child process running.

## Update Discovery and Versioning

`src/updater/bootstrap.cpp` now owns update discovery and version comparison.

- Versions are parsed as SemVer with prerelease comparison.
- Stable releases outrank prereleases of the same core version.
- Nightly versions like `X.Y.Z-nightly.YYYYMMDD.r########` are ordered by the
  prerelease identifiers, matching WORR nightly naming.
- Discovery is bounded to a 2 second connect timeout and a 5 second total
  metadata budget.
- Discovery flow is GitHub Releases API -> release index asset ->
  platform/role payload -> role manifest.

Two local metadata files are part of the updater contract:

- `worr_update.json`
- `worr_install_manifest.json`

`worr_update_state.json` caches the last check result and any pending newer
remote payload so a previously discovered required update can still block
launch even if the online check later fails.

## Apply Worker and Recovery

The apply worker validates and installs updates transactionally:

- verifies staged file hashes before touching the live install
- copies overwritten files into a rollback directory
- tracks newly created paths
- writes the new local install manifest last
- restores backed-up files if any step fails

This contributes the implementation half of `DV-08-T03`. The remaining gap is
live fault-injection smoke coverage against the built worker in CI.

## Release Contract

### Manual packages

Manual client/server packages remain role-specific.

### Update packages

Updater ZIP payloads are now full-install payloads for both roles so a client
or server update preserves the whole installed desktop tree instead of deleting
the other launcher/runtime pair during sync.

Each role still has explicit release-index metadata:

- `role`
- `launcher_exe`
- `runtime_exe`
- `update_manifest_name`
- `update_package_name`
- `local_manifest_name`

### Installer layout

Windows installer staging now uses a role-specific prepared install root built
by `tools/release/prepare_role_install.py` instead of harvesting the raw merged
`.install/` tree.

That prepared layout:

- filters to the client payload
- writes `worr_update.json`
- writes `worr_install_manifest.json`

This keeps the MSI aligned with the client archive/updater contract.

## Tooling and Workflow Changes

Updated tooling:

- `tools/release/targets.py`
- `tools/package_release.py`
- `tools/release/package_platform.py`
- `tools/release/compose_release_index.py`
- `tools/release/verify_artifacts.py`
- `tools/release/validate_stage.py`
- `tools/release/prepare_role_install.py`

Updated workflows:

- `.github/workflows/release.yml`
- `.github/workflows/nightly.yml`

Workflow changes include:

- bootstrapper enabled on Linux, macOS, and Windows
- libcurl enabled on all desktop release builds
- curl packages installed in Linux/macOS/MSYS2 setup steps
- Windows installer layout prepared through the role-specific install-root tool

## Tests and Validation

Added Python release-tool tests under `tools/release/tests/`:

- SemVer ordering for stable, prerelease, and nightly versions
- release index role payload parsing and malformed metadata rejection
- target-contract checks ensuring update payloads cover the full installed tree

Local validation performed:

- `python -m unittest discover -s tools/release/tests -v`
- `python -m py_compile ...` for the modified release/build scripts
- `python tools/refresh_install.py --build-dir builddir-mingw-bootstrap --install-dir .install-bootstrap --base-game basew --platform-id windows-x86_64`
- `python tools/release/package_platform.py --input-dir .install-bootstrap --output-dir release-bootstrap --platform-id windows-x86_64 --repo themuffinator/WORR --channel stable --version 0.1.0 --commit-sha localtestsha --build-id localtest`
- `python tools/release/verify_artifacts.py --artifacts-root release-bootstrap --platform-id windows-x86_64`
- `python tools/release/prepare_role_install.py --input-dir .install-bootstrap --output-dir .release-layout-bootstrap --platform-id windows-x86_64 --role client --repo themuffinator/WORR --channel stable --version 0.1.0 --commit-sha localtestsha --build-id localtest`
- `python -m mesonbuild.mesonmain compile -C builddir-mingw-bootstrap`

The MinGW validation pass also exposed and fixed an unrelated Windows-portability
bug in `inc/system/pthread.h`: MinGW now uses its native winpthreads headers
instead of the local SRW-lock shim that is still needed for non-MinGW Windows
toolchains.

## Known Limits

- I did not run a live GitHub-backed upgrade from an already-installed build in
  this change set.
- `DV-08-T03` still needs explicit CI fault-injection coverage for failed
  extraction/apply paths, even though the rollback code path is now implemented.
