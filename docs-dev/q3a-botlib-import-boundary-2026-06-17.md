# Q3A BotLib Import Boundary

Date: 2026-06-17

Tasks: `FR-04-T10`, `FR-04-T12`, `DV-07-T06`

## Summary

This slice creates the explicit WORR boundary for the future Quake III Arena
BotLib runtime import. It does not copy Q3A BotLib runtime source yet. Instead,
it adds a compiled adapter/status layer and reserves `src/game/sgame/bots/q3a/`
as the quarantined import root so the next slice can import files with source
provenance already defined.

The chosen build strategy is to compile imported Q3A BotLib C files as an
internal server-game object group behind `botlib_adapter.*`. That keeps the
runtime close enough to `sgame` for WORR services and memory ownership, while
still preventing Q3A globals and file-layout assumptions from spreading into
unrelated gameplay systems.

## Implementation

Added WORR-native files:

- `src/game/sgame/bots/botlib_adapter.hpp`
- `src/game/sgame/bots/botlib_adapter.cpp`
- `src/game/sgame/bots/q3a/q3a_botlib_boundary.hpp`
- `src/game/sgame/bots/q3a/q3a_botlib_boundary.cpp`
- `src/game/sgame/bots/q3a/README.WORR.md`

Boundary behavior:

- Records the pinned Q3A source baseline:
  `id-Software/Quake-III-Arena` commit
  `dbe4ddb10315479fc00086f08e25d968b4b43c49`.
- Exposes a small planned-file inventory for the AAS/runtime import path.
- Reports that the runtime import is still pending.
- Initializes from `Bot_RuntimeRegisterCvars`.
- Tracks active map/AAS path from the existing runtime shell when structural AAS
  validation succeeds.
- Ends adapter level state when the runtime level state is cleared.

Meson now builds the adapter shell and boundary inventory into `sgame_x86_64`.

## Import Rules

No Q3A source should be copied into `src/game/sgame/bots/q3a/` until:

- The exact upstream path and commit are recorded in
  `docs-dev/q3a-botlib-aas-credits.md`.
- The original id Software GPL header is retained on direct imports.
- Any locally edited imported file receives a clear `Modified for WORR` note.
- Warning policy and local compatibility shims are documented in the
  implementation log that imports the file.

## Validation

Build validation:

```powershell
meson compile -C builddir-win sgame_x86_64
```

Result:

- `sgame_x86_64.dll` compiled and linked successfully with the adapter shell
  and `q3a/` boundary inventory included.

Staging validation:

```powershell
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas --q2aas-stage-report .tmp\q2aas\stage-report.json --q2aas-package-report .tmp\q2aas\refresh-package-archive-report.json --q2aas-package-audit-report .tmp\q2aas\refresh-package-archive-audit-report.json
```

Result:

- `.install` refreshed with the rebuilt `sgame_x86_64.dll`.
- `maps/mm-rage.aas` was re-injected into `.install\basew\pak0.pkz`.
- The archive-required q2aas audit and staged Windows payload validation passed.

Runtime smoke:

```powershell
$env:WORR_LOG_LEVEL='debug'; .\.install\worr_ded_x86_64.exe +set logfile 1 +set logfile_name botlib_boundary_debug_smoke +set logfile_flush 1 +set dedicated 1 +set game basew +set sg_bot_enable 1 +set sg_bot_debug_aas 2 +map mm-rage +wait 30 +quit *> .tmp\botlib_boundary_debug_smoke_stdout.log; Remove-Item Env:WORR_LOG_LEVEL
```

Result:

- The staged dedicated server loaded `.install\basew\sgame_x86_64.dll`.
- The existing runtime shell still loaded packaged `maps/mm-rage.aas` after the
  adapter shell was linked.
- Reported structural counts remained `areas=428`, `reachability=562`,
  `clusters=4`, `bytes=277484`.

## Next Work

- Add ledger rows before copying the first Q3A BotLib source/header files.
- Import the minimal AAS runtime file set into `src/game/sgame/bots/q3a/`.
- Add Meson warning policy for imported C files.
- Replace the adapter's pending-runtime status with real BotLib setup/shutdown
  calls once the imported runtime compiles.
