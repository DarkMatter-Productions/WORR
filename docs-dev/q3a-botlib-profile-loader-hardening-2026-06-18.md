# Q3A BotLib Profile Loader Hardening

Date: 2026-06-18

Tasks: `FR-04-T13`, `DV-07-T06`

## Summary

This round hardens the server-owned bot profile loader and profile smoke path
for packaged repository botfiles without changing `q2proto` or the `sg_bot_add`
fallback path. The final shape supports Q3A/Gladiator-style character entry
points and companion scripts under `botfiles/bots/`.

## Implementation Notes

- Added deterministic scanner markers around every profile reload:
  - `q3a_bot_profile_scan=begin`
  - `q3a_bot_profile_scan_source`
  - `q3a_bot_profile_scan_skip`
  - `q3a_bot_profile_scan=end`
- Added a monotonically increasing in-process profile reload number and included it in scan markers and profile-smoke output.
- Added scanner summary fields for candidates, loaded/active profiles, duplicate IDs, load failures, path failures, limit failures, parse warnings, and final status.
- Added parse diagnostics for malformed profile records that previously failed silently, including truncated keys/values, unexpected `=`, missing values, delimiter-only values, and files with no recognized profile fields.
- Preserved permissive forward compatibility: unknown profile keys are still ignored, and profile files with recognized identity or behavior fields continue loading.
- Added Q3-style source ID handling for `botfiles/bots/*.c`:
  - `*_c.c` files become profile IDs without the `_c` suffix.
  - `*_w.c`, `*_i.c`, and `*_t.c` files are reported as
    `reason=companion_script` and are not loaded as profiles.
- Added Q3-style field aliases:
  - `CHARACTERISTIC_NAME` -> `name`
  - `CHARACTERISTIC_REACTIONTIME` seconds -> `reaction` milliseconds
  - `CHARACTERISTIC_AGGRESSION` -> `aggression`
  - `WORR_SKIN`, `WORR_TEAM`, `WORR_AIM_ERROR`, `WORR_PREFERRED_WEAPON`,
    `WORR_CHAT_PERSONALITY`, `WORR_ROLE`, and `WORR_MOVEMENT_STYLE`
- Added `sv_bot_profile_smoke_target` with default `smoke` so packaged profile smoke runs can name the profile ID or display name they are validating explicitly.
- Updated `sv_bot_profile_smoke` to use the configured smoke target and report the reload number and parse-warning count that produced the smoke result.

## Validation

- Build passed:
  `meson compile -C builddir-win worr_ded_engine_x86_64 worr_ded_x86_64 sgame_x86_64`
- Install refresh passed and rebuilt `.install/basew/pak0.pkz` from the current `assets/` tree:
  `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
- Repository-assets scanner reload passed before the companion-file reshape:
  `.\worr_ded_x86_64.exe +set basedir E:\Repositories\WORR +set game assets +set logfile 1 +set logfile_name q3a_bot_profile_loader_assets_scan +set logfile_flush 1 +set developer 1 +sg_bot_reload_profiles +quit`
  - `q3a_bot_profile_scan_source reload=1 dir=botfiles/bots ext=.c candidates=5`
  - `q3a_bot_profile_scan=end reload=1 candidates=5 loaded=5 active=5 duplicates=0 load_failures=0 path_failures=0 limit_failures=0 warnings=0 status=ok`
  - `Loaded 5 bot profiles.`
- Archive-only staged profile smoke completed but did not find profiles because this dedicated launch did not mount `.install/basew/pak0.pkz` in the search path:
  `.\worr_ded_x86_64.exe +set basedir E:\Repositories\WORR\.install +set game basew +set logfile 1 +set logfile_name q3a_bot_profile_loader_hardening_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sv_bot_profile_smoke 2 +set sv_bot_profile_smoke_target smoke +map mm-rage`
  - `q3a_bot_profile_scan=end reload=1 candidates=0 loaded=0 active=0 duplicates=0 load_failures=0 path_failures=0 limit_failures=0 warnings=0 status=empty`
  - `q3a_bot_profile_smoke=begin target=smoke reload=1 profiles=0 warnings=0 found=0`
- Loose staged profile-add smoke passed after temporarily mirroring the current repository botfiles into `.install/basew/botfiles/bots`; the loose files were removed after validation:
  `.\worr_ded_x86_64.exe +set basedir E:\Repositories\WORR\.install +set game basew +set logfile 1 +set logfile_name q3a_bot_profile_loader_hardening_loose_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sv_bot_profile_smoke 2 +set sv_bot_profile_smoke_target smoke +map mm-rage`
  - `q3a_bot_profile_scan=end reload=1 candidates=5 loaded=5 active=5 duplicates=0 load_failures=0 path_failures=0 limit_failures=0 warnings=0 status=ok`
  - `q3a_bot_profile_smoke=begin target=smoke reload=1 profiles=5 warnings=0 found=1`
  - `q3a_bot_profile_smoke_after_add count=1 name=B|Smoke profile=smoke skin=male/grunt skill=4 reaction=250 aggression=0.65 aim_error=2.5 preferred_weapon=rocketlauncher chat=quiet role=attacker movement=strafe`
  - `q3a_bot_profile_smoke=end final_count=0`
- Follow-up staging validation passed after `tools/package_assets.py` began
  mirroring `botfiles` loose in refreshed installs:
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
  - `python tools\bot_scenarios\run_bot_scenarios.py --scenario profile_backed_spawn --binary .install\worr_ded_x86_64.exe --install-dir .install --artifact-dir .tmp\bot_scenarios --json-out .tmp\bot_scenarios\profile_backed_spawn_report.json --format text`
  - Result: profile scan loaded 5 profiles, found `smoke`, spawned `B|Smoke`, and cleaned up to final count 0.
- Q3-style reshape validation passed after replacing the flat profile files
  with `*_c/_w/_i/_t.c` scripts:
  - `meson compile -C builddir-win worr_ded_engine_x86_64 worr_ded_x86_64 sgame_x86_64`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
  - `python tools\bot_scenarios\run_bot_scenarios.py --scenario profile_backed_spawn --binary .install\worr_ded_x86_64.exe --install-dir .install --artifact-dir .tmp\bot_scenarios --json-out .tmp\bot_scenarios\profile_backed_spawn_report.json --format text`
  - Result: passed. The scan sees 20 `.c` candidates under `botfiles/bots`,
    skips 15 companions, loads 5 active profiles, resolves profile id `smoke`
    from `smoke_c.c`, and cleans up to final count 0.

## Remaining Risks

- The loader remains intentionally permissive for Q3A-style profile compatibility; diagnostics warn about malformed records but do not reject them.
- Dedicated builds with `USE_ZLIB 0` still do not mount `.pkz` archives. The
  refreshed install now stages `botfiles` loose as the supported fallback for
  server-side profile scripts while keeping the packaged archive member.
