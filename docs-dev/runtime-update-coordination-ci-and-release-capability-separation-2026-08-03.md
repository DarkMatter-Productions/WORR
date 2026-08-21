# Runtime/Update Coordination, Staged CI, and Release Capability Separation

Date: 2026-08-03
Status: Implemented in source and workflows; external production controls remain under `DV-08-T16`
Project tasks: `DV-02-T02`, `DV-02-T03`, `DV-08-T16`, `DV-08-T17`,
`DV-08-T18`, `DV-08-T19`, `DV-08-T20`

## Outcome

This follow-up closes the code-completable priorities left by the authenticated
release and atomic-updater work:

- pull requests expose OpenGL, native Vulkan, and RTX renderer builds as three
  independently named lanes;
- every supported platform launches the final staged dedicated runtime in a
  bounded, isolated, input-free smoke test;
- update transactions can migrate complete managed subtrees between file and
  directory shapes with durable rollback/restart recovery;
- one stable top-level file coordinates the complete engine lifetime against
  update apply/recovery;
- Windows elevated-worker handoffs bind the predecessor PID to its immutable
  process creation time;
- `.install` publication tolerates bounded Windows scanner/share races and
  rejects developer runtime state before it can become a package candidate;
  and
- release workflows separate keyless candidate assembly, protected signing
  authorization, and protected publication capabilities.

The last item is meaningful in-repository progress on `DV-08-T16`, not its
completion. Independent KMS/custodian custody, separate protected-environment
reviewers, platform signing/notarization, out-of-band first-install trust, and
hermetic builds still require external administration and credentials.

## Renderer and Staged Runtime CI (`DV-02-T02`, `DV-02-T03`)

The pull-request workflow contains an Ubuntu renderer matrix whose visible job
names and exact Meson targets are:

| Lane | Meson target |
| --- | --- |
| OpenGL | `worr_opengl_x86_64` |
| Vulkan | `worr_vulkan_x86_64` |
| RTX | `worr_rtx_x86_64` |

The configure step explicitly enables external renderers and Vulkan. Each lane
installs the reviewed compiler, Vulkan loader/header, shader compiler, and
window-system dependencies instead of relying on hosted-image accident.

After the Windows, Linux, and macOS matrix builds, each job refreshes
`.install` and runs `tools/run_staged_runtime_smoke.py`. The harness launches
only the public dedicated-server bootstrap and supplies:

- an isolated temporary `homedir` with a pre-created `basew/` directory;
- loopback-only IPv4 and port zero;
- closed standard input and captured diagnostics;
- a one-use random readiness token written by the hosted engine only after
  common initialization;
- a 30-second default timeout and process-tree termination; and
- an install-tree metadata snapshot before/after launch, making any write to
  the distributable tree a failure.

On Windows, the existing headless helper starts the process with no visible
window and owns it through a kill-on-close job object. The test never starts a
client, initializes client input, captures the mouse, or uses screen capture.

## Stable Runtime/Update Lock (`DV-08-T18`)

Every registered manual, update, and MSI payload now contains
`worr_install.lock` at the install root. Its signed contract is exact: an empty
regular file with mode `0644` where POSIX modes are represented. The path is
reserved from transaction plans and the updater never replaces it.

The public bootstrap opens that existing file without creating it or changing
its permissions, takes a shared advisory lock before inspecting/loading the
engine library, and retains the lock through `engine_main` and library unload.
Apply and interrupted-transaction recovery take the conflicting exclusive
lock before any recovery-state creation or managed mutation and retain it
through commit/rollback cleanup. Both sides validate the empty-file/mode
contract after locking. Multiple runtimes may coexist; no update can mutate
the installation until every runtime releases its shared lock.

Windows opens the stable file with a share mode that prevents replacement
while held and uses `LockFileEx`; POSIX uses `O_RDONLY`, `O_CLOEXEC`, and
`flock`. The existing-file lock APIs reject links, reparses, special files, and
hard-link aliases and never create or permission-repair the public guard.

