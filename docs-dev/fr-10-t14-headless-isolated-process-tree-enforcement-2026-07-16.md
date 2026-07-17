# FR-10-T14 headless isolated process-tree enforcement

Date: 2026-07-16  
Project tasks: `FR-10-T14`, `FR-10-T15`

## Purpose

Automated networking evidence must never initialize physical client input, grab
the mouse, display a window, or leave an engine process behind after the test
has reported completion. This change turns that requirement into one shared
runtime-launch policy rather than relying on each Python runner to duplicate
direct-process termination.

## Shared implementation

`tools/networking/headless_process.py` is the single process-policy surface for
the networking runtime gates.

- `start_headless_process` requires `stdin=subprocess.DEVNULL` and the
  platform no-window creation flags. On Windows the required flag is
  `CREATE_NO_WINDOW`.
- Each Windows process is assigned to a private job object configured with
  `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`. Closing the job destroys every
  remaining member, including a child that outlives a launcher process.
- `terminate_process_tree` still invokes hidden
  `taskkill /PID <pid> /T /F` while the direct root remains live, waits for
  that direct handle, and always closes the tracked job before reporting
  success.
- If a Windows job cannot be created, configured, or assigned, the start fails
  closed after cleanup. A runner never continues with an untracked launch whose
  descendants might escape the isolated test directory.

The policy is used by all networking runtime launchers that create a live
process:

- canonical weapon-damage acceptance;
- native command-shadow runtime smoke;
- historical moving-brush gate;
- live rail-damage gate; and
- staged impairment smoke.

Client command construction remains separately responsible for
`win_headless=1`, `in_enable=0`, and `in_grab=0`. The helper only permits
the already-required no-window, detached-stdin process configuration; it does
not synthesize gameplay input or alter client/server authority.

## Launcher-exit boundary

The earlier direct-handle cleanup correctly killed a process that was still
running, but a bootstrap launcher can exit before a descendant has fully
finished. In that timing window a later root-only `taskkill` cannot discover
the orphan. The private job closes that gap: its membership outlives the
launcher's direct handle and its kill-on-close limit applies to every process
in the isolated tree.

No user process is selected by this mechanism. The only PIDs eligible for a
job or `taskkill` are handles returned by the current test runner's own
`Popen` call.

## Verification

Focused shared-policy, input-contract, and affected-runner contracts pass
94/94. They cover mandatory detached stdin/no-window routing for each launcher,
the exact hidden Windows tree-kill command, root-handle waiting, the tracked
job close after a launcher has already exited, POSIX direct-handle fallback,
and rejection of an interactive stdin launch.
The shared process-policy contract is registered in the standard networking
suite; the reconfigured full Windows Meson suite passes 138/138.

One fresh staged, dedicated-plus-two-hidden-client Proximity Launcher lifecycle
run passed at
`.tmp/networking/canonical-prox-launcher-lifecycle-job-cleanup-runtime.json`.
It retained the normal policy-17 acceptance evidence: 56 ms authenticated
current-world forward progress, no historical impact, normal mine landing,
trigger, delayed explosion, and exactly 61 current-world RadiusDamage. The
runner reported all server, shooter, and target processes as harness
terminated. An immediate post-run process audit found no
`worr_x86_64`, `worr_ded_x86_64`, `worr_engine_x86_64`, or
`worr_ded_engine_x86_64` process.

This is process-lifecycle hardening only. It does not claim the still-open
telemetry, load, malformed-input, soak, platform-matrix, or rollout acceptance
work required to complete `FR-10-T14` or `FR-10-T15`.
