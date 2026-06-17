# Q3A BotLib AAS Sample Query Import

Date: 2026-06-17

Tasks: `FR-04-T10`, `FR-04-T12`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice imports Quake III Arena's `be_aas_sample.c` into the existing Q3A
BotLib boundary. The previous slice proved that WORR can load the generated Q2
AAS payload through Q3A's native file loader. This slice proves that imported
Q3A sampling code can query that loaded world.

The runtime now runs a narrow read-only smoke immediately after AAS load:

- `AAS_AreaInfo(area, &info)` reads loaded area metadata.
- `AAS_PointAreaNum(info.center)` resolves an AAS BSP tree point query.
- The adapter records the sampled area, resolved point area, cluster, and
  presence type for `sg_bot_debug_aas 2`.

This is still not full routing or movement. It is the first imported AAS query
working against the generated Q2 map AAS.

## Imported Files

Direct import from `id-Software/Quake-III-Arena` commit
`dbe4ddb10315479fc00086f08e25d968b4b43c49`; original id Software GPL header is
retained.

| WORR path | Upstream path | SHA-256 |
|---|---|---|
| `src/game/sgame/bots/q3a/botlib/be_aas_sample.c` | `code/botlib/be_aas_sample.c` | `41a5699e9c23c772f2937cad3b20cb4f4a40c17e17a214e9e26049e0d59f1330` |

No imported Q3A source text was edited.

## WORR Bridge Changes

`q3a_botlib_import.*` now exposes sample-query status through
`Q3ABotLibImportSmokeStatus`:

- `aasSampleAttempted`
- `aasSamplePassed`
- `aasSampleArea`
- `aasSamplePointArea`
- `aasSamplePresenceType`
- `aasSampleCluster`
- `aasSampleMessage`

Temporary WORR-owned shims were added so the exact imported sample file can
compile before the full BotLib runtime lands:

- `bot_developer = 0`, matching the non-developer bridge mode.
- `VectorNormalize`, because `be_aas_sample.c` references the Q3 shared helper
  even though the current smoke path does not call movement tracing.
- `AAS_EntityCollision`, currently a no-hit stub used only if later callers use
  trace/entity collision paths before the real BSP/entity bridge is imported.
- `AAS_AreaReachability` was temporary in this slice and has since been replaced
  by the imported Q3A `be_aas_reach.c` implementation; see
  `docs-dev/q3a-botlib-aas-reach-query-2026-06-17.md`.

These shims are deliberately local to the import boundary and should be removed
or replaced as the full Q3A AAS runtime files are imported.

## Runtime Behavior

After `Q3A_BotLibImport_LoadAASBuffer` succeeds:

1. The bridge iterates loaded AAS areas.
2. It calls imported `AAS_AreaInfo`.
3. It calls imported `AAS_PointAreaNum` on the area center.
4. It accepts the first center that resolves to a non-zero AAS area.
5. If no area resolves, the AAS runtime gate fails with the sample message.

This keeps area sampling as part of the same bot AAS readiness gate as file
loading.

## Validation

Build validation:

```powershell
meson compile -C builddir-win sgame_x86_64
```

Result:

- `be_aas_sample.c` compiled into `libq3a_botlib_utility.a`.
- `sgame_x86_64.dll` linked successfully with the sample-query bridge.

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
$env:WORR_LOG_LEVEL='debug'; .\.install\worr_ded_x86_64.exe +set logfile 1 +set logfile_name q3a_aas_sample_smoke +set logfile_flush 1 +set dedicated 1 +set game basew +set sg_bot_enable 1 +set sg_bot_debug_aas 2 +map mm-rage +wait 30 +quit *> .tmp\q3a_aas_sample_smoke_stdout.log; Remove-Item Env:WORR_LOG_LEVEL
```

Relevant log evidence from `.install\basew\logs\q3a_aas_sample_smoke.log`:

- `Bot AAS: loaded maps/mm-rage.aas (areas=428, reachability=562, clusters=4)`.
- `q3a_aas=Q3A AAS file load passed`.
- `q3a_sample=Q3A AAS area sample passed: area=1 point_area=1 cluster=0 presence=6`.
- `ShutdownGame`.

## Outstanding Work

- Continue replacing the remaining temporary entity, trace, movement, debug, and
  timing stubs recorded in
  `docs-dev/q3a-botlib-aas-reach-query-2026-06-17.md`.
- Replace the no-hit `AAS_EntityCollision` shim with a real WORR/Q2 BSP and
  entity trace bridge before using imported trace or movement prediction paths.
- Add a WORR-facing area query API so navigation code can request area data
  without touching Q3A globals directly.
- Add spawn-origin area lookup once spawn entities are fed through the adapter.
