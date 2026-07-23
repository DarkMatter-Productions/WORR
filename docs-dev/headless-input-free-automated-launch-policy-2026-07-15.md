# Headless, input-free automated launch policy

Date: 2026-07-15  
Project tasks: `FR-10-T11`, `FR-10-T12`, `FR-10-T14`, `FR-10-T15`

## Policy

Automated test launches must not interfere with a developer's desktop. The
workspace rule now requires a dedicated server or an explicit no-window mode,
an isolated runtime directory, and no client input initialization or mouse
capture. Visual assessment remains a separate, deliberately requested activity.

For Windows client automation the required launch settings are:

```text
+set win_headless 1
+set in_enable 0
+set in_grab 0
```

`win_headless` supplies a hidden native surface where a renderer-backed test
needs one. `in_enable=0` exits the input initialization path before platform
mouse setup, while `in_grab=0` is a second explicit no-capture guard. Launchers
also use `stdin=DEVNULL` and `CREATE_NO_WINDOW` on Windows. Dedicated-only
tests continue to use `worr_ded_x86_64.exe` and do not take a client path.
Every networking runtime launcher that creates a live process now uses the
shared `tools/networking/headless_process.py` policy. On Windows it assigns
each isolated process to a private kill-on-close job, invokes hidden
`taskkill /PID <pid> /T /F` while the direct root remains live, waits for that
handle, and closes the job before reporting completion. This prevents a child
engine, renderer, or dedicated process from outliving a launcher that exits
first; failure to establish the job fails the launch closed. No cleanup selects
an arbitrary desktop process: it acts only on the handle created by the current
test runner.
The client additionally fails closed at `IN_Init()`: `win_headless` prevents
platform mouse initialization even if a future launcher omits `in_enable=0`.
`IN_GetCurrentGrab()` also fails closed when this deliberate opt-out leaves the
grab cvar unset; map activation can still call it after the client transitions
to gameplay. This keeps the policy no-window and input-free through the full
connection lifecycle.

The Windows mouse backend independently enforces the same boundary. In
`win_headless` it refuses raw-mouse registration and every direct grab request;
its cursor clipping and warping helpers are no-ops. This protects the desktop
if a future window, focus, resize, or renderer path reaches the platform
backend without passing through `IN_Init()`.

## Applied launchers

The rule is enforced by command-building contracts for networking live snapshot
and native-shadow runs. The same settings are applied to staged impairment,
renderer-parity, Vulkan debug, shadowmapping, and RmlUi capture tools. RmlUi's
`ui_rml_runtime_synthetic_input` remains engine-generated test input, not OS
pointer input, and continues to work with physical input disabled.

