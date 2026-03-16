# Release Archive Layout + WORR Asset Pack (2026-03-16)

Task ID: `DV-08-T05`

## Summary
Nightly and stable release packaging now produces role-specific archives instead of packaging the same full `.install/` tree twice.

The staging pipeline also now emits a second asset archive:

- local/runtime pack: `.install/baseq2/worr-assets.pkz`
- release-aligned mod pack: `.install/worr/pak0.pkz`

This keeps the existing local runtime layout intact while ensuring published release archives always carry a standalone WORR gamedir payload.

## Problems Fixed
Before this change:

1. `tools/release/package_platform.py` invoked `tools/package_release.py` for both `client` and `server` with no role-specific include/exclude rules.
2. The resulting client/server packages were effectively duplicates of the same staged `.install/` tree.
3. Verification only checked that expected artifact files existed; it did not validate the manifest contents for archive correctness.
4. Release staging only generated `.install/baseq2/worr-assets.pkz`, so published archives did not include a dedicated `worr/pak0.pkz` payload.

## Implementation
### 1. Dual asset-pack staging
`tools/refresh_install.py` now runs `tools/package_assets.py` twice:

- `baseq2/worr-assets.pkz`
- `worr/pak0.pkz`

`tools/release/validate_stage.py` was expanded to require both archives for release-target validation.

### 2. Role-specific package rules
`tools/release/targets.py` now defines payload rules per role:

- `client`
  - includes root client binaries, renderer libraries, updater payload, `baseq2/*`, and `worr/pak0.pkz`
  - excludes dedicated-server root binaries
- `server`
  - includes the dedicated server root binary, `baseq2/*`, and `worr/pak0.pkz`
  - excludes client-only payload such as renderer libraries, `cgame`, and `shader_vkpt`

`tools/release/package_platform.py` now forwards these include/exclude globs into `tools/package_release.py`.

It also only writes `worr_update.json` for the client package, avoiding a server manifest/config mismatch.

### 3. Manifest-content verification
`tools/release/verify_artifacts.py` now parses each generated manifest and validates:

- required paths for the role are present
- forbidden paths for the role are absent
- the manifest package name matches the expected release asset name

This closes the gap where a nightly could publish syntactically complete artifacts that still contained the wrong payload split.

### 4. Stable workflow parity
`.github/workflows/release.yml` now uses `tools/release/package_platform.py` instead of manually calling `tools/package_release.py` twice.

This keeps stable and nightly release packaging behavior aligned.

## Validation
Recommended local validation flow:

```powershell
python tools/refresh_install.py --build-dir builddir --install-dir .install --base-game baseq2 --platform-id windows-x86_64
python tools/release/package_platform.py --input-dir .install --output-dir release-test --platform-id windows-x86_64 --repo themuffinator/WORR --channel nightly --version 0.0.0-nightly.local --commit-sha local --build-id local
python tools/release/verify_artifacts.py --artifacts-root release-test --platform-id windows-x86_64
```

Expected outcomes:

- `.install/worr/pak0.pkz` exists
- client manifest includes `worr.exe`, `baseq2/cgame*`, `baseq2/sgame*`, `baseq2/worr-assets.pkz`, `worr/pak0.pkz`
- server manifest includes `worr.ded.exe`, `baseq2/sgame*`, `baseq2/worr-assets.pkz`, `worr/pak0.pkz`
- server manifest excludes `worr.exe`, renderer DLLs, `worr_update.json`, `baseq2/cgame*`, and `baseq2/shader_vkpt/*`

## Files Changed
- `tools/package_assets.py`
- `tools/refresh_install.py`
- `tools/release/package_platform.py`
- `tools/release/targets.py`
- `tools/release/validate_stage.py`
- `tools/release/verify_artifacts.py`
- `.github/workflows/release.yml`
- `README.md`
- `BUILDING.md`
- `docs-user/getting-started.md`
- `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`
