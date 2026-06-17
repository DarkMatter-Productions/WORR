# WORR Q2 AAS Generator Vendor Notes

Date: 2026-06-16

Tasks: `FR-04-T11`, `FR-04-T16`, `DV-07-T06`

## Source Snapshot

`tools/q2aas/` vendors the `TTimo/bspc` source tree at commit `10d23c5ebd042ddc5d03e17de0f560f5076649dc`.

Upstream:

- `https://github.com/TTimo/bspc`
- Fork lineage recorded from `https://github.com/bnoordhuis/bspc`
- License: GPL-2.0-or-later per the upstream README and retained `LICENSE`

The upstream source snapshot is kept intact in this bootstrap slice. Do not edit imported upstream files without updating `docs-dev/q3a-botlib-aas-credits.md` and adding a local modification note.

## WORR Integration

WORR-native files added beside the snapshot:

- `meson.build`: builds the standalone `worr_q2aas` target.
- `worr_q2aas_compat.h`: force-included build shim for compiler/platform compatibility.
- `cfg/worr_q2.cfg`: first WORR/Q2 movement and player-hull preset.
- `validate_worr_q2aas.py`: local cfg/map smoke runner that keeps output under `.tmp/q2aas/`.
- `audit_worr_q2aas_stage.py`: staged AAS artifact audit helper for `.install/basew/maps/`.
- `validation_manifest.json`: staged-map validation matrix seed for repeatable generator smoke coverage.
- `worr_q2aas_q2trace.c/.h`: WORR-native Q2 BSP trace bridge used by BotLib reachability during generation.
- `README.WORR.md`: this vendor note.

The Meson target is controlled by `-Dq2aas=true` and is enabled by default. The generator executable is intentionally not installed or staged yet; generated AAS staging, packaging policy, and validation are tracked under `FR-04-T16`.

## Local Validation

Build the tool:

```powershell
meson compile -C builddir-win worr_q2aas
```

Smoke the executable:

```powershell
builddir-win\tools\q2aas\worr_q2aas.exe
```

Smoke the WORR/Q2 preset:

```powershell
meson compile -C builddir-win q2aas-config-smoke
```

Convert a staged BSP into scratch output:

```powershell
python tools\q2aas\validate_worr_q2aas.py --map .install\basew\maps\mm-rage.bsp
```

Require generated reachability and clusters:

```powershell
python tools\q2aas\validate_worr_q2aas.py --map .install\basew\maps\mm-rage.bsp --require-reachability
```

Run the staged validation matrix, require Q2 IBSP38 input, enforce the current
diagnostic gates, write deterministic AAS metadata sidecars, write the JSON
report, and verify invalid BSPs and invalid manifests fail cleanly:

```powershell
meson compile -C builddir-win q2aas-staged-smoke
```

Equivalent direct command:

```powershell
python tools\q2aas\validate_worr_q2aas.py --manifest tools\q2aas\validation_manifest.json --require-q2-bsp --require-reachability --require-clean-bsp-lumps --require-spawn-coverage --require-item-coverage --require-high-value-reachability --write-aas-metadata --invalid-input-smoke --manifest-schema-smoke --report-json .tmp\q2aas\validation-report.json
```

Validate and stage generated `.aas` files into `.install/basew/maps/`:

```powershell
meson compile -C builddir-win q2aas-stage-aas
```

Equivalent direct command:

```powershell
python tools\q2aas\validate_worr_q2aas.py --manifest tools\q2aas\validation_manifest.json --require-q2-bsp --require-reachability --require-clean-bsp-lumps --require-spawn-coverage --require-item-coverage --require-high-value-reachability --write-aas-metadata --stage-aas --stage-aas-dir .install\basew\maps --report-json .tmp\q2aas\stage-report.json
```

The current Q2 bridge is a first generator-side reachability path. It has passed
strict validation on the staged `mm-rage.bsp` smoke map through the manifest
matrix, emits `.tmp/q2aas/mm-rage.aas.meta.json` with input/tool/config/output
hashes, and invalid BSP smoke input now exits with a clear `unknown BSP format`
error. The staged smoke also preflights Quake II `IBSP` version 38 headers,
records BSPX marker offsets when present, summarizes Q2 brush contents, parses
spawn/item/mover/trigger entities, maps spawn/item origins to generated AAS
area bounds, and reports first-pass high-value pickup reachability from spawn
areas. The staged smoke now fails when BSP lump-table issues, spawn coverage
regressions, item coverage regressions, or high-value pickup reachability
regressions are present. The manifest also records minimum structural metrics
and travel counts for the staged map, so `q2aas-staged-smoke` catches drops in
AAS area count, reachability size, cluster count, walk routes, jump routes,
ladder routes, walk-off-ledge routes, and elevator routes. The manifest declares
schema `worr-q2aas-validation-manifest-v1`, and the validator rejects malformed
schema/version/task metadata, wrong gate types, unknown baseline names, and
non-integer thresholds before generation. The JSON report records the manifest
schema, task IDs, map counts, pending reference categories, and manifest errors.
The staged smoke also creates and removes a malformed scratch manifest to keep
the expected-failure path covered.
The stage target runs the same strict map validation gates before copying a
generated AAS file into `.install/basew/maps/`, and records the staged path and
hash in `.tmp/q2aas/stage-report.json`.

