# Q3A BotLib AAS File Loader Import

Date: 2026-06-17

Tasks: `FR-04-T10`, `FR-04-T12`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice imports the first real Quake III Arena AAS runtime code:
`be_aas_file.c` plus the headers it needs to parse and own a loaded AAS world.
WORR still loads `maps/<map>.aas` through its own filesystem first, validates the
Q3A/BSPC header as before, then hands the already-loaded bytes to the imported
Q3A loader through a temporary in-memory `botlib_import_t` filesystem bridge.

This is not full BotLib routing yet. It proves that Q3A's own AAS lump loader can
consume the generated Q2 AAS payload, allocate/free its loaded world data, and
report matching structural counts behind the WORR adapter.

## Imported Files

All direct imports come from `id-Software/Quake-III-Arena` commit
`dbe4ddb10315479fc00086f08e25d968b4b43c49`; original id Software GPL headers
are retained.

| WORR path | Upstream path | SHA-256 |
|---|---|---|
| `src/game/sgame/bots/q3a/botlib/aasfile.h` | `code/botlib/aasfile.h` | `937ea3f9143c44e8d61002a369e92fc44f5c416b7f3c92d088c4914fe28ebdcb` |
| `src/game/sgame/bots/q3a/game/be_aas.h` | `code/game/be_aas.h` | `86ac70f29f7c387b255b0c8b6da56841cc773f26ac7551923e4b8429652dad70` |
| `src/game/sgame/bots/q3a/botlib/be_aas_file.c` | `code/botlib/be_aas_file.c` | `7306cf38153ede8c7608b1d07fd174901a69639c5ff1e47cc11c2e616b08a9a5` |
| `src/game/sgame/bots/q3a/botlib/be_aas_file.h` | `code/botlib/be_aas_file.h` | `d92e01098c310137d3e003fc394627aca726868a92b61c287eacd3c139c51982` |
| `src/game/sgame/bots/q3a/botlib/be_aas_def.h` | `code/botlib/be_aas_def.h` | `a248bab0f92165bbf0382997d401409394be0f053382b8367e9bd138e148b054` |
| `src/game/sgame/bots/q3a/botlib/be_aas_funcs.h` | `code/botlib/be_aas_funcs.h` | `c018b4a9a7c076924b406f464318d32c178ff051bfeefb93ffef74eedf39bedb` |
| `src/game/sgame/bots/q3a/botlib/be_aas_main.h` | `code/botlib/be_aas_main.h` | `d1abfb9d2da6af9cfc89ba4d0ee1dc1bde7f82d086a727edae5038705ac06d8a` |
| `src/game/sgame/bots/q3a/botlib/be_aas_entity.h` | `code/botlib/be_aas_entity.h` | `c3d0ced64df7b73f6b1ef9d6d63a359024f1d2f55977a96aed89ea13e130c22b` |
| `src/game/sgame/bots/q3a/botlib/be_aas_sample.h` | `code/botlib/be_aas_sample.h` | `5fc4b89d263f12c7b79843f7d589b231f8ff96c8af09154f79f9d6478a6df993` |
| `src/game/sgame/bots/q3a/botlib/be_aas_cluster.h` | `code/botlib/be_aas_cluster.h` | `fc6274401718ea0a95c4912e530ec13544bd4f7d71bb040f662a6189be469899` |
| `src/game/sgame/bots/q3a/botlib/be_aas_reach.h` | `code/botlib/be_aas_reach.h` | `7e16dbfa25178cd11edcdd480155e49c3dbe7aed809f4fe0adafca3c836e7227` |
| `src/game/sgame/bots/q3a/botlib/be_aas_route.h` | `code/botlib/be_aas_route.h` | `7de8aff915f394452c405e2c0c5d2f617158c8a0cee5ebb79eb0daeba2d7f96c` |
| `src/game/sgame/bots/q3a/botlib/be_aas_routealt.h` | `code/botlib/be_aas_routealt.h` | `204b16897a6a95b7cf7d392e5b1637f1b89823695d3a5c47a3e327a3ed8efae9` |
| `src/game/sgame/bots/q3a/botlib/be_aas_debug.h` | `code/botlib/be_aas_debug.h` | `acaab8949a831f6e2ee0418e80edad4215b03406555aecd86f9970ed6b719ff6` |
| `src/game/sgame/bots/q3a/botlib/be_aas_optimize.h` | `code/botlib/be_aas_optimize.h` | `92be274f1e600df658d588d924e7b030806b455ff7b706afb28c489bd3ceb92c` |
| `src/game/sgame/bots/q3a/botlib/be_aas_bsp.h` | `code/botlib/be_aas_bsp.h` | `b7c48e3fe8c353445ede0b35ebaa6ff1bdfe8cab9bc48b8b8816945d382b5db0` |
| `src/game/sgame/bots/q3a/botlib/be_aas_move.h` | `code/botlib/be_aas_move.h` | `22cc80f7362514aeba0d2cb9b1550cfb0adee96912ff94269af7bf5bad1ba719` |
| `src/game/sgame/bots/q3a/botlib/l_script.h` | `code/botlib/l_script.h` | `5415db33c82197b7375942762df34ef535b23c464a672aba5f11944a7af444d9` |
| `src/game/sgame/bots/q3a/botlib/l_precomp.h` | `code/botlib/l_precomp.h` | `dd5692a0991b9428a8af203f3586525a3d569323695f77f4839d5fe7deb2b419` |
| `src/game/sgame/bots/q3a/botlib/l_struct.h` | `code/botlib/l_struct.h` | `b18d46536025610ca9782c66ff3744640d05cc20f6e988dc45e7ab6633613a40` |
| `src/game/sgame/bots/q3a/botlib/l_utils.h` | `code/botlib/l_utils.h` | `5512c0f7554791aa95b02a5960eec635eb6a391894034611eab82c041cdfdaeb` |

