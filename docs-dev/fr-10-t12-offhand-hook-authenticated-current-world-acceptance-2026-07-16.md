# FR-10-T12 native off-hand Hook current-world acceptance

Date: 2026-07-16  
Project task: `FR-10-T12`

## Decision

The legacy off-hand `hook` client string command has no mapped canonical
command-time identity. It remains normal current-world production behavior and
cannot borrow any rewind or spawn-forward decision.

This change adds a separate native `+hook` user-command action. Its bit is
preserved by the legacy user-command writer, mirrored in the game input
contract, and consumed once by the server after ordinary client movement. The
server accepts it only for a live playing client while `g_allow_grapple` and
`g_grapple_offhand` are enabled and no hook is already active.

## Authority policy

Policy 24, `WORR_REWIND_WEAPON_OFFHAND_HOOK_SPAWN_FORWARD`, permits only the
new hook's initial clear current-world forward hull sweep. It uses the
server-authenticated command age, caps the physical advance at 100 ms, and
does not perform a historical trace, collision test, target selection, or
damage result.

The normal hook callback remains authoritative for contact, attachment, pull,
cable state, damage, reset, detach, lifetime, and all later interaction. A
current-world block simply leaves ordinary production behavior in charge.
This is not a claim of predicted cgame Hook presentation or reconciliation;
that remains within the broader open `FR-10-T08`/`FR-10-T12` work.

## Acceptance evidence

- Static contracts: 39 canonical runtime-gate tests and 45 lag-compensation
  contract tests pass, including native Hook bit preservation, protocol mask
  coverage, explicit legacy-string exclusion, and no historical trace claim.
- Builds: `sgame_x86_64.dll`, `worr_engine_x86_64.dll`, and `worr_x86_64.exe`
  build successfully; the distributable stage is refreshed under `.install`.
- Runtime: `canonical-offhand-hook-debug-runtime.json` passes, then
  `canonical-offhand-hook-install-runtime.json` passes three consecutive
  dedicated-server/two-client runs. Every accepted run reports policy 24, a
  clear authenticated/advanced 56 ms current-world sweep, no historical hit
  or damage, and terminated server/client/engine processes.
- Regression: `meson test -C builddir-win --print-errorlogs` passes. The
  working tree diagnostic `git diff --check` exits successfully; its existing
  CRLF warnings are unrelated repository line-ending noise.

Every launch used the automated headless contract: `win_headless=1`,
`in_enable=0`, `in_grab=0`, disabled audio, an isolated runtime directory,
hidden/no-stdin process creation, and kill-on-close job cleanup. No test
initialized input or captured the mouse. A post-run process check found no
remaining WORR process.

## Status

This accepts one bounded current-world input/launch seam only. It does not
complete `FR-10-T12`, does not promote a native transport authority path, and
does not change roadmap task totals.
