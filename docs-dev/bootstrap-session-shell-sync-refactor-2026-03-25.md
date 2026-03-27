# Bootstrap Session-Shell Sync Refactor

Date: 2026-03-25

Task IDs: `DV-08-T12`

## Summary

The desktop bootstrapper was refactored toward the session-shell architecture
defined in `docs-dev/client-bootstrap-session-shell-architecture-2026-03-25.md`.

This pass does not implement the later host-owned-window contract yet, but it
does land the Phase 1 shape:

- bootstrap discovery now resolves the remote update manifest even when the
  local install is already on the same semantic version
- the bootstrap computes an explicit install synchronization plan before any
  worker/apply step
- same-version install drift is now treated as a repair/synchronization path,
  not as "current"
- apply-time file removal is now scoped to managed files tracked in the local
  install manifest instead of deleting arbitrary non-preserved files from the
  install tree
- metadata-only syncs can complete without downloading an update package

## Why The Refactor Was Needed

The previous bootstrap flow was still update-centric in two important ways:

1. It only considered a release actionable when the remote semantic version was
   newer than the local manifest version.
2. It removed any live file in the install root that was not present in the new
   manifest and not covered by the preserve allowlist.

That left two gaps against the session-shell design:

- a damaged local install on the same version could not self-heal through the
  normal bootstrap path
- removal semantics were broader than the intended manifest-convergence model

## Implementation Details

## Explicit sync planning

`src/updater/bootstrap.cpp` now introduces an explicit install sync plan:

- install actions are classified as add vs refresh
- removals are tracked separately
- version/metadata-only updates are represented explicitly

The bootstrap computes this plan after loading:

- the local install manifest
- the remote release manifest

The plan becomes the source of truth for:

- whether a pending update/repair is actionable
- how the prompt is phrased
- whether a package download is required
- which file mutations the apply path is allowed to perform

## Same-version repair support

The bootstrap now evaluates a remote payload as actionable when either:

- the remote semantic version is newer than the local manifest version
- the version is the same but the local install still requires synchronization

That enables a repair/self-heal path through the ordinary bootstrap flow for
cases such as:

- missing managed files
- stale local install manifests
- metadata drift between local and remote manifests

## Safer removal semantics

The apply path no longer scans the entire install tree and deletes every
non-preserved file that is absent from the target manifest.

Instead, removal is now constrained to files that were tracked in the previous
local install manifest and are absent from the new manifest.

That matches the intended synchronization contract:

- remove stale managed release files
- do not delete unrelated local content just because it is not in the shipped
  payload

## Metadata-only synchronization

If the new release manifest only changes install metadata and does not require
any file copy operations, the worker can now complete the synchronization
without downloading and extracting the package archive.

That keeps the sync path aligned with the idea that the bootstrap should
converge the install to the authoritative manifest, not blindly overlay
archives.

## User-facing behavior changes

Client/server bootstrap prompts now distinguish between:

- versioned updates
- same-version repair/synchronization

The bootstrap status strings were updated accordingly:

- "synchronize the install" replaces several update-only messages in the worker
  handoff/apply path
- repair completions use dedicated success messaging instead of always claiming
  that a newer build was installed

## Validation

Validated locally with:

- `meson compile -C builddir-win-bootstrap-hosted worr_x86_64 worr_ded_x86_64 worr_updater_x86_64`
- `python -m unittest discover -s tools/release/tests -v`
- `python tools/refresh_install.py --build-dir builddir-win-bootstrap-hosted --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools/release/package_platform.py --input-dir .install --output-dir build-audit/release-session-shell --platform-id windows-x86_64 --repo themuffinator/WORR --channel stable --version 1.2.3 --commit-sha session-shell --build-id local-session-shell`
- `python tools/release/prepare_role_install.py --input-dir .install --output-dir build-audit/installs/server-session-shell --platform-id windows-x86_64 --role server --repo themuffinator/WORR --channel stable --version 1.2.2 --commit-sha session-shell-install --build-id local-session-shell-install`
- manual drift injection:
  - deleted `build-audit/installs/server-session-shell/worr_ded_engine_x86_64.dll`
- `python tools/release/server_bootstrap_update_smoke.py --install-dir build-audit/installs/server-session-shell --release-root build-audit/release-session-shell --action install --port 8781 --timeout 180 --wait 60`

Observed result:

- the dedicated bootstrap detected the pending synchronization
- the worker repaired the deliberately missing managed engine DLL
- `worr_install_manifest.json` advanced from `1.2.2` to `1.2.3`
- the repaired install relaunched through the public bootstrap path

## Remaining Work

`DV-08-T12` is still not complete.

This change does not yet implement:

- a long-lived bootstrap-owned client window
- engine adoption of a host-provided native window/surface
- seamless same-window fullscreen handoff between shell UI and engine render

Those remain the next phase if WORR is to fully match the session-shell design.
