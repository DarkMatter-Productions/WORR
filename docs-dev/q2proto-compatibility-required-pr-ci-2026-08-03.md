# q2proto compatibility required PR CI

Date: 2026-08-03

Project task: `DV-03-T01`

## Outcome

The pull-request workflow now treats the complete vendored `q2proto/tests`
compile/link suite as a distinct required result instead of relying on the main
engine build as indirect protocol evidence. The implementation does not modify
any file under `q2proto/`.

The supported suite currently contains nine executables:

- vanilla, Q2PRO extended, Q2PRO extended v2, and Q2REPRO configurations;
- a deflate-enabled build for each of those four configurations; and
- the combined single-source integration fixture.

The suite is intentionally a compile/link compatibility test. Its fixtures say
they do not perform sensible runtime behavior, and the upstream workflow builds
rather than executes them. WORR therefore preserves that test contract and does
not promote the fixture binaries into runtime tests.

## Required CI gate

`.github/workflows/pull-request.yml` defines the independently named
`q2proto compatibility` job on the fixed `ubuntu-24.04` hosted-runner label. It
uses GCC, Ninja, and the repository's hash-locked Meson requirement. The label
does not make the hosted image hermetic or immutable. The gate has a ten-minute
job bound, while `tools/run_q2proto_compatibility.py` applies a shared 300-second
bound to configuration, introspection, and compilation together. It configures
with:

- `--fatal-meson-warnings`;
- `-Dwerror=true`;
- `--wrap-mode=nodownload`; and
- `debugoptimized`, matching the vendored suite default explicitly.

The job is not allowed to continue on error. It explicitly compiles all nine
reviewed targets with two workers, so a partial/default-target-only invocation
cannot satisfy the gate.

All four pull-request checkout steps use the immutable Checkout v6.0.2 commit
and `persist-credentials: false`. The result publisher uses the immutable
Upload Artifact v7.0.1 commit.

## Read-only and process-containment invariants

The runner requires both build and result directories to be outside the vendor
tree and refuses overlapping output roots or non-empty reused roots. It takes a
deterministic SHA-256 inventory of every regular file, directory, and symbolic
link under `q2proto/` before configuration and after all cleanup. A byte, path,
type, or link-target change fails the run.

Every tool command starts with detached standard input. POSIX commands run in a
new session, and timeout cleanup terminates and, when necessary, force-kills the
whole process group before the runner returns. Windows commands use the shared
no-window, kill-on-close job-object process policy. This prevents a compiler or
Ninja descendant from surviving the timeout and continuing to mutate output
after the result is recorded.

## Published evidence

The pinned upload step runs with `always()` and treats missing results as an
error. It retains the following files for fourteen days:

- `report.json`, including status, bounds, exact expected/observed targets,
  step outcomes, vendor fingerprints, and every output size/SHA-256;
- `targets.json`, the Meson target introspection used by the verifier;
- configure, introspection, compile, and Meson diagnostic logs; and
- a concise Markdown summary.

The runner fails if any reviewed target is missing, unexpected executable
coverage appears without an intentional contract update, a target is not built
by default, an output escapes the build root, or an expected binary is absent.
This makes the uploaded evidence correspond to the exact gate that ran.

## Validation

Focused Windows contracts cover command construction, exact inventory,
build-root anchoring, vendor fingerprinting, product hashes, timeout reporting,
POSIX and Windows containment policy, result publication, immutable action pins,
credential isolation, and the hash-locked Meson setup. The same wrapper was also
run under Ubuntu 24.04/WSL with GCC 13.3: Meson configured nine targets and Ninja
completed all 276 compile/link steps with Werror enabled. The generated report
recorded nine observed targets, nine hashed products, and equal before/after
vendor snapshots.

The final repository validation should continue to include the focused Python
contracts, YAML parsing, action-pin scans, and the exact Linux wrapper invocation
used by CI.
