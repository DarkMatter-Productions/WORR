# Q3A BotLib Memory Allocator Bridge

Date: 2026-06-17

Tasks: `FR-04-T12`, `FR-04-T14`, `DV-07-T06`

## Summary

This round replaces the remaining raw Q3A `botimport.GetMemory`, `botimport.FreeMemory`, and `botimport.HunkAlloc` `malloc` / `free` callbacks with a tracked bot-owned allocator inside the WORR Q3A import boundary.

The new allocator keeps Q3A's existing `MEMORYMANEGER` behavior intact:

- zone allocations are freed through `botimport.FreeMemory`;
- hunk allocations stay grouped while AAS is loaded;
- grouped hunk allocations are released after `AAS_Shutdown` during failed loads, map unloads, and import shutdown.

This is not yet a `gi.TagMalloc` / `gi.TagFree` backed allocator. It is a narrower bot-owned bridge that makes active bytes, peak bytes, allocation counts, grouped hunk releases, failures, and available-budget estimates visible through adapter status. That keeps the next engine-zone decision explicit while removing the anonymous raw allocation callbacks from the imported BotLib table.

## Implementation Notes

- `q3a_botlib_import.c` now owns linked lists for BotLib zone and hunk allocations.
- `Q3A_BotLibGetMemory` and `Q3A_BotLibHunkAlloc` now allocate tracked blocks with a local header and magic value.
- `Q3A_BotLibFreeMemory` validates and releases tracked zone allocations, with defensive support for unexpected hunk frees.
- `Q3A_BotLibReleaseHunkAllocations` releases grouped hunk allocations after `AAS_Shutdown` returns and Q3A has nulled its AAS pointers.
- `Q3A_BotLibReleaseAllAllocations` catches remaining tracked allocations during import shutdown.
- Adapter and runtime status now print a compact `BotLib adapter memory` line under `sg_bot_debug_aas 2`.

## Validation

- `meson compile -C builddir-win sgame_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
- Dedicated server reload smoke:
  `.\.install\worr_ded_x86_64.exe +set logfile 1 +set logfile_name q3a_bot_memory_smoke +set logfile_flush 1 +set dedicated 1 +set game basew +set sg_bot_enable 1 +set sg_bot_debug_aas 3 +map mm-rage +wait 70 +map mm-rage +wait 100 +quit`
- Smoke log evidence:
  `BotLib adapter memory: q3a_memory=Q3A BotLib zone allocation tracked: zone_active=239894 zone_peak=239894 zone_allocs=358 zone_frees=3 hunk_active=691078 hunk_peak=691078 hunk_allocs=17 hunk_groups=0 failures=0 available=15846244`
- The same smoke still reported `q3a_bot_client_command=Q3A BotClientCommand bridge passed: callback=yes client=0 accepted=0 rejected=1 failures=0` and `q3a_route=Q3A AAS route query passed: start=3 goal=6 travel_time=113 reachability=1 route_end=6 stop=0`.
