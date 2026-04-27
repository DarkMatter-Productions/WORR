# WORR Spatial Audio Capabilities

> **Immersive 3D Audio System with Real-Time Occlusion, Environmental Reverb, and HRTF Support**

---

## Overview

WORR features a comprehensive spatial audio system built on OpenAL with EFX (Effects Extension) support. The system delivers immersive, physically-modeled audio through advanced occlusion, environmental reverb, Doppler effects, and Head-Related Transfer Function (HRTF) processing for headphone users.

---

## Key Features at a Glance

| Feature | Description |
|---------|-------------|
| **Multi-Ray Occlusion** | 5-ray source fan with material-aware filtering |
| **EFX Reverb** | 26 environment presets with smooth transitions |
| **Doppler Effect** | Per-projectile tracking with velocity smoothing |
| **HRTF (Binaural)** | Accurate 3D positioning for headphones |
| **Air Absorption** | Distance-based high-frequency rolloff |
| **Per-Source Reverb Sends** | Individual reverb routing per sound |
| **Material Transmission** | Frequency-dependent filtering through surfaces |
| **Portal Propagation** | One- and two-hop areaportal fallback for sounds around openings |
| **Two-Identity Source Paths** | Listener-room EFX plus per-source room/path send filtering |

---

## Occlusion System

The occlusion system simulates how sounds are muffled and attenuated when obstructed by world geometry. Unlike simple binary on/off systems, WORR uses a multi-ray probability model with material-aware weighting.

### Multi-Ray Sampling

The system traces **5 rays** per sound source to determine occlusion:

- **1 direct ray**: Listener → source center
- **4 peripheral rays around the source path**: Cardinal offsets that catch partial obstruction and corner leakage

This multi-sample approach produces smooth, realistic transitions as objects partially obstruct sounds.

### Diffraction Approximation

Peripheral rays are blended with the direct trace using a configurable weight (default 40%), approximating how sound "bends" around corners through diffraction. This prevents the jarring cutoffs typical of single-ray occlusion.

### Material-Based Transmission

Occlusion traces resolve the BSP surface's rerelease `.mat` ID through an acoustic material table before producing attenuation and filtering. Exact `.mat` IDs are preferred; substring hints remain only as a fallback for custom or path-like material names.

Each acoustic material stores low/mid/high transmission, low/mid/high absorption, scattering, and semantic flags such as water, foliage, grate, sky, and outdoor hint. The initial built-in table preserves the old family behavior while making the model extensible:

| Material Family | Direct Gain Weight | Direct HF Cutoff | Reverb Send Colour |
|-----------------|--------------------|------------------|--------------------|
| **Sky** | Clear | Clear | Clear exterior hint |
| **Glass / Window / Ice** | 0.25 | 4000 Hz | Brightened but still filtered |
| **Grate / Mesh / Vent / Fence / Ladder** | 0.30 | Clear | Mostly bright/scattered |
| **Soft (Cloth / Carpet / Dirt / Sand / Flesh)** | 0.60 | 2000 Hz | Damped |
| **Wood / Plywood** | 0.75 | 2000 Hz | Damped with moderate scatter |
| **Metal / Steel / Iron** | 0.85 | 1500 Hz | Darker reflections |
| **Stone / Concrete / Brick / Tile / Marble** | 0.15 | 800 Hz | Deep, low-passed reflections |
| **Water / Slime / Lava** | 0.40 | 700 Hz | Wet, heavily low-passed reflections |

### Temporal Smoothing

To prevent "flutter" from geometry edges or minimal obstructions:

- **Attack rate**: 25.0 (quick occlusion onset)
- **Release rate**: 8.0 (gradual return to clarity)
- **Dead-zone threshold**: 0.1 (ignores tiny occlusion fractions)
- **Update interval**: 50ms per channel with entity-based jitter

### OpenAL Implementation

