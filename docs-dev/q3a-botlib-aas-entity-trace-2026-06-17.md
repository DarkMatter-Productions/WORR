# Q3A BotLib AAS Entity Trace Bridge

Date: 2026-06-17

Tasks: `FR-04-T12`, `DV-07-T06`

## Summary

This slice connects the imported Q3A `AAS_EntityCollision` path to a WORR-owned entity trace callback. The bridge is still deliberately narrow: Q3A C code calls a neutral callback ABI, `botlib_adapter.*` translates that ABI, and `bot_runtime.cpp` resolves the actual collision against WORR `gentity_t` state through `gi.clip`.

The runtime smoke now proves that a Q3A entity collision query can hit a synced WORR mover entity:

`q3a_entity_trace=Q3A AAS entity trace smoke passed: callback=yes attempts=1 hits=1 misses=0 failures=0`

## Implementation

- Added `Q3ABotLibImportTraceResult` and `Q3ABotLibImportEntityTraceCallback` to the Q3A bridge header, plus `Q3A_BotLibImport_SetEntityTraceCallback`.
- Replaced the no-hit `AAS_EntityCollision` stub with a callback-backed implementation that fills Q3A `bsp_trace_t` data, including hit fraction, end position, plane normal/dist/type/signbits, contents, and entity number.
- Added entity trace status fields and verbose `sg_bot_debug_aas 2` counters for callback registration, attempts, hits, misses, and failures.
- Added the C++ adapter-side trace result type and callback registration path.
- Added a WORR runtime callback that maps Q3A content masks to WORR contents masks and clips against `SOLID_BBOX` / `SOLID_BSP` entities with `gi.clip`.
- Corrected SOLID_BSP model numbers during entity snapshot sync. WORR stores server config model indices, while Q3A `AAS_UpdateEntity` expects inline BSP model numbers, so the sync path now mirrors the server hull conversion before handing model numbers to Q3A.
- Added a swept entity trace smoke after entity sync, so the smoke validates the real callback path against the Q3A-linked entity bounds instead of only checking registration.

## Validation

Build:

```powershell
meson compile -C builddir-win sgame_x86_64
```

Install refresh:

```powershell
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas
```

Runtime smoke:

```powershell
$env:WORR_LOG_LEVEL='debug'
try {
  .\.install\worr_ded_x86_64.exe +set logfile 1 +set logfile_name q3a_aas_entity_trace_smoke +set logfile_flush 1 +set dedicated 1 +set game basew +set sg_bot_enable 1 +set sg_bot_debug_aas 2 +map mm-rage +wait 60 +quit *> .tmp\q3a_aas_entity_trace_smoke_stdout.log
  $code = $LASTEXITCODE
} finally {
  Remove-Item Env:WORR_LOG_LEVEL -ErrorAction SilentlyContinue
}
exit $code
```

Observed in `.install\basew\logs\q3a_aas_entity_trace_smoke.log`:

- `Bot AAS: loaded maps/mm-rage.aas (areas=428, reachability=562, clusters=4)`
- `q3a_entity_sync=Q3A AAS entity sync passed: updated=18 unlinked=1006 skipped=0 failures=0 max=1024`
- `q3a_entity_trace=Q3A AAS entity trace smoke passed: callback=yes attempts=1 hits=1 misses=0 failures=0`

## Remaining Work

- Dynamic BSP leaf links now use active-map Q2 BSP leaves; see `docs-dev/q3a-botlib-aas-bsp-leaf-link-2026-06-17.md`.
- Decide final ownership for Q3A `Trace` / `EntityTrace` once the full runtime is imported; the current callback bridge is intentionally adapter-owned.
- Add bot movement and debug overlay consumers for the imported AAS collision and route data.
