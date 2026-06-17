# Q3A BotLib Boundary

This directory is reserved for the Quake III Arena BotLib runtime rehost.

Current state:

- `q3a_botlib_boundary.*` is WORR-native boundary scaffolding.
- `game/q_shared.h`, `game/surfaceflags.h`, `game/botlib.h`,
  `game/be_aas.h`, `botlib/aasfile.h`, the `botlib/be_aas_*.h` declarations,
  `botlib/be_aas_file.c`, `botlib/be_aas_sample.c`, `botlib/be_aas_reach.c`,
  `botlib/be_interface.h`, `botlib/l_log.h`,
  `botlib/l_memory.*`, `botlib/l_libvar.*`, and the parser utility headers are
  direct imports from the pinned Q3A commit used to prove the import/build and
  AAS file loading, loaded-area sampling, and area-reachability query paths.
- `q3a_botlib_import.*` is a WORR-native smoke bridge that provides temporary
  memory, in-memory filesystem, logging, no-hit entity-collision,
  movement-prediction, debug-line, and shared-utility callbacks for the
  imported utility/AAS loader, sampling, and reachability subsets. It now also
  provides bridge-backed Q3A runtime milliseconds, real `AngleVectors` support,
  active-map Q2 BSP entity-lump parsing for Q3A BSP entity/epair helpers, and
  active-map Q2 BSP model-lump parsing for inline model bounds. Static-world
  `AAS_PointContents` and `AAS_Trace` now use active-map Q2 BSP collision lumps
  and `AAS_inPVS` / `AAS_inPHS` now use active-map Q2 BSP visibility clusters
  while dynamic entity collision and movement/debug hooks remain pending.
- The full Quake III Arena BotLib runtime has not been copied into this
  directory yet.
- The planned upstream baseline is `id-Software/Quake-III-Arena` commit
  `dbe4ddb10315479fc00086f08e25d968b4b43c49`.

Import rules:

- Add or update `docs-dev/q3a-botlib-aas-credits.md` before copying any Q3A file.
- Retain original id Software GPL headers on direct imports.
- Add a clear `Modified for WORR` note to imported files that are edited locally.
- Keep imported C code behind `botlib_adapter.*`; do not call Q3A globals from
  unrelated `sgame` systems.
- Build imported C files as an internal server-game object group unless a later
  implementation note records why a static library is safer.
