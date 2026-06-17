# Q3A BotLib AAS Alternative Route Import

Date: 2026-06-17

Tasks: `FR-04-T12`, `FR-04-T14`, `DV-07-T06`

Related plan: `docs-dev/plans/q3a-botlib-aas-port.md`

## Summary

This slice imports Quake III Arena's AAS alternative-route implementation into the WORR Q3A BotLib quarantine boundary and removes the temporary alternative-routing lifecycle stubs from the native bridge.

The new imported runtime source is:

- `src/game/sgame/bots/q3a/botlib/be_aas_routealt.c`

The source is an exact copy from id Software's Quake III Arena public mirror at commit `dbe4ddb10315479fc00086f08e25d968b4b43c49`:

- `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/be_aas_routealt.c`

Local SHA-256:

- `d74b894c09316ca8161875d8499a7fc0b61bd195f8fda83d9dd6adfc4dc78081`

The imported file keeps the original id Software GPL header and has no local source edits.

## Implementation Notes

`meson.build` now compiles `be_aas_routealt.c` in the internal `q3a_botlib_utility` object group with the rest of the Q3A AAS runtime subset.

`q3a_botlib_import.c` now includes `be_aas_routealt.h` and lets imported Q3A code own:

- `AAS_InitAlternativeRouting`
- `AAS_ShutdownAlternativeRouting`
- `AAS_AlternativeRouteGoals`

Because WORR hands already-loaded AAS bytes directly to Q3A instead of calling Q3A `AAS_LoadMap`, the bridge explicitly calls `AAS_InitAlternativeRouting()` after `AAS_InitRouting()` in `Q3A_BotLibImport_LoadAASBuffer`. Shutdown remains owned by imported Q3A `AAS_Shutdown()`, which calls `AAS_ShutdownAlternativeRouting()`.

The bridge now runs an alternative-route smoke after the normal route smoke. The smoke:

- reuses the route smoke start and goal areas,
- samples both area centers with `AAS_AreaInfo`,
- calls `AAS_AlternativeRouteGoals(..., ALTROUTEGOAL_ALL)`,
- accepts zero returned goals as a valid map-topology result,
- records the first alternative goal when present,
- reports the result through `sg_bot_debug_aas 2` as `q3a_alt_route`.

On `mm-rage`, the current generated AAS returns two alternative goals. The first goal reports `extra_time=65534`; this is the raw Q3A unsigned-short field after upstream alternative-route math wraps a slightly shorter-than-direct path, so the smoke records it without reinterpretation.

## Status Surface

The adapter status now mirrors:

- attempt/pass flags,
- start and goal area numbers,
- returned alternative goal count,
- first alternative goal area,
- first alternative start, goal, and extra travel-time fields,
- failure count,
- human-readable `q3a_alt_route` message.

Verbose runtime output now includes:

- `q3a_alt_route`
- `q3a_alt_route_start`
- `q3a_alt_route_goal`
- `q3a_alt_route_goals`
- `q3a_alt_route_first_area`
- `q3a_alt_route_first_start_time`
- `q3a_alt_route_first_goal_time`
- `q3a_alt_route_first_extra_time`
- `q3a_alt_route_failures`

## Validation

Build:

```powershell
meson compile -C builddir-win sgame_x86_64
```

Install refresh:

```powershell
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas
```

Dedicated smoke:

```powershell
$env:WORR_LOG_LEVEL = 'debug'
try {
  .\.install\worr_ded_x86_64.exe +set logfile 1 +set logfile_name q3a_aas_alt_route_smoke +set logfile_flush 1 +set dedicated 1 +set game basew +set sg_bot_enable 1 +set sg_bot_debug_aas 2 +map mm-rage +wait 90 +quit *> .tmp\q3a_aas_alt_route_smoke_stdout.log
  $code = $LASTEXITCODE
} finally {
  Remove-Item Env:WORR_LOG_LEVEL -ErrorAction SilentlyContinue
}
exit $code
```

Observed smoke status:

```text
q3a_alt_route=Q3A AAS alternative route query passed: start=3 goal=6 goals=2 first_area=10 start_time=72 goal_time=39 extra_time=65534 failures=0
```

The dedicated server exited with code `0`.

