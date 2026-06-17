# Q3A BotLib Bridge Time and Vector Helpers

Date: 2026-06-17

Tasks: `FR-04-T12`, `DV-07-T06`

## Summary

This slice replaces two early placeholder behaviors in the WORR-owned Q3A
BotLib bridge:

- `Sys_MilliSeconds()` now returns a bridge-fed runtime clock instead of `0`.
- `AngleVectors()` now computes real forward/right/up vectors from pitch, yaw,
  and roll instead of always returning fixed axes.

No imported Q3A source was changed. The work stays inside the WORR bridge and
adapter boundary so later imported AAS movement, reachability, and route code can
use real time and orientation math without reaching into unrelated `sgame`
systems.

## Implementation

`q3a_botlib_import.*` now owns:

- `Q3A_BotLibImport_SetMilliseconds(int milliseconds)`, clamped to non-negative
  time and returned by `Sys_MilliSeconds()`.
- A trigonometric `AngleVectors()` implementation using Q3A's `PITCH`, `YAW`,
  `ROLL`, and `DEG2RAD` conventions.
- A narrow startup smoke that checks yaw `90` produces the expected basis
  vectors and reports `Q3A AngleVectors smoke passed`.
- Runtime status fields for `runtimeMilliseconds`, `angleVectorsSmokePassed`,
  and `angleVectorsMessage`.

`botlib_adapter.*` now copies those status fields and accepts a per-frame runtime
millisecond value. `bot_runtime.cpp` feeds `level.time.milliseconds()` into the
adapter each frame and prints the bridge status through `sg_bot_debug_aas 2`:

- `q3a_angle_vectors=Q3A AngleVectors smoke passed`
- `q3a_time_ms=<level time in milliseconds>`

## Validation

Build validation:

```powershell
meson compile -C builddir-win sgame_x86_64
```

Result:

- `q3a_botlib_import.c` compiled with the new math/time bridge.
- `sgame_x86_64.dll` linked successfully.

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
$env:WORR_LOG_LEVEL = 'debug'; .\.install\worr_ded_x86_64.exe +set logfile 1 +set logfile_name q3a_bridge_time_vector_smoke +set logfile_flush 1 +set dedicated 1 +set game basew +set sg_bot_enable 1 +set sg_bot_debug_aas 2 +map mm-rage +wait 60 +quit *> .tmp\q3a_bridge_time_vector_smoke_stdout.log; Remove-Item Env:WORR_LOG_LEVEL
```

Relevant log evidence from
`.install\basew\logs\q3a_bridge_time_vector_smoke.log`:

- `Bot AAS: loaded maps/mm-rage.aas (areas=428, reachability=562, clusters=4)`.
- `q3a_sample=Q3A AAS area sample passed: area=3 point_area=3 cluster=1 presence=6 reachability=1`.
- `q3a_angle_vectors=Q3A AngleVectors smoke passed`.
- `q3a_time_ms=25`.
- `ShutdownGame`.

## Outstanding Work

- The BSP entity epair callbacks are now backed by
  `docs-dev/q3a-botlib-bsp-entity-bridge-2026-06-17.md`; the BSP inline model
  callback is now backed by
  `docs-dev/q3a-botlib-bsp-model-bridge-2026-06-17.md`.
- Static `AAS_Trace` and `AAS_PointContents` are now backed by
  `docs-dev/q3a-botlib-bsp-collision-bridge-2026-06-17.md`.
- Continue dynamic entity collision and final WORR collision ownership work.
- Replace movement-prediction stubs with Q2 movement-aware BotLib bridges.
- Q3A debug line/cross/arrow bridging is covered by `docs-dev/q3a-botlib-aas-debug-draw-bridge-2026-06-17.md`.
- Feed the imported Q3A AAS main/start-frame path once that source subset lands.
