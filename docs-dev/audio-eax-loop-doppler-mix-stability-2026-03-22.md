# OpenAL EAX Loop Doppler Mix Stability Fix (2026-03-22)

Task reference: `FR-06-T01`

## Summary
Issue #761 reported substantial crackle/noise when many spatialized loop emitters were active at once, especially with hyperblaster projectiles.

## Root Cause
OpenAL keeps Doppler-capable projectile loops unmerged so each projectile can retain its own world-space velocity and pitch shift.

That preservation had two side effects when many identical projectile loops were active together:
- every per-projectile loop kept its full gain, so dense groups could overdrive the OpenAL mix more easily than merged loop paths;
- newly started unmerged loops used the same time-based phase offset, so identical loop samples tended to start in lockstep and stack constructively.

The combination was especially audible on rapid-fire projectile loops such as the hyperblaster.

## Implementation
### File: `src/client/sound/al.cpp`
- Added `AL_GetLoopGroupGainScale(int count)`:
  - applies a `1 / sqrt(count)` gain scale to dense Doppler-preserved loop groups;
  - keeps additional emitters audible without letting same-sample groups scale linearly into clipping.
- Added `AL_GetLoopSoundPhaseOffsetSeconds(...)`:
  - derives a stable per-entity playback phase from entity number and buffer id;
  - prevents identical unmerged autosounds from all starting on the same waveform phase.
- Updated `AL_MergeLoopSounds()`:
  - counts Doppler-tagged emitters for each same-sample loop group;
  - passes the shared normalization factor into unmerged projectile loop channel setup.
- Updated `AL_AddLoopSoundEntity(...)`:
  - applies the group gain scale both when refreshing an existing entity channel and when allocating a new one.
- Updated `AL_PlayChannel()`:
  - unmerged autosounds now use the stable per-entity phase offset instead of the shared realtime-derived offset.

## Behavior Impact
- Dense projectile loop groups remain spatialized and Doppler-aware.
- Hyperblaster/rocket-style loop emitters no longer pile up as aggressively in identical phase.
- Loudness growth for many identical unmerged loops is smoother and less likely to produce crackle.

## Validation
- Static repo check validated with:
  - `git diff --check`
- Build command identified for follow-up in a provisioned environment:
  - `meson compile -C builddir-client-cpp20`

## Files Changed
- `src/client/sound/al.cpp`
- `docs-dev/audio-eax-loop-doppler-mix-stability-2026-03-22.md`
- `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`
