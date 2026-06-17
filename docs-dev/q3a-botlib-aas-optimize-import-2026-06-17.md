# Q3A BotLib AAS Optimization Import

Date: 2026-06-17

Tasks: `FR-04-T12`, `FR-04-T14`, `DV-07-T06`

## Summary

This round imports the pinned Quake III Arena `be_aas_optimize.c` source into
the WORR Q3A BotLib boundary and removes the temporary local `AAS_Optimize`
no-op from `q3a_botlib_import.c`.

The imported source is compiled into the internal `q3a_botlib_utility` object
group without local edits. Q3A only calls this path from the save/forcewrite
AAS continuation flow when the `aasoptimize` LibVar is enabled. WORR's current
loaded-AAS smoke path keeps the Q3A default `aasoptimize=0`, so this slice
proves link coverage and removes the stub without changing normal staged-map
runtime behavior.

## Imported Source

- Local path: `src/game/sgame/bots/q3a/botlib/be_aas_optimize.c`
- Upstream source: `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/be_aas_optimize.c`
- Upstream commit: `dbe4ddb10315479fc00086f08e25d968b4b43c49`
- SHA-256: `69098a4cecf6137704304237b5b300e97801d1531bdb7ed35776af9f4a5d44b1`
- License/header: original id Software GPL header retained.

## Implementation Notes

- Added `be_aas_optimize.c` to the Meson Q3A BotLib source group beside
  `be_aas_optimize.h`.
- Marked `be_aas_optimize.c` present in the Q3A boundary inventory.
- Removed the temporary WORR-owned `AAS_Optimize` empty definition. The symbol
  now resolves to the imported Q3A implementation.
- Left `aasoptimize` disabled for the staged loaded-AAS validation path because
  the imported optimization function rewrites AAS geometry/index arrays and is
  intended for Q3A's opt-in save/forcewrite workflow.

## Validation

- `meson compile -C builddir-win sgame_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
- Dedicated smoke with `sg_bot_debug_aas 2` keeps the default loaded-AAS path
  on `aasoptimize=0` and verifies the existing imported AAS queries still pass:
  - `q3a_aas=Q3A AAS file load passed`
  - `q3a_route=Q3A AAS route query passed: start=3 goal=6 travel_time=113 reachability=1 route_end=6 stop=0`
  - `q3a_alt_route=Q3A AAS alternative route query passed: start=3 goal=6 goals=2 first_area=10 start_time=72 goal_time=39 extra_time=65534 failures=0`
  - `q3a_movement=Q3A AAS movement prediction passed: start=3 end=3 stop=0 frames=8 drop=yes jump=yes`