- Per-source `AL_FILTER_LOWPASS` filters control overall direct gain and high-frequency rolloff
- `AL_DIRECT_FILTER` applies the acoustic direct path atomically where EFX filters are available
- `AL_AUXILIARY_SEND_FILTER` colours the reverb send separately so occluded metal, stone, glass, foliage, and liquid surfaces do not all feed the same bright room tail

### Portal-Aware Propagation

When direct multi-ray occlusion reports a blocked path, the OpenAL backend can try a lightweight BSP route through one or two neighbouring areaportals. The route is estimated from acoustic region bounds and areaportal connectivity, then scored by:

- **Route distance**: Longer indirect paths lose strength
- **Aperture openness**: Regions with more portal connectivity pass more energy
- **Bend angle**: Sharper turns lose more high-frequency energy
- **Material transmission**: Dominant region materials colour the indirect path

Valid portal routes can reduce excessive wall-style muffling and increase filtered reverb send for sounds heard through doorways or adjacent spaces. This is intentionally modest; it is not a full wave simulation or baked propagation sidecar.

### CVars

```
s_occlusion          1       Enable/disable occlusion system
s_occlusion_strength 1.0     Scale final occlusion intensity (0.0-2.0)
```

---

## Environmental Reverb

The reverb system uses OpenAL EFX to create convincing room acoustics with 26 environmental presets and smooth transitions between zones.

### Preset Library

| # | Preset | Use Case |
|---|--------|----------|
| 0 | Generic | Default fallback |
| 1 | Padded Cell | Anechoic/damped spaces |
| 2 | Room | Small indoor spaces |
| 3 | Bathroom | Tiled reflective spaces |
| 4 | Living Room | Furnished medium rooms |
| 5 | Stone Room | Hard-surfaced chambers |
| 6 | Auditorium | Large performance spaces |
| 7 | Concert Hall | Grand acoustic venues |
| 8 | Cave | Natural rock formations |
| 9 | Arena | Sports/combat arenas |
| 10 | Hangar | Massive industrial spaces |
| 11 | Carpeted Hallway | Damped corridors |
| 12 | Hallway | Open corridors |
| 13 | Stone Corridor | Hard-surfaced passages |
| 14 | Alley | Outdoor narrow spaces |
| 15 | Forest | Open natural spaces |
| 16 | City | Urban environments |
| 17 | Mountains | Wide open terrain |
| 18 | Quarry | Open industrial pits |
| 19 | Plain | Flat open terrain |
| 20 | Parking Lot | Outdoor concrete spaces |
| 21 | Sewer Pipe | Cylindrical tunnels |
| 22 | Underwater | Submerged acoustics |
| 23 | Drugged | Psychoacoustic effect |
| 24 | Dizzy | Disorienting effect |
| 25 | Psychotic | Extreme effect |

### Environment Selection

Reverb presets are selected based on:

1. **BSP acoustic regions**: Map-load area cache stores leaf bounds, dominant material groups, sky ratio, vertical openness, and areaportal neighbours
2. **Room dimension probing**: Live raycasts estimate local volume, vertical openness, and sky exposure around the listener
3. **Weighted material matching**: Region material composition and the listener floor material are scored against `sound/default.environments`
4. **Explicit outdoor classification**: Sky exposure, portal connectivity, vertical openness, and enclosure ratio decide whether the listener is interior, semi-open, or exterior
5. **Built-in fallback rules**: Small, medium, large, and exterior spaces remain covered when the external environment file is absent
6. **Authored EAX zones**: `client_env_sound` / `env_sound` entities can override the automatic resolver when `al_eax` is enabled; zones support numeric `reverb_effect_id` and named `reverb_effect`, `reverb`, or `eax_profile` keys
7. **Optional `.aud` sidecars**: Map-specific sidecars can refine existing BSP regions, portal/opening hints, and authored EAX zones without replacing the automatic resolver

### Optional `.aud` Sidecars

OpenAL looks for sidecars in this order:

1. `maps/<mapname>.aud`
2. `sound/acoustics/<mapname>.aud`

