# Q3A BotLib Q2 BSP Visibility Bridge

Date: 2026-06-17

Tasks: `FR-04-T12`, `DV-07-T06`

## Summary

This slice replaces the missing Q3A `AAS_inPVS` and `AAS_inPHS` runtime
callbacks with an active-map Quake II BSP visibility bridge.

The bridge decodes Q2 `IBSP38` leaf cluster IDs and the visibility lump's
compressed PVS/PHS rows. Q3A callers can now ask whether two points are in
potentially visible or potentially hearable clusters using Q2 map data.

## Architecture Notes

- This is a static Q2 BSP visibility bridge. It does not yet model dynamic
  area-portal state from doors or scripted map logic.
- `AAS_inPVS` and `AAS_inPHS` keep their Q3A callback signatures, but point
  lookup and cluster row decoding use Q2 BSP leaf/visibility data.
- If visibility data is unavailable, the callbacks return visible/hearable
  rather than over-pruning imported AAS behavior.
- The bridge depends on active-map Q2 BSP collision leaf data being loaded
  first, because point-to-cluster lookup walks the same BSP node/leaf tree used
  by `AAS_PointContents` and `AAS_Trace`.

## Implementation

- `src/game/sgame/bots/q3a/q3a_botlib_import.*`
  - Stores Q2 leaf `cluster` and `area` values.
  - Adds bridge-owned Q2 visibility-lump storage.
  - Decodes compressed Q2 PVS/PHS rows.
  - Implements Q3A `AAS_inPVS` and `AAS_inPHS`.
  - Reports visibility cluster count and PVS/PHS smoke status.
- `src/game/sgame/bots/botlib_adapter.*`
  - Adds visibility load/clear entry points and adapter status fields.
- `src/game/sgame/bots/bot_runtime.*`
  - Loads Q2 BSP visibility data after collision data and before AAS handoff.
  - Prints visibility telemetry through `sg_bot_debug_aas 2`.
- `docs-dev/plans/q3a-botlib-aas-port.md`
  - Updates Phase 2 callback, lifecycle, debug, docs, and runtime-smoke
    checklist items.
- `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`
  - Updates `FR-04-T12` and `DV-07-T06` progress.
- `docs-dev/q3a-botlib-aas-credits.md`
  - Updates provenance rows for the WORR-owned runtime, adapter, and import
    bridge work.

## Validation

Build:

```powershell
meson compile -C builddir-win sgame_x86_64
```

Install refresh and q2aas package preservation:

```powershell
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas --q2aas-stage-report .tmp\q2aas\stage-report.json --q2aas-package-report .tmp\q2aas\refresh-package-archive-report.json --q2aas-package-audit-report .tmp\q2aas\refresh-package-archive-audit-report.json
```

Dedicated runtime smoke:

```powershell
$env:WORR_LOG_LEVEL = 'debug'; .\.install\worr_ded_x86_64.exe +set logfile 1 +set logfile_name q3a_bsp_visibility_bridge_smoke +set logfile_flush 1 +set dedicated 1 +set game basew +set sg_bot_enable 1 +set sg_bot_debug_aas 2 +map mm-rage +wait 60 +quit *> .tmp\q3a_bsp_visibility_bridge_smoke_stdout.log; Remove-Item Env:WORR_LOG_LEVEL
```

Observed smoke lines:

- `Bot AAS: loaded maps/mm-rage.aas (areas=428, reachability=562, clusters=4)`.
- `q3a_bsp_visibility=Q3A BSP visibility load passed: maps/mm-rage.bsp clusters=303 smoke_cluster=0 pvs_visible=142 phs_visible=289`.
- `q3a_bsp_pvs_smoke=yes`.
- `q3a_bsp_phs_smoke=yes`.
- `ShutdownGame`.

## Credits

- Q3A callback shapes remain credited to id Software's Quake III Arena source
  baseline pinned in the credits ledger.
- Q2 BSP visibility structures follow the credited `TTimo/bspc` / id Software
  BSPC lineage already vendored under `tools/q2aas/`.
- The runtime bridge implementation is WORR-native and shares BSP ownership
  boundaries with the active-map collision bridge.

## Outstanding Work

- Add dynamic area-portal and door-state visibility policy if imported bot
  runtime paths need it.
- Imported movement prediction/drop/jump helpers are covered by
  `docs-dev/q3a-botlib-aas-movement-import-2026-06-17.md`; continue debug
  drawing and any dynamic area-portal policy needed by future runtime paths.
- Q3A AAS start-frame/runtime orchestration is covered by
  `docs-dev/q3a-botlib-aas-start-frame-2026-06-17.md`.
