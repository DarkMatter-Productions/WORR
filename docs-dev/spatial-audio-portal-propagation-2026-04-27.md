# Spatial Audio Portal Propagation

Date: 2026-04-27

Task IDs: `FR-06-T01`, `FR-06-T09`

## Summary

Implemented the fourth wave from `docs-dev/proposals/spatial-audio.md`: add a modest portal-aware propagation fallback after direct multi-ray occlusion. The OpenAL backend now uses the BSP acoustic region cache to search one- and two-hop areaportal neighbour routes, then applies route strength to direct filtering and reverb sends.

## Implementation

- Added a lightweight portal path resolver in `src/client/sound/al.cpp`.
  - Source and listener areas are classified through the third-wave acoustic region cache.
  - The resolver first keeps the direct multi-ray occlusion result from `S_ComputeOcclusion` / `S_GetOcclusion`.
  - If source and listener occupy different BSP areas, it searches one-hop and two-hop routes through cached areaportal neighbours.
  - Client frame area bits are respected where available so closed dynamic area connectivity does not create an audio route.
- Added indirect path scoring.
  - Route distance is compared against direct distance.
  - Portal aperture is approximated from neighbouring regions' portal openness.
  - Bend penalty is computed from waypoint direction changes.
  - Material transmission and high-frequency colour are derived from dominant region material groups.
  - Portal waypoints are approximated from adjacent area bounds because Quake II `mareaportal_t` exposes connectivity, not aperture polygons.
- Routed portal results into OpenAL spatial effects.
  - Valid portal routes can reduce excessive occlusion when the indirect route is less blocked than the direct wall trace.
  - Portal route HF colour can raise the direct low-pass cutoff relative to a monolithic wall hit.
  - Per-source and merged-loop reverb sends receive route gain and HF colour modifiers.

## Behavior Notes

This pass is intentionally conservative. It handles the common "sound just beyond a doorway" case without introducing an offline bake, a new map format, or full acoustic wave simulation. It also prepares the fifth-wave two-identity model by making source/listener path state more explicit.

The implementation does not yet trace against actual portal polygons because that geometry is not present in the loaded `mareaportal_t` data. The route uses adjacent acoustic region bounds as a practical approximation.

## Verification Notes

- `meson compile -C builddir-win-bootstrap-hosted worr_engine_x86_64`
  - Rebuilt the OpenAL backend changes successfully.
- `python tools\refresh_install.py --build-dir builddir-win-bootstrap-hosted --install-dir .install --base-game basew --platform-id windows-x86_64`
  - Refreshed and validated the local Windows `.install` staging root after the build.
