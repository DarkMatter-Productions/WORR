# Spatial Audio Authored Sidecar Overrides

Date: 2026-04-27

Task IDs: `FR-06-T01`, `FR-06-T11`

## Summary

Implemented the sixth wave from `docs-dev/proposals/spatial-audio.md`: keep the automatic BSP-native spatial audio path as the baseline, preserve authored `client_env_sound` / `env_sound` overrides and EAX JSON profiles, and add optional `.aud` sidecars for maps that want finer acoustic hints.

## Implementation

- Extended authored EAX zone parsing in `src/client/sound/al.cpp`.
  - Existing `client_env_sound` and `env_sound` entities still accept `origin`, `radius`, and `reverb_effect_id`.
  - Added named profile keys: `reverb_effect`, `reverb`, and `eax_profile`.
  - Names are matched case-insensitively and ignore spaces, underscores, and hyphens, so `stone_corridor`, `stonecorridor`, and `Stone Corridor` resolve to the same EAX profile.
- Added optional `.aud` sidecar loading during OpenAL end-registration.
  - First lookup: `maps/<mapname>.aud`.
  - Fallback lookup: `sound/acoustics/<mapname>.aud`.
  - Missing sidecars are silent. Present but invalid sidecars warn and are ignored before any hints are applied.
- Added sidecar region refinements.
  - Region hints target an existing BSP acoustic area by `area` / `area_id`, or by `origin` resolved through the BSP leaf.
  - Supported refinements: `dimension`, `horizontal_openness`, `vertical_openness`, `sky_ratio`, `enclosed_ratio`, `portal_openness`, `exterior_score`, `material`, `dominant_material`, and `step_id`.
  - Invalid or non-existent target areas are ignored. The loader never creates acoustic regions that the automatic BSP cache did not already build.
- Added sidecar portal/opening hints.
  - Portal hints target existing acoustic areas by `from` / `to` or `from_origin` / `to_origin`.
  - Supported refinements: `openness`, `transmission` or `gain`, `gain_hf` / `gainhf`, and `bidirectional`.
  - Valid portal hints add route edges to the existing one- and two-hop portal resolver, then tune aperture, transmission, and high-frequency colour for source paths that use those edges.
- Added sidecar-authored EAX zones.
  - `eax_zones`, `env_sounds`, or `environment_zones` arrays can append zone overrides equivalent to map-authored `client_env_sound` / `env_sound` entities.
  - Zones support `origin`, `radius`, and numeric or named reverb profile keys.

## `.aud` Schema

The sidecar is JSON. Every top-level section is optional:

```json
{
  "regions": [
    {
      "area": 3,
      "dimension": 1024,
      "sky_ratio": 0.1,
      "vertical_openness": 0.25,
      "dominant_material": "stone"
    }
  ],
  "portals": [
    {
      "from": 3,
      "to": 4,
      "openness": 0.85,
      "transmission": 0.95,
      "gain_hf": 0.9,
      "bidirectional": true
    }
  ],
  "eax_zones": [
    {
      "origin": [128, 256, 64],
      "radius": 512,
      "reverb": "cave"
    }
  ]
}
```

Origins may also be strings in the entity-origin form, such as `"128 256 64"`.

## Behavior Notes

The sidecar is deliberately a refinement layer. WORR still builds acoustic regions from BSP leaves, faces, material tags, sky exposure, and areaportal neighbours without any authored file. `.aud` files can correct known map-specific weak spots, add better doorway/opening links, or append showcase EAX zones, but they cannot make a non-spatial map sound spatial by themselves.

The runtime keeps the fifth-wave two-identity model intact. The listener room still owns the global EFX slot; per-source path states continue to own direct damping and reverb-send colour. `.aud` portal hints only influence those existing route calculations.

## Verification Notes

- `meson compile -C builddir-win-bootstrap-hosted worr_engine_x86_64`
  - Rebuilt the OpenAL sidecar and authored-zone changes successfully.
- `python tools\refresh_install.py --build-dir builddir-win-bootstrap-hosted --install-dir .install --base-game basew --platform-id windows-x86_64`
  - Refreshed and validated the local Windows `.install` staging root after the build.