The same audit found that the Windows helper-process path used broad inherited
handle semantics. Child creation now uses `STARTUPINFOEX` with
`PROC_THREAD_ATTRIBUTE_HANDLE_LIST`, allowing only its three intentional
standard-I/O handles to cross the process boundary.

## Windows Handoff Process Identity (`DV-08-T20`)

The protected elevated-worker chain must sometimes wait for its predecessor to
exit before replacing a running updater image. A numeric Windows PID is not a
stable process identity: after the predecessor exits, the operating system may
reuse that PID for an unrelated process, making the child wait for the wrong
process for up to the bounded timeout.

Every such handoff now carries both the predecessor PID and the 64-bit creation
time returned by `GetProcessTimes`. The child opens the process once with
query-and-synchronize rights and compares the creation time on that same held
handle before waiting. A no-longer-existing PID or a creation-time mismatch
means the intended predecessor is already gone; the child proceeds without
waiting on a recycled identity. Partial identities, self identities, query
failures, wait failures, and the existing 60-second timeout fail closed. This
applies to both the unelevated-to-elevated recovery handoff and the
installed-to-protected elevated-worker handoff without inheriting a privileged
kernel handle.

## Managed Path Shape Transitions (`DV-08-T17`)

Transaction journal V3 records each original destination as absent, regular
file, or directory, along with durable-backup readiness and POSIX mode. The
parser retains V2 recovery so an interrupted transaction from the immediately
preceding implementation can still be rolled back.

Before creating private state, preflight walks every affected chain through
the anchored no-follow root. A directory that will become a file is accepted
only when every regular-file leaf below it appears in the authenticated
removal set. Empty, unowned, link/reparse, special, hard-linked, colliding, or
over-limit contents fail without mutation.

Apply order is dependency-safe:

1. back up and remove authenticated old file leaves;
2. journal and remove old directories deepest first;
3. journal and create required new directories shallowest first;
4. install authenticated files; and
5. replace the local manifest last and persist the commit marker.

Rollback traverses journal entries in reverse, removing new leaves/directories,
recreating old directories in parent-first order, and restoring old files and
POSIX modes. The same order is used after a simulated process crash and restart.
Directory removal uses held no-follow handles and identity checks so a raced
leaf is not mistaken for the object that was preflighted.

## Clean and Verifiable `.install` (`DV-08-T19`)

The directory publisher still performs a complete sibling-tree swap with an
old-tree rollback. Windows access denied, sharing violation, and lock violation
errors from a scanner holding a freshly built image are retried for a short,
bounded schedule; all other errors fail immediately, and a failed new-tree
publish restores the previous complete tree.

The Meson `basew` directory is also usable as a local runtime home. It can
therefore accumulate `config.cfg`, `.conhistory`, saves, screenshots, demos,
and logs. Canonical staging removes those paths from the fresh sibling tree
even when they occur in the build input. The release target contract also
forbids the known root config/history and user-data subtrees.

The platform stage validator now inventories the complete pre-role staging
tree and rejects links/reparse points, portable case aliases, registered
forbidden paths, missing required runtimes/modules/renderers/notices/asset
pack, or a missing/non-empty/wrong-mode coordination lock. It intentionally
does not require `worr_update.json` at this phase because
`prepare_role_install.py` creates that channel/role-specific trust document
from the clean shared staging input.

## Release Capability Separation (`DV-08-T16` Progress)

Stable and nightly workflows now use three capability-specific jobs:

1. **Candidate:** contents-read only, no private keys, verifies platform
   artifacts and composes a relocatable deferred-signing request.
2. **Authorize:** contents-read only in the signing environment, independently
   downloads the frozen public policy by its immutable upload artifact ID and
   the build artifacts, strictly verifies
   the candidate against workflow-bound release identity and artifact
   metadata, materializes private keys only after that validation, produces
   both the updater's index signature and a distinct domain-separated
   threshold authorization over the exact canonical request, then removes
   private material before uploading the authorization artifact.
