# Q3A BotLib Utility Import

Date: 2026-06-17

Tasks: `FR-04-T10`, `FR-04-T12`, `DV-07-T06`

## Summary

This slice imports the first actual Quake III Arena BotLib source subset into
the quarantined WORR runtime boundary. The imported files are utility-level Q3A
headers and the `l_memory` / `l_libvar` C pair, enough to prove that commit
pinned Q3A C code can compile, link, initialize through a WORR bridge, and run
a small libvar smoke test inside `sgame_x86_64.dll`.

The full Q3A BotLib AAS runtime is still pending. This slice intentionally does
not load AAS through BotLib, answer area/reachability queries, sync entities, or
drive bot movement.

## Imported Files

All direct imports come from `id-Software/Quake-III-Arena` commit
`dbe4ddb10315479fc00086f08e25d968b4b43c49`; original id Software GPL headers
are retained.

| WORR path | Upstream path | SHA-256 |
|---|---|---|
| `src/game/sgame/bots/q3a/game/q_shared.h` | `code/game/q_shared.h` | `9083a35790991b674bc58c3800b068a9a978898508c5fb08123ea52e1dc8597a` |
| `src/game/sgame/bots/q3a/game/surfaceflags.h` | `code/game/surfaceflags.h` | `f05993c571858f3bb86cdcb9121d1748351377746916b5db0e28312bdb3b6722` |
| `src/game/sgame/bots/q3a/game/botlib.h` | `code/game/botlib.h` | `5eb2397db26ea5463aa1153431b8f39ac8ad92fcb2d4e3108e6b33fc894515af` |
| `src/game/sgame/bots/q3a/botlib/be_interface.h` | `code/botlib/be_interface.h` | `f715954bdbeeef62fa8946f35894da1bab7bdadca1dae2abece4f4c2e80e6358` |
| `src/game/sgame/bots/q3a/botlib/l_log.h` | `code/botlib/l_log.h` | `c48bdc73031a087055afb363123c72bdf5d349b76b8c4c02ff0ee8affb214a1e` |
| `src/game/sgame/bots/q3a/botlib/l_memory.c` | `code/botlib/l_memory.c` | `45c52cbde96d3675ac2e48adaf7bcd21c4a2c744f81e5be13880fa76c7e29237` |
| `src/game/sgame/bots/q3a/botlib/l_memory.h` | `code/botlib/l_memory.h` | `fcee4453a6a9b76347ae8aa068e711afa55b35a017cdb1f2bd625123decc81fa` |
| `src/game/sgame/bots/q3a/botlib/l_libvar.c` | `code/botlib/l_libvar.c` | `96966243e0c590649a0287f5c1b01970425cc65e364c58aee43fedb7165bab40` |
| `src/game/sgame/bots/q3a/botlib/l_libvar.h` | `code/botlib/l_libvar.h` | `60a403b2bba9cb0f813253bbd1121f606f83bdb21bffaeb8e7633a6ba6c34f41` |

## Build Boundary

Meson now collects the imported subset plus the WORR bridge into the
`q3a_botlib_utility` static library and links that library whole into
`sgame_x86_64`.

The build wrapper sets `Q3A_BOTLIB_WORR_BOUNDARY=1` and defines
`ID_INLINE=inline` at the compiler boundary. This keeps imported Q3A source
text untouched while satisfying Q3A's legacy platform-header assumptions under
WORR's current Windows/Clang Meson build.

The imported C files receive only local warning suppressions for known legacy C
patterns in this object group:

- `-Wno-missing-prototypes`
- `-Wno-sign-compare`
- `-Wno-strict-prototypes`
- `-Wno-unused-parameter`

MSVC-style argument syntax receives the equivalent local `ID_INLINE` define and
keeps the existing MSVC warning disables.

## WORR Bridge

`src/game/sgame/bots/q3a/q3a_botlib_import.*` is native WORR glue for this
utility subset. It provides:

- The Q3A global `botimport` table required by `l_memory.c`.
- Temporary memory callbacks backed by `malloc` / `free`.
- A quiet Q3A print callback for this smoke-only stage.
- `Q_stricmp`, `Com_Memset`, and `Com_Memcpy` compatibility functions required
  by the imported utility files.
- `Q3A_BotLibImport_RunLibVarSmoke`, which allocates a libvar, updates it,
  checks the changed flag, clears the changed flag, and frees all libvars.

`BotLibAdapter_Init` runs that smoke test and records the result in
`BotLibAdapterStatus`. Verbose `sg_bot_debug_aas` output now prints the adapter
status beside the AAS runtime status.

## Validation

Build validation:

```powershell
meson compile -C builddir-win sgame_x86_64
```

Result:

- `libq3a_botlib_utility.a` compiled from the imported Q3A utility subset and
  WORR bridge.
- `sgame_x86_64.dll` linked successfully with that static library.

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
$env:WORR_LOG_LEVEL='debug'; .\.install\worr_ded_x86_64.exe +set logfile 1 +set logfile_name q3a_utility_import_smoke +set logfile_flush 1 +set dedicated 1 +set game basew +set sg_bot_enable 1 +set sg_bot_debug_aas 2 +map mm-rage +wait 30 +quit *> .tmp\q3a_utility_import_smoke_stdout.log; Remove-Item Env:WORR_LOG_LEVEL
```

Relevant log evidence from
`.install\basew\logs\q3a_utility_import_smoke.log`:

- `Bot AAS: loaded maps/mm-rage.aas (areas=428, reachability=562, clusters=4)`.
- `Bot AAS: maps/mm-rage.aas loaded (areas=428, reachability=562, clusters=4, bytes=277484)`.
- `BotLib adapter: Q3A BotLib runtime import pending; boundary pinned to dbe4ddb10315479fc00086f08e25d968b4b43c49 (utility=Q3A LibVar smoke passed, imported=no, planned_files=47, commit=dbe4ddb10315479fc00086f08e25d968b4b43c49)`.

## Outstanding Work

- Replace the temporary `malloc` / `free` memory bridge with the agreed WORR
  allocator strategy or a documented bot-owned allocator.
- Import the remaining Q3A BotLib runtime/AAS files with per-file ledger rows
  before each copied source/header lands.
- Implement real `botlib_import_t` callbacks for tracing, point contents, PVS,
  filesystem, entity data, inline model bounds, command dispatch, and debug
  drawing.
- Load active-map AAS through BotLib and add simple area/reachability query
  validation before movement work starts.