The canonical two-client weapon gate additionally sets `cl_async=1`: command
finalization stays on its independent physics cadence while the hidden renderer
is absent from the input clock. Its sustained-beam modes issue one ordinary
`+attack` only; they never synthesize release/press refresh edges.
The same gate covers bounded Disruptor, Rocket, Plasma Gun, and shared
Blaster/HyperBlaster projectile-spawn-forward policies with an explicit 100 ms
server cvar cap; each three-repeat acceptance run remains dedicated/headless
and input-free. Rocket, Plasma Gun, and Blaster evidence further require an
explicit no-historical-impact result, so current-world projectile authority
cannot be misreported as a hitscan rewind.
The Rocket splash scenario uses the same dedicated host and two hidden,
input-disabled clients; its current-world impact fixture is server-created
geometry, never a client input or mouse-driven test action.
The Plasma Gun direct-hit scenario uses that same host/client configuration and
a server-staged target position. Its only client action is the ordinary
server-stuffed `+attack`; no radius test, input initialization, or mouse
interaction is added.
The Blaster scenario uses the same dedicated host, hidden clients, and sole
ordinary server-stuffed `+attack` action; it introduces no mouse-driven or
physical-input validation path.
The HyperBlaster cadence scenario uses the same dedicated host and two hidden,
input-disabled clients. Its ordinary client-side release/press edge advances
the production held fire loop without OS input initialization, physical mouse
interaction, or mouse capture.
The Chainfist hybrid-melee scenario keeps that same dedicated/headless,
input-disabled two-client configuration. Its one ordinary server-stuffed
`+attack` reaches the production repeating weapon path; historical
reach/FOV selection, live displacement validation, current `CanDamage`, and
normal `Damage` require neither physical input nor mouse initialization.
The ETF flechette scenario uses the same dedicated host and hidden,
input-disabled clients. Its held-command refresh is a normal client-side
release/press edge after the repeating weapon enters firing state; it neither
constructs server input nor initializes or captures a mouse.
The Phalanx scenario uses the same dedicated host, hidden clients, and
input-disabled launch contract. Its Generic fire-frame refresh is an ordinary
client-side release/press edge only; it cannot initialize physical input,
construct server-side input, or capture the mouse. The runtime gate verifies
that all processes are terminated after every repeat.
The Phalanx splash/occlusion mode uses exactly the same dedicated host and
hidden, input-disabled client configuration. Its off-axis target and
non-damageable current-world blocker are server-owned fixture geometry; the
two ordinary client-held attack edges never initialize or capture a mouse.
The Grenade Launcher ballistic/splash mode uses the same dedicated host and
hidden, input-disabled client configuration. Its present-world damageable
blocker and off-axis target are server-owned fixture geometry; the normal
grenade touch and splash must fall in the closed 57–60 production-damage
window, and the runner verifies all three processes terminate after each
repeat without OS input or mouse capture.
The Hand Grenade release-ballistic mode uses that same dedicated/headless,
input-disabled configuration. Once the server has observed the normal priming
attack, its sole later action is a client-side `stuff <id> "-attack"` release;
the client sends the ordinary key-up command promptly, without constructing
server input, enabling physical input, or initializing/capturing a mouse. Its
clear-flight-only acceptance deliberately creates no contact fixture, so it
cannot turn a release test into a mouse-driven, interactive, or synthetic
damage path.
The Proximity Launcher deployment mode uses the same dedicated host and hidden,
input-disabled clients. Its only action is the normal server-stuffed
`+attack` command; server-owned fixture state supplies neither a mine landing
nor a trigger. The clear gravity-path proof therefore cannot initialize input,
capture a mouse, fabricate deployable damage, or turn the later mine lifecycle
into an interactive test path.
The separate Proximity Launcher lifecycle mode preserves that same launch
contract. After the accepted clear flight it stages only a non-damageable
current-world landing surface and, after normal `prox_land`, an isolated player
position. Production physics, arm delay, candidate/visibility scan, delayed
explosion, and `RadiusDamage` remain the only collision and combat authority;
the runner has no OS-input, mouse, or interactive action beyond the ordinary
server-stuffed `+attack`.

## Validation

```powershell
python -m unittest tools/networking/test_run_staged_impairment_smoke.py tools/networking/test_run_native_shadow_runtime_smoke.py tools/networking/test_run_native_shadow_repeated_runtime_smoke.py tools/networking/test_run_live_snapshot_acceptance_gate.py
python -m unittest tools/renderer_parity/test_run_renderer_perf_capture.py
python -m unittest test_run_vk_debug_smoke.py  # run from tools/renderer_parity
python -m pytest tools/ui_smoke/test_check_rmlui_runtime_capture.py -q
python -m unittest tools/networking/test_headless_input_contract.py
python -m unittest tools/networking/test_run_canonical_rail_damage_runtime_gate.py
```

The shared cleanup regression, affected-runner contracts, and headless-input
contract pass 94/94, including an actual staged lifecycle run that leaves no
WORR client, dedicated server, or engine process behind. The detailed
implementation record is
`docs-dev/fr-10-t14-headless-isolated-process-tree-enforcement-2026-07-16.md`.
The standard registered Windows Meson suite passes 138/138 with the shared
process-policy contract included.
Earlier focused evidence remains valid: networking 69/69; renderer performance
4/4; Vulkan debug 2/2; and RmlUi capture 28/28.
