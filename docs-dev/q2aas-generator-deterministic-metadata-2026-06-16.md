# Q2 AAS Deterministic Metadata Sidecars

Date: 2026-06-16

Tasks: `FR-04-T11`, `FR-04-T16`, `DV-07-T06`

## Summary

This round adds deterministic metadata around generated AAS files without
changing the upstream AAS v5 binary format. The validation harness now records
tool/config/input/output hashes, decodes Q2 BSP and AAS headers, validates Q2
`IBSP` version 38 inputs for staged smoke runs, and writes per-AAS sidecar JSON
files under `.tmp/q2aas/`.

## Format Decision

The AAS v5 header used by the Q3A BotLib loader contains only the ident,
version, source BSP checksum, and fixed lump table. Adding a new metadata lump
or changing the header would require a coordinated runtime loader extension and
could break vanilla AAS compatibility before the WORR BotLib runtime boundary is
ready.

For this phase, metadata is emitted as a sidecar:

- AAS binary: unchanged upstream-compatible `.aas`.
- Sidecar: `<map>.aas.meta.json`.
- Validation report: `.tmp/q2aas/validation-report.json`.
- Generation time: intentionally omitted; identity is recreated from hashes.

## Implementation

- `tools/q2aas/validate_worr_q2aas.py` now computes SHA-256 hashes for the
  generator executable, config, source BSP, and generated AAS file.
- The validator decodes Quake II BSP headers and can fail early with
  `--require-q2-bsp` if a selected map is not `IBSP` version 38.
- The validator scans after the normal Q2 lump range for `BSPX` markers and
  records detected offsets for follow-up Q2R/BSPX tolerance work.
- The validator decodes AAS v5 headers, including the obfuscated post-version
  header region, and records the AAS source BSP checksum and lump table.
- `q2aas-staged-smoke` now passes `--require-q2-bsp` and
  `--write-aas-metadata`.

## Validation Results

Commands run:

```powershell
python -m py_compile tools\q2aas\validate_worr_q2aas.py
meson compile -C builddir-win q2aas-staged-smoke
```

`q2aas-staged-smoke` passed and wrote:

- `.tmp/q2aas/mm-rage.aas`
- `.tmp/q2aas/mm-rage.aas.meta.json`
- `.tmp/q2aas/validation-report.json`

Observed staged metadata:

- `mm-rage.bsp` header: `IBSP`, version `38`, no lump-table issues.
- Standard Q2 lump range ends at offset `766953`.
- BSPX marker: detected at offset `766956`, immediately after the standard Q2
  lump range.
- AAS header: `EAAS`, version `5`, valid AAS header.
- AAS source BSP checksum: signed `-1381807757`, unsigned `2913159539`.
- Config SHA-256: `b8737520ea58eace58c08162226378c83d5e895dc040ac13d28eac7da6f93f33`.

## Current Limits

The sidecar is currently a validation/build artifact, not a packaged runtime
contract. Packaging work under `FR-04-T16` still needs to decide whether
sidecars are shipped, folded into package manifests, or replaced by a WORR AAS
metadata extension once the runtime loader is under local control.
