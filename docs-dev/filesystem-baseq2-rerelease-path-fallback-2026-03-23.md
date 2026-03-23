# Filesystem base-path fallback to Quake II Rerelease `baseq2` (2026-03-23)

## Task
- **Title:** Fix `pics/colormap.pcx` resolution when launching under WORR `.install` with rerelease assets.
- **Context:** Rerelease installs typically place shared game assets under `baseq2`, while this build’s configured `BASEGAME` can still be `basew`.
- **Status:** Implemented.

## Root cause
- Base filesystem bootstrap in `setup_base_paths` only added `/<base>/BASEGAME` (e.g. `basew`), so rerelease installs that do not include that layout could miss core assets such as `pics/colormap.pcx`.
- This affected startup palette initialization in renderer bootstrap when base assets were only available under `baseq2`.

## Change
- Updated `src/common/files.c`:
- Added helper `add_game_dir_with_rerelease_base(...)`.
- In rerelease mode (`com_rerelease == RERELEASE_MODE_YES`), the helper:
  - Adds `baseq2` first as a compatibility fallback.
  - Adds the configured `BASEGAME` directory immediately after, so explicit base-game assets remain higher priority when both are present.
  - Rewired `setup_base_paths` to call the helper for home/detected/basedir base entries.
- This preserves `BASEGAME` precedence while ensuring rerelease asset layout is still discoverable.

## Follow-up
- If `.install` remains the primary runtime root, consider whether the default Meson `base-game` value should be revisited for non-rerelease workflows.
