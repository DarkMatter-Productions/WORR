# Bootstrap Client In-Process Sync Handoff

Date: 2026-03-25

Task IDs: `DV-08-T12`

## Summary

The Windows client bootstrap now keeps the same visible window through the
client autoupdate/synchronization path and into engine launch when the sync
plan does not require replacing the running client bootstrap executable.

This closes the immediate UX gap in the earlier refactor:

- create the client window once in the bootstrap shell
- run the approved client synchronization in that shell
- launch the hosted engine in the same window afterward

The old temp-worker relaunch path still exists, but it is now reserved for
cases where the running bootstrap itself would need replacement or when an
out-of-process boundary is otherwise required.

## What Changed

`src/updater/bootstrap.cpp` now distinguishes between:

- client sync plans that are safe to apply in the main bootstrap process
- plans that still require the external worker/relaunch path

The client launcher now stays in-process when:

- the role is `client`
- the bootstrap is still the launcher process, not the worker
- the sync plan does not touch the running `launch_relpath`

When that is true:

- the launcher no longer jumps to the temp worker just because an approved
  client sync exists
- the sync applies in the bootstrap process
- successful apply transitions directly into `LaunchEngineAndWait(...)`
- the adopted bootstrap-owned window is reused for engine startup

## Why This Was Needed

The earlier shared-window adoption work only solved half of the problem.

Before this change:

- the client could reuse the bootstrap-owned window on the no-update path
- but the approved update path still restarted into a temp worker before the
  engine launch

That broke the same-window contract during the exact path the session-shell
architecture is meant to improve.

## Validation

Validated locally with:

- `meson compile -C builddir-win-bootstrap-hosted`
- `python tools/refresh_install.py --build-dir builddir-win-bootstrap-hosted --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools/release/package_platform.py --input-dir .install --output-dir build-audit/release-session-shell --platform-id windows-x86_64 --repo themuffinator/WORR --channel stable --version 1.2.3 --commit-sha session-shell --build-id local-session-shell`
- prepared a same-version client install:
  - `python tools/release/prepare_role_install.py --input-dir .install --output-dir build-audit/installs/client-session-shell --platform-id windows-x86_64 --role client --repo themuffinator/WORR --channel stable --version 1.2.3 --commit-sha session-shell-client --build-id local-session-shell-client`
- manual drift injection:
  - deleted `build-audit/installs/client-session-shell/worr_engine_x86_64.dll`
- client in-process sync smoke:
  - `python tools/release/client_bootstrap_sync_smoke.py --install-dir build-audit/installs/client-session-shell --release-root build-audit/release-session-shell --expect-file worr_engine_x86_64.dll --port 8784 --timeout 180 --wait 120`

Observed result:

- the public client bootstrap restored the missing managed engine DLL
- the trace recorded `RunBootstrapFlow apply_in_process=1`
- the trace recorded `RunBootstrapFlow in_process_sync_launch_engine`
- the trace recorded `LaunchEngineAndWait shared_window_handoff=1 ...`

That confirms the client bootstrap can now synchronize and enter the engine
within the same bootstrap-owned window for the supported in-process path.

## Remaining Limit

If a client update must replace the running bootstrap executable itself, the
current architecture still needs an out-of-process apply boundary.

That means the new same-window contract is now true for safe in-process client
syncs, but not yet for bootstrap self-replacement.
