# Q3A BotLib Route Target Stabilization

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T05`, `FR-04-T14`, `DV-07-T06`

## Summary

This slice adds a small movement-smoothing guard inside the WORR-native route
cache. When a newly refreshed AAS route returns a move target that is already
very close to the bot, the cache can promote a later sampled route point to the
current move target. That keeps command steering from fixating on tiny
near-origin route segments while preserving the same BotLib route query and
goal ownership.

## Implementation

- Added `BotNavStabilizeRouteTarget()` in `bot_nav.cpp`.
- The helper runs only after a successful route refresh and before the route is
  stored in the per-client cache.
- If the initial `moveTarget` is within a small horizontal threshold and a later
  sampled route point is far enough to be stable, the helper replaces
  `moveTarget` with that sampled point.
- `BotNavRouteStatus` now records stabilization checks, applications, skips,
  original/stable target distances, and the sampled route-point index used.

This does not change BotLib route building, persistent goal selection, item
reservation, or brain command ownership.

## Validation

Command:

```powershell
ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_nav.cpp.obj
```

Result: passed. Ninja emitted the existing shared-build-dir warning
`premature end of file; recovering`.

## Remaining Work

The new stabilization counters should be surfaced in scenario/frame status once
the parallel brain-status lanes settle. A later map-backed movement pass can add
trace-checked corner cutting and jitter gates for adjacent-area transitions.
