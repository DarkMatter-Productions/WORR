# Updater Pipeline Audit

Date: 2026-03-25

Task IDs: `DV-08-T09`, `DV-08-T11`

## Summary

This audit rechecked the desktop updater flow end to end after the bootstrap and
engine-library split landed.

The release/staging contract is coherent and the ordinary bootstrap-hosted
startup path remains correct. The previously unresolved Windows dedicated
approved-update handoff is now closed:

- release metadata, package contents, staged installs, and pending-update
  payloads are internally consistent
- update deferral continues into the installed build instead of forcing an exit
- bootstrap startup now normalizes the install root before handing it to the
  worker or using it for local path resolution
- Windows update apply now retries transient permission/share failures instead
  of relying on a separate write-probe precheck
- the Windows dedicated launcher now spawns approved-update workers and
  post-update relaunches via native `CreateProcessW` new-process-group handoff
  instead of the generic SDL process wrapper
- Windows dedicated handoff now supports a quiet bootstrap mode so
  worker/relaunch startup does not depend on transient launcher status output
- local Windows console-driven tests now complete successfully through the
  public dedicated bootstrap for both deferred and approved-update paths

## Confirmed Working Areas

### Release metadata and packaging

The packaging side of the updater pipeline now holds together under the current
bootstrap contract:

- `tools/release/package_platform.py` produced a complete Windows artifact set
- `tools/release/compose_release_index.py` generated a schema-3 release index
- `tools/release/verify_artifacts.py` validated the resulting artifact bundle
- `tools/release/prepare_role_install.py` produced role-scoped shipped install
  trees with the expected `worr_update.json` and
  `worr_install_manifest.json` payloads

### Bootstrap/runtime contract

The active contract remains:

- public executables:
  - `worr_x86_64(.exe)`
  - `worr_ded_x86_64(.exe)`
  - `worr_updater_x86_64(.exe)`
- hosted engine libraries:
  - `worr_engine_x86_64(.dll/.so/.dylib)`
  - `worr_ded_engine_x86_64(.dll/.so/.dylib)`

The launcher still hosts the engine library in-process for ordinary startup and
only escalates to the copied updater worker for approved file replacement.

### Deferred-update user path

The dedicated bootstrap console path was rechecked with a locally staged pending
update:

- the launcher reported the pending update
- declining the update no longer exited immediately
- the installed dedicated build continued to launch afterward

## Hardening Kept From This Audit

### Install-root normalization

`src/updater/bootstrap.cpp` now normalizes the install root before:

- forwarding it into worker invocations
- deriving local config/manifest paths
- relaunching the public bootstrap after an applied update

This removes trailing-separator ambiguity from the bootstrap/worker contract.

### Permission handling during apply

The updater no longer depends on a separate "is this install writable" probe.
Instead, the worker now:

- extracts the payload
- attempts the real file replacement work
- retries transient Windows permission/share failures during apply
- only requests elevation after a real permission-denied result survives the
  retry window

That is a more faithful representation of the actual update operation than the
previous up-front probe.

### Windows dedicated handoff hardening

The Windows dedicated launcher handoff was tightened further during this pass:

- the public dedicated bootstrap now launches the copied temp worker through a
  native `CreateProcessW` path with `CREATE_NEW_PROCESS_GROUP`
- the worker relaunch of the updated dedicated bootstrap now uses the same
  native path
- the worker invocation contract now carries an explicit quiet-bootstrap flag
  so the apply/relaunch handoff can skip transient bootstrap status UI when
  that output path is unstable
- opt-in `WORR_BOOTSTRAP_TRACE=1` tracing now records the launcher/worker
  boundary and early startup phases into `%TEMP%/worr-bootstrap-trace.log`

This narrowed the remaining failure from a generic "approved updates do not
apply" report to a much more specific launcher-child lifetime/output issue in
the local Windows console-driven harness, and the dedicated handoff is now
resolved by the native spawn path plus the quiet worker/relaunch mode.

