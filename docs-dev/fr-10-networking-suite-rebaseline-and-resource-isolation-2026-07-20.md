# FR-10 networking-suite rebaseline and heavyweight-test isolation

Date: 2026-07-20  
Project tasks: `FR-10-T04`, `FR-10-T06`, `FR-10-T09` (supporting
`FR-10-T07`)  
Status: integration repair complete; no project-task closure

## Purpose

Restore one trustworthy, fully green networking baseline before extending the
native event adapter to another production service family. This work repairs
test-target composition, removes fixture cvar aliasing, proves the reason for
one deterministic production-corpus digest change, and isolates the three
resource-heavy acceptance rows from parallel contention. It does not weaken a
workload, timeout, semantic comparison, or `FR-10-T14/T15` acceptance floor.

## Repaired target and fixture composition

The full suite exposed two link/composition defects that focused incremental
builds had hidden:

- `native_event_virtual_link_test` now compiles the canonical snapshot
  timeline implementation and includes `src/game/bgame`, matching the code it
  exercises. Its fixture also supplies the required `cgame_import_t cgi`
  storage.
- `cgame_native_event_presenter_test` links the same zlib dependency as the
  production-facing cgame code.
- `native_snapshot_production_virtual_link_test` supplies the inert adaptive
  input output seam and gives the input-batch mode, input-batch offer, event
  presentation ownership, snapshot timeline ownership, and public capability
  offer separate cvar storage. Previously, newly registered derived cvars
  aliased the master native-mode fixture and silently changed the exact public
  offer from `0x57` to `0x03`.

Windows Meson regeneration uses the repository resource-compiler wrapper:

```powershell
$env:WINDRES = (Resolve-Path -LiteralPath 'tools\rc.cmd').Path
```

No production resource path was bypassed.

## Parser and scheduling corrections

`run_fr10_t04_acceptance.py` now expects canonical child schema v42, matching
the accepted child evidence. The direct production corpus, its `FR-10-T06`
parent, and the million-command `FR-10-T09` parent are registered with
`is_parallel: false`. They still execute their complete workloads and retain
their original timeouts; serialization prevents a 100,000-snapshot corpus
from competing with the million-command probe for the same host resources.

The million-command probe passes directly in 220.703 seconds and through the
registered full suite in 242.88 seconds. Its semantic digest remains
`a9685d1ac8f40ef6`.

## Production corpus rebaseline

The production corpus runner first validates both 100,000-snapshot results and
requires byte-identical normalized reports, then checks the manifest golden.
Both valid runs produced the new deterministic digest
`ba519ae7bdd1db74`. Object-level relinking isolated the change to two intended
production corrections:

1. controlled-entity generation provenance in `snapshot_q2proto.cpp`;
2. the exact armed snapshot capability/readiness bundle `0x57` replacing the
   obsolete aliased fixture value `0x03`.

Relinking both corresponding pre-correction objects reproduced the former
`c6aee48df85341ab` digest. Metrics at 100, 1,000, and 10,000 snapshots were
otherwise identical except for server-to-client delivery bytes caused by the
changed wire/provenance content. The manifest golden is therefore updated to
`ba519ae7bdd1db74`; this is a forensic rebaseline, not an unexplained golden
refresh.

Current strict two-run evidence:

- `.tmp/networking/native_snapshot_production_corpus/evidence.json`
- schema `worr.native_snapshot_production_corpus.evidence.v1`
- status `ok`, repeat count 2, 100,000 requested and accepted snapshots
- semantic digest/golden `ba519ae7bdd1db74`
- normalized-report SHA-256
  `2c17ac3aaf2a5853b7eb217533df1675e3c3868910f5565314465b382ff91db3`
- evidence SHA-256
  `003be94a4c7db60d8e1b1b4a2f0fc3d9beacc202b3ae484a267657bad6320c8b`
- server-to-client delivery bytes `204601290`

The refreshed `FR-10-T06` parent passed 12 compiled gates twice, the separate
two-run 100,000-snapshot offline corpus, the production corpus above, three
negative probes, and the maximum-capacity budget row. Its evidence is
`.tmp/networking/fr10_t06_acceptance.json`, schema
`worr.networking.fr10-t06-acceptance-evidence.v1`, SHA-256
`5b982923a48599dc5fe46221345d8c554ec8479b2074d76e705ba2d2596aa1ed`.

## Final baseline

The complete command was:

```powershell
meson test -C builddir-win --suite networking --print-errorlogs
```

Result: **199/199 passed in 760.8 seconds**. The heavyweight rows completed as
follows:

- `network-fr10-t09-acceptance`: 242.88 s;
- `network-native-snapshot-production-corpus`: 234.04 s;
- `network-fr10-t06-acceptance`: 253.51 s.

The log is `builddir-win/meson-logs/testlog.txt`. `q2proto/` was not modified.
This restores the pre-extension baseline only. `FR-10-T04` and `FR-10-T07`
remain In Progress, and FR-10 remains **8/16 complete**.

## Superseding keyed-POI slice revalidation

The later event-model-revision-2 keyed-POI slice supersedes the word
"current" in the evidence snapshot above without changing the serialized
production golden. Its final 201/201 suite records offline parity digest
`6451c75bdb523477`, production digest `ba519ae7bdd1db74`, production evidence
SHA-256
`9302fd5cb40b1cfbf774e0f7115ec03618dde763c056b67ff71cb0f5d7f5c945`,
and T06 parent evidence SHA-256
`781b1f1edd147b2218423ac192c5047cd77baef8feaf5233d8c1bae933963782`.
See `docs-dev/fr-10-t04-t05-t07-keyed-poi-native-shadow-2026-07-20.md`.
