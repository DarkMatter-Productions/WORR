# Spatial Audio BSP Acoustic Regions

Date: 2026-04-27

Task IDs: `FR-06-T01`, `FR-06-T08`

## Summary

Implemented the third wave from `docs-dev/proposals/spatial-audio.md`: add BSP-native acoustic regions and move automatic reverb selection away from the old listener-floor-first model. The OpenAL backend now builds a map-load acoustic region cache keyed by BSP area and combines that static map knowledge with live listener probes.

## Implementation

- Added an OpenAL acoustic region cache in `src/client/sound/al.cpp`.
  - Regions are keyed by BSP `area`.
  - Each region accumulates non-solid leaf bounds, leaf-face material groups, dominant `.mat` step IDs, sky ratio, enclosure ratio, vertical/horizontal openness, and areaportal neighbours.
  - The region cache is rebuilt during sound end-registration after materials are available, and freed on shutdown or when no BSP is active.
- Reworked automatic reverb selection.
  - The listener is classified as an acoustic space from its BSP area plus the existing live 14-direction probe set.
  - Reverb environment bucket selection now uses the classified listener dimension rather than only the rolling probe average.
  - Preset selection now scores `sound/default.environments` entries against the listener region's material composition, with listener-floor material as an added signal instead of the sole decision point.
  - Outdoor classification now explicitly weighs sky exposure, vertical openness, portal openness, enclosure ratio, and outdoor material hints.
- Added source/listener region context to reverb sends.
  - Non-looping OpenAL sources classify their source BSP region and compare it with the cached listener region.
  - Merged looping sounds aggregate region send modifiers across their contributing entities.
  - Cross-area and interior/exterior sources can now bias reverb-send gain and high-frequency colour without waiting for the later portal-routing pass.

## Behavior Notes

The third wave remains zero-authoring. It uses shipped BSP areas, leaf faces, areaportals, sky flags, and existing rerelease `.mat` material IDs. Authored EAX zones still override the automatic resolver when active, but the automatic fallback is now region-aware instead of being driven primarily by the material under the player's feet.

This is not the fourth-wave portal propagation graph. The cache stores portal neighbours and uses portal openness for classification, but it does not yet search areaportal routes or bend sound paths around corners.

## Verification Notes

- `meson compile -C builddir-win-bootstrap-hosted worr_engine_x86_64`
  - Rebuilt the OpenAL backend changes successfully.
- `python tools\refresh_install.py --build-dir builddir-win-bootstrap-hosted --install-dir .install --base-game basew --platform-id windows-x86_64`
  - Refreshed and validated the local Windows `.install` staging root after the build.
