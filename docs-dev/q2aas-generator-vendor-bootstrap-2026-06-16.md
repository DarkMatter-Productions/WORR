# Q2 AAS Generator Vendor Bootstrap

Date: 2026-06-16

Task IDs: `FR-04-T11`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice starts the Q2/Q2R AAS generator work by importing the pinned `TTimo/bspc` source snapshot into `tools/q2aas/` and wiring it into WORR as a standalone Meson executable named `worr_q2aas`.

The imported upstream source files are not edited in this slice. WORR-specific integration is limited to local build/documentation files beside the snapshot plus the root Meson option/subdir hook.

## Source and Credits

- Upstream source: `https://github.com/TTimo/bspc`
- Imported commit: `10d23c5ebd042ddc5d03e17de0f560f5076649dc`
- Fork lineage: `https://github.com/bnoordhuis/bspc`, audited at `6c11357e6d79a89e88cda2fe0e67c99a8923e116`
- License: GPL-2.0-or-later, with upstream `LICENSE` retained under `tools/q2aas/LICENSE`
- Contributor baseline: Ben Noordhuis, Chris Brooke, Joel Baxter, Thomas Koeppe, Timothee "TTimo" Besset, Victor Luchits, plus original id Software source headers where present

The active credit ledger is `docs-dev/q3a-botlib-aas-credits.md`.

## Implementation

- Added `option('q2aas', type: 'boolean', value: true, ...)` to `meson_options.txt`.
- Added `subdir('tools/q2aas')` to the root Meson build after project arguments are registered.
- Added `tools/q2aas/meson.build` with the upstream Makefile source set and include paths.
- Added `tools/q2aas/worr_q2aas_compat.h` as a force-included shim:
  - normalizes `_WIN32` to the upstream `WIN32` branch for Windows-only code paths;
  - declares the existing `COM_Compress` implementation for BSPC parser sources;
  - avoids editing imported upstream C files during the bootstrap.
- Added `tools/q2aas/README.WORR.md` with local vendor and validation notes.

The target is not installed yet. `.install/` staging impact was checked and intentionally deferred to `FR-04-T16`, because release policy for shipping the generator and/or generated `.aas` files still needs validation rules.

## Validation

Commands run:

```powershell
meson setup builddir-win --reconfigure
meson compile -C builddir-win worr_q2aas
builddir-win\tools\q2aas\worr_q2aas.exe
builddir-win\tools\q2aas\worr_q2aas.exe -help
```

Results:

- `worr_q2aas.exe` built successfully at `builddir-win\tools\q2aas\worr_q2aas.exe`.
- The executable prints the inherited BSPC usage text and exits cleanly with no arguments.
- `-help` is not an upstream-supported switch; the tool reports `unknown parameter -help` and then prints usage.
- Reconfigure still reports the pre-existing local Windows `zlib-ng` fallback resource-compiler warning, unrelated to the `q2aas` target.

## Follow-Ups

- Completed in `docs-dev/q2aas-generator-q2-preset-validation-2026-06-16.md`: add a WORR Q2 config preset under `tools/q2aas/cfg/`.
- Completed in `docs-dev/q2aas-generator-q2-preset-validation-2026-06-16.md`: start Q2/Q2R map conversion tests with outputs under `.tmp/q2aas/`.
- Add deterministic metadata and diagnostics once the generated `.aas` format path is confirmed.
- Decide release staging policy for the generator binary and generated AAS assets under `FR-04-T16`.
- Continue per-file provenance expansion when imported BSPC files are locally tailored.