Validate archive-backed map extraction and conversion with a scratch pkz:

```powershell
meson compile -C builddir-win q2aas-package-map-smoke
```

Equivalent direct command:

```powershell
python tools\q2aas\validate_worr_q2aas.py --package-map-smoke --require-q2-bsp --require-reachability --require-clean-bsp-lumps --require-spawn-coverage --require-item-coverage --require-high-value-reachability --write-aas-metadata --report-json .tmp\q2aas\package-map-smoke-report.json
```

Manifest map entries may use either a loose BSP `path` or an archive-backed
`archive` plus `archive_member`. Archive members are extracted under
`.tmp/q2aas/packaged-maps/`, and the report records `map_source` provenance
including the archive path, archive hash, member name, member size, compressed
size, and extracted BSP path. Archive members must be relative POSIX-style
paths inside the archive; absolute paths, traversal components, and drive-root
style components are rejected by the manifest loader and covered by
`--manifest-schema-smoke`.

Audit staged `.aas` files against the stage report:

```powershell
meson compile -C builddir-win q2aas-stage-audit
```

Equivalent direct command:

```powershell
python tools\q2aas\audit_worr_q2aas_stage.py --report-json .tmp\q2aas\stage-report.json --stage-dir .install\basew\maps --require-staged-output --audit-report-json .tmp\q2aas\stage-audit-report.json
```

The audit verifies that each required staged output exists under
`.install/basew/maps/`, is non-empty, has a `.aas` extension, and matches the
hashes recorded in `.tmp/q2aas/stage-report.json`.

Package staged `.aas` files into `.install/basew/pak0.pkz`:

```powershell
meson compile -C builddir-win q2aas-package-aas
```

Equivalent direct command:

```powershell
python tools\q2aas\package_worr_q2aas_archive.py --report-json .tmp\q2aas\stage-report.json --install-dir .install --base-game basew --archive-name pak0.pkz --package-report-json .tmp\q2aas\package-archive-report.json
```

The package step reads the stage report, verifies the loose staged AAS hash,
and writes `maps/<map>.aas` into the base game archive. The current staged smoke
adds `maps/mm-rage.aas` to `.install/basew/pak0.pkz` while keeping the loose
`.install/basew/maps/mm-rage.aas` file available for debugging and audit
comparison.

Audit q2aas package readiness after staging:

```powershell
meson compile -C builddir-win q2aas-package-audit
meson compile -C builddir-win q2aas-package-archive-audit
```

Equivalent direct command:

```powershell
python tools\q2aas\audit_worr_q2aas_package.py --report-json .tmp\q2aas\stage-report.json --install-dir .install --base-game basew --archive-name pak0.pkz --audit-report-json .tmp\q2aas\package-audit-report.json
python tools\q2aas\audit_worr_q2aas_package.py --report-json .tmp\q2aas\stage-report.json --install-dir .install --base-game basew --archive-name pak0.pkz --require-archive-member --audit-report-json .tmp\q2aas\package-archive-audit-report.json
```

The package audit verifies that each staged AAS from the stage report is
represented in the local release payload either as a loose file under
`.install/basew/` or as a member of `.install/basew/pak0.pkz`. The archive
audit uses `--require-archive-member` and fails unless the packaged
`maps/<map>.aas` member matches the staged-output hash.

Refresh `.install/` while preserving generated q2aas AAS in the archive:

```powershell
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas --q2aas-stage-report .tmp\q2aas\stage-report.json --q2aas-package-report .tmp\q2aas\refresh-package-archive-report.json --q2aas-package-audit-report .tmp\q2aas\refresh-package-archive-audit-report.json
```

`refresh_install.py` rebuilds `pak0.pkz` from `assets/` before optional q2aas
packaging, so use `--package-q2aas-aas` whenever a refreshed local install
should retain generated AAS members.
When `--platform-id` is provided with `--package-q2aas-aas`, the refresh
workflow also passes the staged AAS archive member names and hashes from the
q2aas stage report into the generic release validator.

Validate the staged install directly while requiring the packaged AAS member:

```powershell
python tools\release\validate_stage.py --install-dir .install --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --required-archive-member maps/mm-rage.aas=6459585e3c15eaa4170e23ca7465fc8255bd95b9b59d42e8615c39a67b707f9c
```

The broader Q2 reference map set and runtime BotLib loading still need
validation before generated `.aas` files should be treated as shippable bot
navigation data.

Generated logs and map/AAS scratch files belong under `.tmp/q2aas/` once map conversion validation starts.
