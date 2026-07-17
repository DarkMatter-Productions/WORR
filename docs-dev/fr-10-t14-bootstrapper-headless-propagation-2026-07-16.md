# FR-10-T14 bootstrapper headless propagation

Date: 2026-07-16  
Project task: `FR-10-T14`

## Purpose

`win_headless=1` must govern the complete client launch path, not only the
hosted engine. Previously a launcher needed the separate private
`--bootstrap-quiet-status` option to avoid creating its SDL splash window.
That let a correctly headless engine launch briefly create a visible
bootstrapper surface.

## Implementation

`src/updater/bootstrap.cpp` now reads forwarded command-line cvars before it
creates a UI. A final `+set win_headless <integer>` setting selects `SilentUi`
when non-zero; later settings retain the engine's ordinary override order.
The public launcher and the updater worker both apply this rule. Worker
invocations also preserve the resolved quiet setting, so an update/relaunch
cannot reintroduce a splash window.

This is intentionally scoped to the engine's command-line `+set` form. It
does not change renderer setup, input cvars, update policy, or normal
interactive launches.

## Verification

`tools/release/test_bootstrap_headless_contract.py` is registered in Meson as
`release-bootstrap-headless-contract`. It verifies that the `win_headless`
forwarded-cvar parser selects `SilentUi` in both public-launcher and worker
paths and that worker invocation retains the quiet option.