## Build Boundary

`q3a_botlib_utility` now compiles `be_aas_file.c` with the earlier utility
subset. The build wrapper also:

- Defines `WIN32` for Windows Q3A C builds so Q3A's little-endian helpers match
  the platform branch expected by the imported AAS loader.
- Defines Q3A's existing `MEMORYMANEGER` path so hunk allocations made by the
  loader can be freed by `AAS_DumpAASData` in this temporary bridge.
- Adds local warning exceptions for legacy Q3A C patterns:
  `-Wno-return-type` and `-Wno-unused-but-set-variable`, in addition to the
  previous Q3A-object-group exceptions.

No imported Q3A source text was edited.

## WORR Bridge

`q3a_botlib_import.*` now provides:

- The Q3A global `aasworld` expected by `be_aas_file.c`.
- `AAS_Error` and quiet `Log_Write` / `Log_WriteTimeStamped` bridge functions.
- Read-only in-memory `FS_FOpenFile`, `FS_Read`, `FS_FCloseFile`, and `FS_Seek`
  callbacks so Q3A can load bytes supplied by WORR's filesystem.
- `Q3A_BotLibImport_LoadAASBuffer`, which sets the `sv_mapChecksum` libvar,
  calls the imported `AAS_LoadAASFile`, and records the resulting AAS counts.
- `Q3A_BotLibImport_UnloadAAS`, which calls the imported `AAS_DumpAASData`.

`Bot_RuntimeBeginLevel` now treats the Q3A AAS load result as part of the bot AAS
gate. If the imported loader rejects the data, the runtime status becomes failed
with the Q3A `BLERR` code and message.

## Validation

Build validation:

```powershell
meson compile -C builddir-win sgame_x86_64
```

Result:

- `be_aas_file.c` compiled into `libq3a_botlib_utility.a`.
- `sgame_x86_64.dll` linked successfully with the AAS loader bridge.

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
$env:WORR_LOG_LEVEL='debug'; .\.install\worr_ded_x86_64.exe +set logfile 1 +set logfile_name q3a_aas_file_loader_smoke +set logfile_flush 1 +set dedicated 1 +set game basew +set sg_bot_enable 1 +set sg_bot_debug_aas 2 +map mm-rage +wait 30 +quit *> .tmp\q3a_aas_file_loader_smoke_stdout.log; Remove-Item Env:WORR_LOG_LEVEL
```

Relevant log evidence from
`.install\basew\logs\q3a_aas_file_loader_smoke.log`:

- `Bot AAS: loaded maps/mm-rage.aas (areas=428, reachability=562, clusters=4)`.
- `Bot AAS: maps/mm-rage.aas loaded (areas=428, reachability=562, clusters=4, bytes=277484)`.
- `BotLib adapter: ... (utility=Q3A LibVar smoke passed, q3a_aas=Q3A AAS file load passed, q3a_areas=428, q3a_reachability=562, q3a_clusters=4, imported=no, planned_files=48, commit=dbe4ddb10315479fc00086f08e25d968b4b43c49)`.

## Outstanding Work

- Import `be_aas_main.c` and the initialization/frame path, or write a narrow
  WORR-native equivalent that calls deeper BotLib APIs safely.
- Replace temporary in-memory FS callbacks with the final WORR FS callback layer
  once BotLib opens more than the already-loaded active AAS file.
- Implement trace, point contents, PVS, entity-data, inline-model, debug draw,
  and command callbacks.
- First simple area query smoke is tracked in
  `docs-dev/q3a-botlib-aas-sample-query-2026-06-17.md`; broader route and
  movement query work remains pending.
