# Q3A BotLib AAS Reachability Query Import

Date: 2026-06-17

Tasks: `FR-04-T10`, `FR-04-T12`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice imports Quake III Arena's `be_aas_reach.c` into the existing WORR
Q3A BotLib boundary. The previous runtime slice used a WORR-owned
`AAS_AreaReachability` shim while proving `be_aas_sample.c` could query a
loaded AAS world. That shim is now removed: `AAS_AreaReachability` is supplied
by the imported Q3A source.

The active-map smoke now loads packaged `maps/mm-rage.aas`, samples an area via
the imported Q3A AAS sampling code, and records the imported reachability query
result as `q3a_sample_reachability`.

This is still not route planning or bot movement. It is the first imported Q3A
reachability query running against the WORR-generated Q2 AAS payload.

## Imported Files

Direct import from `id-Software/Quake-III-Arena` commit
`dbe4ddb10315479fc00086f08e25d968b4b43c49`; original id Software GPL header is
retained.

| WORR path | Upstream path | SHA-256 |
|---|---|---|
| `src/game/sgame/bots/q3a/botlib/be_aas_reach.c` | `code/botlib/be_aas_reach.c` | `b5622a0e7c6d6dfbf8a82078f4629a3b2885d087eba434a47dedb83908c71548` |

No imported Q3A source text was edited.

## WORR Bridge Changes

`q3a_botlib_import.*` now carries enough temporary bridge surface for the exact
upstream reachability file to compile and answer the current read-only query:

- `aassettings` is initialized with conservative Q2-ish movement settings before
  AAS load.
- The previous WORR-owned `AAS_AreaReachability` implementation was removed.
- `Q3ABotLibImportSmokeStatus` now records `aasSampleReachability`.
- The runtime status line prints `q3a_sample_reachability=<n>`.

Temporary WORR-owned bridge functions were added for later BotLib runtime hooks
referenced by `be_aas_reach.c` but not yet exercised by this smoke. `AngleVectors`
and `Sys_MilliSeconds` have since been replaced by real bridge implementations
in `docs-dev/q3a-botlib-bridge-time-vector-2026-06-17.md`. The BSP entity/epair
helpers have since been replaced by the active-map Q2 BSP entity-lump bridge
recorded in `docs-dev/q3a-botlib-bsp-entity-bridge-2026-06-17.md`. The remaining
temporary stubs are:

- `AAS_Trace`
- `AAS_PointContents`
- `AAS_BSPModelMinsMaxsOrigin`
- `AAS_ClientMovementHitBBox`
- `AAS_PredictClientMovement`
- `AAS_HorizontalVelocityForJump`
- `AAS_RocketJumpZVelocity`
- `AAS_BFGJumpZVelocity`
- `AAS_DropToFloor`
- `AAS_PermanentLine`
- `AAS_DrawPermanentCross`
- `AAS_DrawArrow`

These stubs are deliberately local to the import bridge. They should be replaced
by real WORR/Q2 collision, movement, and debug-draw callbacks as the full AAS
runtime lands.

## Build Policy

`be_aas_reach.c` uses legacy Q3A expressions that trigger Clang's
`-Wabsolute-value` diagnostics for float arguments to `abs`. WORR keeps that
warning exception scoped to the internal `q3a_botlib_utility` object group with
`-Wno-absolute-value`, alongside the existing Q3A import warning policy.

## Validation

Build validation:

```powershell
meson compile -C builddir-win sgame_x86_64
```

Result:

- `be_aas_reach.c` compiled into `libq3a_botlib_utility.a`.
- `sgame_x86_64.dll` linked successfully with imported `AAS_AreaReachability`.

Install refresh:

```powershell
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas --q2aas-stage-report .tmp\q2aas\stage-report.json --q2aas-package-report .tmp\q2aas\refresh-package-archive-report.json --q2aas-package-audit-report .tmp\q2aas\refresh-package-archive-audit-report.json
```

Result:

- `.install` was refreshed with the rebuilt server-game DLL.
- `maps/mm-rage.aas` was re-injected into `.install\basew\pak0.pkz`.
- The q2aas archive audit and Windows staged payload validation passed.

Runtime smoke:

```powershell
$env:WORR_LOG_LEVEL = 'debug'; .\.install\worr_ded_x86_64.exe +set logfile 1 +set logfile_name q3a_aas_reach_smoke_final +set logfile_flush 1 +set dedicated 1 +set game basew +set sg_bot_enable 1 +set sg_bot_debug_aas 2 +map mm-rage +wait 30 +quit *> .tmp\q3a_aas_reach_smoke_final_stdout.log; Remove-Item Env:WORR_LOG_LEVEL
```

Relevant log evidence from
`.install\basew\logs\q3a_aas_reach_smoke_final.log`:

- `Bot AAS: loaded maps/mm-rage.aas (areas=428, reachability=562, clusters=4)`.
- `q3a_aas=Q3A AAS file load passed`.
- `q3a_sample=Q3A AAS area sample passed: area=3 point_area=3 cluster=1 presence=6 reachability=1`.
- `q3a_sample_reachability=1`.
- `ShutdownGame`.

## Outstanding Work

- The BSP inline model callback is now backed by
  `docs-dev/q3a-botlib-bsp-model-bridge-2026-06-17.md`.
- Replace temporary trace, point-contents, movement-prediction, and jump helpers
  with real WORR/Q2 collision and movement bridges.
- Replace debug-line stubs with WORR debug draw.
- Import or bridge the remaining Q3A AAS runtime files required for route
  queries, start-frame updates, and movement steering.