## Outcome

The Windows dedicated approved-update path now behaves correctly in local
console-driven validation:

- the public bootstrap detects the pending update
- approving the update launches the copied temp worker with the expected
  bootstrap arguments, including quiet worker status mode
- the worker downloads, extracts, and applies the payload
- `worr_install_manifest.json` advances from `1.2.2` to `1.2.3`
- the worker relaunches the updated public bootstrap with
  `--bootstrap-skip-update-check`
- declining the update still launches the installed dedicated build without
  mutating the manifest
- the dedicated public-bootstrap path now has a deterministic local automation
  harness in `tools/release/server_bootstrap_update_smoke.py`

With those validations in place, the earlier `DV-08-T11` handoff gap is
considered closed.

## Validation

Validated locally with:

- `meson compile -C builddir-win-bootstrap-hosted`
- `python tools/refresh_install.py --build-dir builddir-win-bootstrap-hosted --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python -m unittest discover -s tools/release/tests -v`
- `python tools/release/package_platform.py --input-dir .install --output-dir build-audit/release-win-d --platform-id windows-x86_64 --repo themuffinator/WORR --channel stable --version 1.2.3 --commit-sha audit789 --build-id local-audit-3`
- `python tools/release/compose_release_index.py --artifacts-root build-audit/release-win-d --repo themuffinator/WORR --channel stable --version 1.2.3 --tag v1.2.3 --commit-sha audit789 --index-path build-audit/release-win-d/worr-release-index-stable.json --asset-list-path build-audit/release-win-d/assets.txt`
- `python tools/release/verify_artifacts.py --artifacts-root build-audit/release-win-d --platform-id windows-x86_64`
- `python tools/release/package_platform.py --input-dir .install --output-dir build-audit/release-win-k --platform-id windows-x86_64 --repo themuffinator/WORR --channel stable --version 1.2.3 --commit-sha audit1004 --build-id local-audit-9`
- `python tools/release/compose_release_index.py --artifacts-root build-audit/release-win-k --repo themuffinator/WORR --channel stable --version 1.2.3 --tag v1.2.3 --commit-sha audit1004 --index-path build-audit/release-win-k/worr-release-index-stable.json --asset-list-path build-audit/release-win-k/assets.txt`
- `python tools/release/verify_artifacts.py --artifacts-root build-audit/release-win-k --platform-id windows-x86_64`
- `python tools/release/package_platform.py --input-dir .install --output-dir build-audit/release-win-m --platform-id windows-x86_64 --repo themuffinator/WORR --channel stable --version 1.2.3 --commit-sha audit1006 --build-id local-audit-11`
- `python tools/release/compose_release_index.py --artifacts-root build-audit/release-win-m --repo themuffinator/WORR --channel stable --version 1.2.3 --tag v1.2.3 --commit-sha audit1006 --index-path build-audit/release-win-m/worr-release-index-stable.json --asset-list-path build-audit/release-win-m/assets.txt`
- `python tools/release/verify_artifacts.py --artifacts-root build-audit/release-win-m --platform-id windows-x86_64`
- `python tools/release/server_bootstrap_update_smoke.py --install-dir build-audit/installs/server-v26 --release-root build-audit/release-win-m --action exit --port 8777 --timeout 120 --wait 60`
- `python tools/release/server_bootstrap_update_smoke.py --install-dir build-audit/installs/server-v27 --release-root build-audit/release-win-m --action install --port 8778 --timeout 120 --wait 60`
- dedicated deferral smoke through the public bootstrap with a staged pending
  update payload
- direct copied-temp-worker approved-update smoke against a staged server
  install, which advanced the install manifest to `1.2.3` and relaunched the
  public bootstrap
- repeated public dedicated-bootstrap approved-update repros with
  `WORR_BOOTSTRAP_TRACE=1`, which now show the copied worker parsing the quiet
  status flag, applying the payload successfully, and relaunching the updated
  public bootstrap

No updater-pipeline gap remains from this audit pass.
