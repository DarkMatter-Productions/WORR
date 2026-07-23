# FR-10-T04 live event-only `0x73` status evidence gate

Project task: `FR-10-T04`

Status: **In Progress**. This slice closes one reporting gap in the bounded
exact-bundle acceptance contract. It does not complete `FR-10-T04`, promote the
native event lane, or change legacy/q2proto traffic.

## Outcome

The canonical `blaster-local-action-lease` child now treats live numeric native
status as required evidence. After its ordinary two-client Blaster callback,
in-session shooter reconnect, and exact local-action authority-receipt parity
have completed, the headless runner:

1. stuffs the existing diagnostic-only `cl_worr_native_shadow_status` command
   to both admitted clients;
2. parses the stable `WORR_NATIVE_CLIENT_STATUS_V1` scalar rows from the
   independent shooter and target logs;
3. queries `sv_worr_native_shadow_status <slot>` for the exact two live server
   peers; and
4. accepts the child only when every endpoint reports schema `1`, protocol
   `1038`, active mode `2`, no failure/rejection, committed wire state, and
   exact public/private mask `0x73`.

The schema-v36 child report adds this explicit shape under each run:

```text
runs[].native_event_shadow.clients.shooter
runs[].native_event_shadow.clients.target
runs[].native_event_shadow.server_peers[0]
runs[].native_event_shadow.server_peers[1]
```

The task parent independently requires that field, both named clients, two
server rows, exact `0x73` public/private values, positive `server_active`, zero
endpoint failure/rejection, reconnect proof, and receipt parity. The accepted
live-row summary is now
`mask_observation=direct-client-and-server-status` and
`numeric_status_in_child_report=true`. The parent removes only the corresponding
open-coverage string. Full-scope evidence construction also rejects an event row
that lacks this direct observation, so a caller cannot publish a nominally full
parent with the event-status gate false. `task_complete` remains hard-coded
false.

The parent is repinned from historical canonical child schema v35 to v36. No
historical JSON report is relabeled or rewritten. In particular, run
`20260720T035616.104289Z-41896` remains historical v35 evidence and retains its
original event-row limitation.

## Safety and compatibility

- The server remains the dedicated executable and never initializes a client
  or renderer.
- Both real clients retain `win_headless=1`, `in_enable=0`, `in_grab=0`, and
  `s_enable=0`, use isolated runtime homes, and receive no device input.
- Status commands are diagnostic-only; they do not alter readiness, ACK state,
  event authority, or presentation.
- The default-off native policy and exact legacy fallback remain unchanged.
- No `q2proto/` file changed, and `.install/` was not refreshed by this slice.

## Initial focused verification (historical)

```text
python -m py_compile \
  tools/networking/run_canonical_rail_damage_runtime_gate.py \
  tools/networking/run_fr10_t04_acceptance.py \
  tools/networking/test_run_canonical_rail_damage_runtime_gate.py \
  tools/networking/test_run_fr10_t04_acceptance.py
PASS

python tools/networking/test_run_canonical_rail_damage_runtime_gate.py
Ran 55 tests - OK

python tools/networking/test_run_fr10_t04_acceptance.py
Ran 11 tests - OK

meson test -C builddir-win --no-rebuild \
  network-canonical-rail-damage-runtime-gate-parser \
  network-fr10-t04-partial-acceptance-parser --print-errorlogs
2/2 passed

git diff --check -- <the four files above>
PASS

git status --porcelain=v1 --untracked-files=all -- q2proto
<empty>
```

Final source hashes at focused verification time:

| File | SHA-256 |
|---|---|
| `tools/networking/run_canonical_rail_damage_runtime_gate.py` | `0e8eb3d68ca029e968fc85f55f97cf5166ddfe750f14fdfa7ab8064e92c1049d` |
| `tools/networking/run_fr10_t04_acceptance.py` | `d9c19981b39a61826f203f4715033838145c966fb830e782e7d64c5bfe0a5d37` |
| `tools/networking/test_run_canonical_rail_damage_runtime_gate.py` | `e8adcfc5782adcac40f3ef52b90b05feb99e3a35b6bfb5124a6f082608f73631` |
| `tools/networking/test_run_fr10_t04_acceptance.py` | `ea4c1ffa9d0664f6aae7b480f07f5515a1e33ee13178855160f0bb581758b42c` |

A provisional real-process launch against the intentionally stale staged v35
sgame was attempted only to exercise orchestration. The v36 runner correctly
could not accept the older status width and timed out before native-status
capture. Its explicitly named
`.tmp/networking/fr10_t04_event_status_v36_provisional.failure.json` and sibling
run directory are **failure diagnostics, not evidence**. Final live evidence
was subsequently generated after the combined v36 build and `.install/`
refresh.

The event row in
`.tmp/networking/fr10_t04_ordered_frontier_full.json` passes with both clients
and both server peers reporting direct public/private `0x73`, an in-session
reconnect, positive native-event receipt correlation, and zero endpoint
failure. The parent records
`mask_observation=direct-client-and-server-status`, passes all five live masks,
and remains `status=partial`, `task_complete=false`. That historical parent has
10 focused children. A later manifest hardening adds cgame local-action
correlation as an eleventh child and binds the shared inputs to the new derived
receipt bound. Final post-capacity parent
`.tmp/networking/fr10_t04_bounded_lifecycle_full_retry.json` passes that
11/11-focused, 5/5-live manifest with the same direct event status and deliberate
`task_complete=false`. The current parent contract is 12/12, and the affected
native/canonical/parent runner tests pass 117/117 in aggregate. See
`docs-dev/fr-10-t04-t08-private-receipt-bounded-lifecycle-2026-07-20.md`.

## Remaining `FR-10-T04` limitations

This slice removes only the event-only direct-numeric reporting gap. The task
remains open for:

- the supported legacy demo/MVD/relay matrix beyond the now-direct live `0x03`
  status row;
- complete game-service event-family adapter coverage;
- native authority promotion beyond default-off shadow adapters;
- multi-client fairness and full deterministic impairment-matrix breadth;
- ACK-exhaustion and map-rotation breadth across every live bundle; and
- sustained load, soak, cross-platform, rollout, and release evidence.