3. **Publish:** contents-write in a separate publishing environment, has no
   signing secrets or persisted checkout credentials, independently downloads
   the same frozen policy by immutable artifact ID, verifies the exact
   request/index/authorization/asset set, and only then permits the draft
   publisher to use `GH_TOKEN`. Policy upload uses a rerun-unique name with
   overwrite disabled; a deleted or replaced artifact therefore fails closed
   instead of falling back to mutable name lookup.

The publication authorization signs the canonical request bytes and filename
under `WORR release publication authorization v1`; it cannot be substituted
for an updater index signature because the schemas, media types, and signing
domains are distinct. The request binds the exact index, policy digest and key
set, output names, and an exact index-derived set of release assets with sizes
and SHA-256 digests. The published set includes the request, index signature,
and request-authorization envelope so the evidence remains independently
auditable after Actions artifact retention expires. The publisher admits no
additional unsigned assets.

Publication compares its pre-network file snapshot with the immutable verified
authorization measurements, reconciles the exact remote asset set and digests,
publishes through a draft gate, then performs a fresh final metadata, asset,
and latest-release check. Both signing and publication reject a candidate whose
generation time is outside the 24-hour authorization window. Publication also
binds the signed channel to its GitHub representation (`stable` is latest and
not prerelease; `nightly` is prerelease and not latest), resolves and peels an
annotated Git tag through at most eight objects, and requires the actual tag
commit before mutation, before asset replacement, and after publication.
Candidate/policy/asset-list substitution, stale authorization, channel-state
remapping, tag races, and cross-domain signature tests fail closed.

This split prevents one ordinary workflow job from simultaneously possessing
candidate control, signing material, and publication authority. It becomes an
independent human control only after repository administrators configure the
two environments with appropriately separate reviewers and custody. The
current signing environment can still materialize all configured threshold
keys in one runner, so it is not independent M-of-N custody.

## Validation

Focused validation covers:

- Windows Meson install-root and transaction tests;
- direct Linux/WSL builds of those tests with strict compiler warnings;
- deep shape-transition success, ordinary rollback, simulated crash/restart,
  V2 journal recovery, POSIX file/directory modes, unowned-subtree rejection,
  lock contention, and no-create/no-permission-mutation lock behavior;
- staged-smoke command, timeout, readiness, process-tree, immutable-tree, and
  required-path contracts plus a real Windows dedicated launch;
- renderer target/workflow source contracts;
- transient directory-publish retry and rollback;
- user-state stripping and exact stage validation;
- Windows predecessor PID/creation-time pairing, same-handle identity
  validation before waiting, and complete command-line propagation; and
- release request authorization, independent-policy anchoring, candidate
  reconstruction, relocation, threshold/cross-domain failure, whole-bundle
  substitution, asset-list races, exact draft publication, and zero-network
  fail-closed behavior.

The final integrated pass completed with 1,568 Python tests passing (8 skipped;
369 subtests), all 216 Meson tests passing, a clean Windows build, a successful
atomic `.install` refresh/validation, and a real Windows staged dedicated smoke.
The release-capability subset accounts for 58 passing tests (3 skipped; 34
subtests). No interactive/fullscreen client, input control, or operating-system
screen capture is part of validation.

## Remaining External Work

`DV-08-T16` remains open until operators provide and exercise all of the
following:

- independently controlled threshold keys or KMS identities such that no one
  runner/process/identity can satisfy the threshold;
- separately governed signing and publishing environments with retained audit
  evidence and rehearsed rotation/revocation/recovery;
- Authenticode signatures for Windows executables/MSI and Apple signing plus
  notarization for macOS;
- an independently authenticated first-install package and trust-policy
  fingerprint channel; and
- pinned/attested or equivalently reproducible hosted images, compilers, SDKs,
  system packages, and release tools.

These require authority and credentials outside the repository. The checked-in
implementation fails closed when its required inputs are absent; it does not
claim those external controls are already provisioned.