The file is optional JSON. Missing files are silent; invalid files warn and are ignored before their hints are applied. Supported top-level arrays:

- `regions`: target an existing BSP acoustic area by `area` / `area_id`, or by `origin`
- `portals` / `openings`: add or tune route edges between existing acoustic areas
- `eax_zones` / `env_sounds` / `environment_zones`: append authored EAX-style zone overrides

Example:

```json
{
  "regions": [
    {
      "area": 3,
      "dimension": 1024,
      "sky_ratio": 0.1,
      "dominant_material": "stone"
    }
  ],
  "portals": [
    {
      "from": 3,
      "to": 4,
      "openness": 0.85,
      "transmission": 0.95,
      "gain_hf": 0.9
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

Sidecars refine WORR's BSP-derived acoustic cache. They do not create regions for invalid areas, and the map still gets region classification, portal-aware propagation, and source-room routing without any authored sidecar.

### Smooth Transitions

When moving between reverb zones:

- **Lerp time**: 3 seconds default (`al_reverb_lerp_time`)
- All EFX parameters interpolate smoothly
- Prevents jarring reverb "pops" when crossing zone boundaries

### Dimension Estimation

The system probes room dimensions using a 14-direction raycast pattern:

```cpp
// Vertical ceiling/floor probes
// + upper-hemisphere and horizontal radial probes
```

Results inform the reverb's:
- Decay time (larger spaces = longer tails)
- Diffusion (complex spaces = more scatter)
- Early reflections (nearby surfaces = shorter delays)

### CVars

```
al_reverb            1       Enable EFX reverb system
al_reverb_lerp_time  3.0     Transition time between presets (seconds)
```

---

## Doppler Effect

The Doppler system applies realistic pitch shifting to moving sound sources, particularly effective for fast-moving projectiles.

### Implementation

- **Per-entity velocity tracking**: Smoothed, clamped velocities derived from interpolated positions
- **Speed of sound**: 13,500 Quake units/second (configurable)
- **Velocity smoothing**: Exponential smoothing (rate 12) prevents pitch spikes from network jitter

### Flagged Projectiles

The following projectile types receive Doppler processing:

| Category | Projectiles |
|----------|-------------|
| **Energy Weapons** | Blaster, Hyperblaster, Blue Blaster, Ion Ripper |
| **Rockets** | Rocket Launcher, Heat Seeker, Phalanx |
| **Plasma** | Plasma Gun bolts |
| **Advanced** | Disruptor/Tracker bolts, BFG ball, Disintegrator |
| **Monsters** | Vore homing pods |
| **Quake 1** | Plasmaball, Tesla bolt |

### Fallback Detection

When `RF_DOPPLER` is unavailable, Doppler enables for entities with these effects:

- `EF_ROCKET` / `EF_BLASTER` / `EF_HYPERBLASTER`
- `EF_BLUEHYPERBLASTER` / `EF_PLASMA`
- `EF_IONRIPPER` / `EF_BFG` / `EF_TRACKER`

### Loop Merge Bypass

Doppler sources bypass the loop-merging optimization to preserve individual entity velocities.

### CVars

```
al_doppler           1       Doppler factor (0 = disabled)
al_doppler_speed     13500   Speed of sound (units/sec)
al_doppler_min_speed 30      Minimum source speed for Doppler
al_doppler_max_speed 4000    Maximum source speed (clamped)
al_doppler_smooth    12      Velocity smoothing rate
```

---

## HRTF (Head-Related Transfer Function)

HRTF provides accurate 3D audio positioning for headphone users by simulating how sound reaches each ear differently based on direction.

### Features

- **Extension**: `ALC_SOFT_HRTF` (OpenAL Soft)
- **Per-source spatialization**: `AL_SOFT_source_spatialize` supported
- **Quality**: Uses OpenAL Soft's built-in HRTF database

### Benefits

- Accurate elevation perception (above/below)
- Front/back distinction
- Precise left/right positioning
- Natural "inside the head" localization for stereo headphones

### CVars

```
al_hrtf              1       HRTF mode: 0=off, 1=default/autodetect, >1=force enable
```

---

## Air Absorption

High frequencies naturally attenuate over distance in air. This effect adds realism to distant sounds without requiring occlusion.

### Implementation

Two approaches based on OpenAL capabilities:

**EFX path** (preferred):
- Uses `AL_AIR_ABSORPTION_FACTOR` per source
- Factor scales linearly with distance

**Fallback path**:
- Folds absorption into the direct low-pass filter
- No additional effect slot required

### Behavior

- Maximum absorption at configurable distance (default 2048 units)
- **Underwater exemption**: Automatically disabled when submerged (avoids double-filtering)

### CVars

```
al_air_absorption          1       Enable air absorption
al_air_absorption_distance 2048    Full absorption distance (units)
```

---

## Per-Source Reverb Sends

Unlike global-only reverb systems, WORR routes each sound source to the reverb effect individually, allowing:

- **Distance-based reverb**: Distant sounds have more reverb
- **Occlusion-boosted reverb**: Muffled sounds route more through room tail
- **Same-room sounds stay dry**: Local sources remain clear
- **Path-aware sends**: Adjacent rooms, portal routes, exterior-to-interior, and interior-to-exterior paths get distinct wetness and filtering

### Implementation

```cpp
// Reverb send scales with:
// - Distance from listener
// - Occlusion factor (occluded = more reverb)
// - Acoustic material HF colour for the blocked path
// - Source/listener region and portal-route modifiers
// - Optional .aud portal/opening hint modifiers
// - Minimum send level (default 0.2)
// - Occlusion boost multiplier (default 1.5)
```

Uses `AL_AUXILIARY_SEND_FILTER` for per-source effect routing.
`al_reverb` is the routing master switch; `al_eax` only controls authored zone overrides and is not required for per-source sends.

### Two-Identity Model

The global EFX slot is always the listener-room identity: it represents the acoustic space the ear is inside. Each audible OpenAL source separately resolves a source-room path state:

- **Same space**: Reduced send scale, clear send colour, no added direct-path damping
- **Adjacent or cross space**: Less direct energy when occlusion is enabled, more filtered send
- **Portal route**: One- or two-hop indirect path can recover audibility while keeping filtered send colour
- **Exterior → interior**: Stronger room send with darker filtering, as outside sound enters the listener room
- **Interior → exterior**: Lighter send boost with different HF colour, as room sound exits into open space
- **Unreachable**: Heavy direct damping when occlusion is enabled and reduced/darker send

Merged looping sounds aggregate source path states across their contributing entities before updating a shared OpenAL source.

### CVars

```
al_reverb_send                    1     Enable per-source reverb sends
al_reverb_send_distance        2048     Full reverb send distance
al_reverb_send_min             0.2      Minimum reverb send level
al_reverb_send_occlusion_boost 1.5      Occluded sound reverb multiplier
```

---

## Distance Attenuation

### Distance Models

WORR supports two OpenAL distance models:

| Model | Behavior | Best For |
|-------|----------|----------|
| **Linear** | Constant falloff rate | Consistent attenuation |
| **Inverse** | Gradual natural rolloff | Large open spaces |

The inverse model (`AL_INVERSE_DISTANCE_CLAMPED`) better matches real-world acoustics for outdoor and large interior spaces.

### CVars

```
al_distance_model    1       0=linear, 1=inverse
```

---

## Underwater Audio

When the player is submerged (`RDF_UNDERWATER`), the audio system applies:

- Global low-pass filtering (configurable HF gain)
- Modified reverb (switches to `UNDERWATER` preset if available)
- Disabled air absorption (water has different propagation)

### CVars

```
s_underwater         1       Enable underwater audio effects
s_underwater_gain_hf 0.25    High-frequency gain when submerged
```

---

## Technical Architecture

### OpenAL Extensions Used

| Extension | Purpose | Status |
|-----------|---------|--------|
| `ALC_EXT_EFX` | Effects and filters | ✅ Active |
| `ALC_SOFT_HRTF` | Binaural processing | ✅ Active |
| `AL_SOFT_source_spatialize` | Per-source HRTF | ✅ Active |
| `AL_SOFT_loop_points` | Seamless looping | ✅ Active |
| `AL_EXT_float32` | High-quality samples | ✅ Active |
| `AL_AIR_ABSORPTION_FACTOR` | Air absorption | ✅ Active |

### Per-Channel State

Each sound channel maintains:

```cpp
struct channel_t {
    float occlusion;           // Smoothed occlusion factor
    float occlusion_target;    // Raw traced occlusion
    float occlusion_cutoff;    // Current HF cutoff
    float occlusion_cutoff_target; // Target HF cutoff
    int   occlusion_time;      // Last update timestamp
    bool  no_merge;            // Bypass loop merge for Doppler
    // ... additional state
};
```

### Source Files

| File | Purpose |
|------|---------|
| `src/client/sound/sound.h` | Constants and interface definitions |
| `src/client/sound/main.cpp` | Core sound management and occlusion queries |
| `src/client/sound/al.cpp` | OpenAL backend with EFX implementation |
| `src/client/sound/dma.cpp` | Software mixer fallback with biquad filters |
| `src/client/sound/qal.cpp` | OpenAL function loading and HRTF setup |

---

## Configuration Summary

### Enable/Disable Features

```
s_occlusion          1       Occlusion system
al_reverb            1       EFX reverb
al_doppler           1       Doppler effect
al_hrtf              1       HRTF default/autodetect
al_air_absorption    1       Air absorption
al_reverb_send       1       Per-source reverb
s_underwater         1       Underwater effects
```

### Quality Tuning

```
s_occlusion_strength        1.0     Occlusion intensity
al_reverb_lerp_time         3.0     Reverb transition time
al_doppler_smooth          12       Doppler smoothing rate
al_distance_model           1       Distance attenuation model
al_air_absorption_distance  2048    Air absorption range
```

---

## Performance Considerations

### Optimization Features

- **Rate-limited occlusion**: 50ms update interval per channel
- **Entity-based jitter**: Prevents all sources updating simultaneously
- **Loop merging**: Combines identical looping sounds (except Doppler sources)
- **Cached reverb probes**: Room dimensions update only on movement

### Recommended Settings

| Hardware | Occlusion | Reverb | HRTF |
|----------|-----------|--------|------|
| Low-end | `s_occlusion 0` | `al_reverb 1` | `al_hrtf 0` |
| Mid-range | `s_occlusion 1` | `al_reverb 1` | `al_hrtf 1` |
| High-end | `s_occlusion 1` | `al_reverb 1` | `al_hrtf 1` |
| Headphones | `s_occlusion 1` | `al_reverb 1` | `al_hrtf 2` |

---

## Future Roadmap

The following enhancements are under consideration:

### Near-Term
- **Directional sources (sound cones)**: Speaker-facing sounds for alarms, NPCs
- **Portal-based propagation**: Sound paths through doorways/windows

### Long-Term
- **Early reflections**: Geometry-based first reflection points
- **Ambient sound zones**: Location-specific ambient soundscapes
- **Advanced diffraction**: True corner-bending via pathfinding

---

## Conclusion

WORR's spatial audio system combines proven techniques with modern OpenAL extensions to deliver immersive, believable sound. The multi-ray occlusion, smooth reverb transitions, and HRTF support create a soundscape that enhances gameplay awareness while maintaining atmosphere.

For optimal experience:
- **Speakers**: Use default settings with reverb enabled
- **Headphones**: Leave HRTF on default/autodetect (`al_hrtf 1`) or force it with `al_hrtf 2` for accurate 3D positioning
- **Competitive**: Consider `s_occlusion 1` for directional awareness
