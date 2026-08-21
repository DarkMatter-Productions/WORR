# RmlUi No-Input Route-Stack Navigation Matrix (2026-08-03)

Task: `DV-03-T07`

## Outcome

The guarded RmlUi runtime harness now has a bounded renderer-native navigation
evidence mode in addition to its isolated document screenshot modes. The new
`--navigation-matrix` mode is designed to prove the ordered route-stack journey

`main -> options -> video -> Back -> options -> Back -> main -> Back -> inactive`

once through each configured native renderer family: OpenGL, conventional
Vulkan, and RTX/vkpt.

The implementation adds the missing command sequencing, checkpoint parser,
failure gates, and deterministic evidence schema between the static 58-route
navigation-graph audit and the isolated route captures. Dry-run and fixture
tests prove that contract. A live three-renderer execution was deliberately not
run during this change, so real route-stack restoration evidence remains
pending.

`DV-03-T07` remains open. The route-wide captures still use manual visual review
for most layout judgments, the default automated viewport pair is still 4:3,
and live UI execution is not yet a required pull-request CI lane.

## Safety contract

The navigation mode does not synthesize or inject keyboard, text, pointer,
button, or wheel input. It invokes only existing engine console route commands
and reads engine console-log evidence. It does not request a screenshot, use an
operating-system capture API, or redirect Vulkan/RTX work through OpenGL.

Every lane is constructed with:

- `win_headless=1` for the verified hidden-window Windows renderer path;
- `in_enable=0` and `in_grab=0` so client input initialization and capture stay
  disabled;
- `r_fullscreen=0` and `s_enable=0`;
- a lane-specific `homedir` below `.tmp/rmlui/runtime-navigation/`, keeping
  configs, logs, and writable runtime state out of the user's normal home and
  the staged install;
- the lane's real `r_renderer` value (`opengl`, `vulkan`, or `rtx`).

Before a live launch, the runner revalidates the no-window/input-off command
contract and rejects any synthetic-input or screenshot command. Navigation
base-game and evidence names are restricted to portable identifiers before they
can enter an engine command or evidence path. Windows launches additionally use
`stdin=DEVNULL` and `CREATE_NO_WINDOW`. Live navigation execution is fail-closed
outside Windows until another platform has an equivalent verified
renderer-capable no-window path. The bounded timeout rejects non-finite,
non-positive, and greater-than-300-second values. Existing scratch path
components are inspected without following them, and any symbolic link,
junction, or Windows reparse point fails the launch before directory creation.
All resolves are exception-safe, and the complete component, resolve, and
containment contract is repeated after directory creation and immediately
before process creation. After its parent path passes that contract, the runner
atomically creates the lane leaf with an exclusive claim. Any pre-existing lane
home, even an empty one, fails closed, so another same-evidence harness run or
archived settings cannot share the home and prior checkpoints cannot be
appended into new evidence.

The atomic claim coordinates trusted local harness invocations; it is not a
security boundary against another same-account process deliberately modifying
the newly claimed directory. An immediate emptiness check and final full path
contract narrow accidental changes before process creation. Holding a
Windows-specific no-delete directory handle across launch is outside this
bounded harness change.

## Journey and evidence protocol

The harness opens the root through `ui_rml_runtime_capture_menu main`, then uses
the existing `ui_rml_runtime_open` and `ui_rml_runtime_back` commands. A bounded
frame wait precedes each labelled `ui_rml_runtime_status` checkpoint:

| Checkpoint | Expected active state | Expected route |
|---|---:|---|
| `main` | yes | `main` |
| `options` | yes | `options` |
| `video` | yes | `video` |
| `back_options` | yes | `options` |
| `back_main` | yes | `main` |
| `inactive` | no | `<none>` |

Validation requires the checkpoint labels in this exact order and exactly one
status, frame-counter, route-counter, and input-counter record for each label.
Every active checkpoint must report at least one update and render,
`availability='ready'`, and the exact native renderer family. The final
checkpoint must be inactive, name no route, and report a close request and
close. All input and synthetic-input counters must remain zero, and the legacy
synthetic-input marker is forbidden.

Frame counters are route-local rather than cumulative:
`UI_Rml_MarkRouteActive()` clears the metrics whenever a route is opened or
restored. Requiring positive counters at every active checkpoint therefore
proves that the newly active document rendered after that transition; an
earlier route cannot satisfy a later checkpoint.

Each lane writes its source log below its isolated
`.tmp/rmlui/runtime-navigation/` home. Successful logs are copied to
`.tmp/rmlui/runtime-capture/`. The aggregate
`.tmp/rmlui/runtime-capture/navigation-matrix.json` uses stable renderer order,
checkpoint names, evidence names, sorted JSON keys, and the
`worr.rmlui.runtime_navigation.v1` schema. An explicit `--write-manifest` path
can override that default.

## Commands

Inspect the exact hidden, input-free commands without launching the client:

```text
python tools/ui_smoke/check_rmlui_runtime_capture.py --navigation-matrix --dry-run --format json
```

Validate already-recorded isolated-runtime logs:

```text
python tools/ui_smoke/check_rmlui_runtime_capture.py --navigation-matrix --format json
```

The optional `--run` path is deliberately limited to the verified Windows
hidden-window implementation. It was not invoked for this implementation
validation.

## Automated validation

`tools/ui_smoke/test_check_rmlui_runtime_capture.py` now covers:

- three deterministic renderer-lane commands and the required hidden-window,
  input-off, windowed flags;
- exact journey action/checkpoint order and absence of screenshot and synthetic
  input commands;
- passing ordered OpenGL, Vulkan, and RTX/vkpt fixture logs plus equivalent
  printed JSON/manifest payloads;
- failures for a wrong Back restoration target, missing checkpoint, renderer
  family mismatch, zero render count, active final state, nonzero input, a
  synthetic-input marker, a synthetic-input command, a screenshot command,
  conflicting safety cvars, unsafe identifiers, an unbounded timeout, and an
  unsupported live-run platform;
- isolated `homedir`, no-link/reparse scratch containment, `stdin=DEVNULL`, and
  `CREATE_NO_WINDOW` launch contracts;
- deterministic refusal of a mocked reparse point introduced after the atomic
  leaf claim, resolution failures, pre-existing empty lane homes, and nonempty
  lane homes containing stale config/log state, without invoking a process.

Focused validation completed without launching a client:

```text
python -m pytest -q -p no:cacheprovider tools/ui_smoke/test_check_rmlui_runtime_capture.py
57 passed, 1 skipped

python -m pytest -q -p no:cacheprovider tools/ui_smoke
395 passed, 1 skipped
```

## Remaining `DV-03-T07` work

- Add responsive 16:9 and wider viewport assertions, then extend meaningful
  layout checks beyond the synthetic smoke document.
- Decide on a trusted required automation lane capable of executing the native
  renderer journeys and engine-render-target captures without creating a
  visible window or initializing input.
- Record the first live OpenGL/Vulkan/RTX journey manifest through that policy;
  this implementation validated only dry-run, fixture, parser, and failure
  contracts.
- Preserve the separate static navigation, runtime service, renderer-family,
  and no-Vulkan-to-OpenGL guardrails while expanding representative runtime
  journeys for popup and session-owned flows.
