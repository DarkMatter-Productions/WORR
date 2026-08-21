# Release and Updater Trust-Boundary Hardening

Date: 2026-08-03
Status: Implemented in source; final integrated validation remains pending handoff
Project tasks: `DV-08-T13`, with follow-up closure under `DV-08-T17`,
`DV-08-T18`, and `DV-08-T19`

## Summary

This change closes a connected set of fail-open boundaries between release
staging, package production, publication verification, and the desktop
bootstrap updater:

1. Platform metadata could describe an empty or incomplete artifact set, and
   the verifier trusted that self-description instead of the target registry.
2. Client packages could pass without any external renderer library.
3. Package mappings, archive members, manifests, and cached updater state did
   not share a portable relative-path policy before reaching filesystem APIs.
4. Positive package include filters silently omitted the local install
   manifest, causing a published install to be treated as a development tree
   and skip normal update discovery.
5. `.install/basew` staging swept build-root `cgame_*` and `sgame_*` test
   programs into releases alongside the real game modules.
6. Recursive asset lookup could satisfy one platform's metadata from another
   matrix bundle, and a plausible Compound File fixture could pass without
   being an openable Windows Installer database.
7. Role staging, package/manifest generation, and final publication could
   expose a partially replaced output set after an interruption or rerun.

The release target registry is now the authoritative artifact and role
contract. Packages are checked through their metadata, external manifests,
actual archive members, and embedded install manifests. The Python producer
and active C++ consumer enforce the same conservative structural path subset
and ASCII collision policy, staging copies only real game shared modules, and
multi-file producer outputs are staged and committed as coherent sets.

## Root Causes

### Self-described release completeness

`tools/release/verify_artifacts.py` iterated only the asset names declared by
`metadata-<platform>.json`. The metadata was produced beside the assets under
test, so an empty `artifacts` array supplied an empty verification workload.
Missing registered manifests were also skipped. Artifact names, kinds, roles,
and variants had no independent source of truth.

### Copy filters were mistaken for runtime requirements

Renderer globs were package include filters, but no renderer was present in
the required-path lists. Inclusion filters decide what may be copied; they do
not prove that a required build product exists. WORR's hosted client engine
loads external OpenGL, Vulkan, or RTX renderer modules, so publishing none of
them creates an unusable client.

The same distinction caused the generated `worr_install_manifest.json` to be
omitted: it was created after file selection and then admitted only if it
matched a positive include filter, although no target filter named it.

### Unvalidated filesystem-relative data

Mapped sources and destinations, archive member names, manifest paths, and
cached update metadata crossed trust boundaries before a shared safety check.
Hashes alone do not address this class of defect because an archive and its
manifest can agree on the same unsafe destination.

### Over-broad game-module staging

`tools/stage_install.py` overlaid every runtime-looking build-root entry whose
name began with `cgame` or `sgame`. The build root also contains similarly
named native test executables and PDBs. The canonical Windows stage had 30
such files totalling 106,672,640 bytes.

### Cross-bundle and partial-publication ambiguity

Downloaded matrix artifacts share one parent directory in the final job.
Recursive basename search made that parent an accidental global namespace, so
the right filename in the wrong platform bundle could mask a missing asset.
Separately, replacing a package, manifest, index, signature, or asset list one
file at a time allowed old/new combinations to be observed if a process failed
mid-publication. A direct-to-public GitHub release could likewise expose a
partial asset set.

## Implementation

### Authoritative artifact and metadata contract

- `tools/release/targets.py` now produces the exact expected artifact tuples
  (`name`, `kind`, `role`, and `variant`) and exact per-role metadata.
- Platform packaging emits those registry-derived structures and records the
  repository identity. A registry-declared installer is mandatory, must use
  the exact registered filename, and a non-installer target rejects an
  untracked installer argument.
- Verification rejects missing, duplicate, unexpected, or semantically
  swapped metadata and validates platform, archive, updater, role, repository,
  channel, version, commit, and build identity.
- Each metadata file owns one flat bundle root. Every registered asset must be
  the unique direct file in that root, and verification rejects recursive or
  cross-bundle fallback, nested metadata, mixed layouts, and portable-name
  aliases.
- Verification and release-index composition use the same bundle-layout
  resolver. Its preflight recognizes metadata names case-insensitively,
  requires canonical lowercase spelling, rejects portable metadata
  collisions, and accepts either one flat artifact root or one level of matrix
  bundle directories, never a mixture. Before
  either consumer reads metadata, the resolver walks without following
  redirects and rejects symbolic links, Windows reparse points, special
  entries, hard-linked metadata, and an artifacts root whose leaf or ancestor
  is itself a redirecting alias. Once metadata supplies the registered asset
  names, exact resolution rejects hard-linked or aliased assets before their
  content is read or hashed and before any output is created.
- MSI artifacts first pass a bounded Compound File preflight and then must open
  as a real installer database. Windows queries the `File` table through the
  system Windows Installer API; non-Windows database verification uses
  `msiinfo` from `msitools`. Absence of the semantic parser, the table, or at
  least one payload row fails closed. A header/root-only fixture is rejected.
- Final verification also requires `msiextract`. It proves the complete
  File-table path set has one exact install-root prefix matching the client
  manifest, extracts into an isolated private directory, and rejects unsafe,
  duplicate, case/prefix-colliding, symbolic-link, special, missing, extra, or
  differently spelled paths. Every managed client file must match the signed
  size and SHA-256, and the local install manifest must be the byte-exact
  canonical install-state form of that client manifest. MSI does not represent
  POSIX modes, so the verifier deliberately does not claim the ZIP mode check
  for installer payloads.
- Release-index composition refuses repository, channel, version, commit,
  build, platform, role, artifact kind/name, size, or digest identity that
  disagrees with the verified platform metadata. Its index, detached
  signature, and publishable asset list are staged, verified, and atomically
  committed as one output set.
- An updater-bearing archive's config must have exactly the `repo`, `channel`,
  `role`, `release_index_asset`, `launch_exe`, `autolaunch`,
  `allow_prerelease`, `preserve`, and `release_trust_policy` keys. Their values
  must equal the verified metadata/manifest, canonical preserve list, channel
  policy (`autolaunch=true`; prerelease only for nightly), and signer policy.
  Extra or omitted fields are not tolerated. The preserve list covers the
  engine's actual `basew/save/*` path and the historical `basew/saves/*`
  spelling.

### Package and archive integrity

- External manifests must describe the actual package name, format, size, and
  SHA-256 digest.
- Every manifest file entry requires a safe unique path, non-negative bounded
  size, SHA-256 digest, and normalized `0644` or `0755` mode. File/directory
  prefix collisions and the reserved `.worr-update` namespace are rejected.
- ZIP and tar packages are opened and checked member by member. Both reject
  unsafe, duplicate, unexpected, missing, or case-mismatched members; ZIP also
  rejects directory, encrypted, symbolic-link, and non-ASCII legacy-encoded
  entries without the UTF-8 flag, while tar accepts only regular files. Tar
  production dereferences accepted hard-link aliases so each named payload is
  emitted as a regular member. Every managed member's size, hash, and
  normalized mode is compared to the manifest, with per-file, package,
  expanded-size, managed-count, local-manifest, and transaction-plan limits
  enforced before publication.
- Every archive must contain the configured local install manifest. Its
  install-state payload must be semantically equivalent to the external
  manifest after excluding the fields that intentionally differ. The external
  manifest may not list this generated file as ordinary managed content, and
  the semantic check runs independently of ordinary member hash validation.
- Package and external-manifest bytes are completely staged before either
  destination is replaced. Role-install and complete release-layout trees are
  built in sibling staging directories, revalidated, and swapped into place;
  old destinations are restored if replacement cannot complete.

### Renderer and staging requirements

- Manual client and full-update payloads require the exact native OpenGL,
  Vulkan, and RTX library filenames for Windows, Linux, and macOS. Manual
  dedicated-server packages remain renderer-free.
- Client and server game-module requirements also use exact platform library
  names, so a PDB or similarly named test library cannot satisfy `cgame` or
  `sgame` runtime completeness.
- Base-game staging clears generated module-shaped files and overlays exactly
  one real `cgame` and one real `sgame` shared library (`.dll`, `.so`, or
  `.dylib`), plus matching Windows PDBs when present. Test executables,
  extensionless Unix tests, test PDBs, import libraries, and stamps are not
  distributable module outputs.

### Portable path policy

The producer and consumer reject:

- empty, absolute, drive-qualified, UNC, `.` or `..` paths;
- empty components and doubled separators;
- invalid UTF-8, control characters, Windows-invalid characters, trailing
  spaces or dots, and reserved Windows device names;
- components longer than 255 UTF-8 bytes and paths longer than 4096 bytes;
- case-insensitive archive destination collisions, file/directory prefix
  collisions, and updater payloads under the reserved `.worr-update` state
  namespace;
- mapped sources outside the resolved input tree, non-regular sources, and
  files or directories reached through symbolic links;
- collisions among generated package, external-manifest, and local-manifest
  names, or between those names and mapped destinations;
- package output nested below its input, and release-staging input/output
  trees that are equal, ancestors, or descendants of one another;
- release-stage source symbolic links and Windows reparse points, checked
  before replacing an old output and again while copying;
- absolute, traversal, or multi-component base/release game names, and unsafe
  root-relocation destinations supplied to release staging.

The canonical `.install` refresh also starts in a clean sibling tree and
publishes it by rollback-capable directory swap. It therefore removes stale
screenshots, saves, logs, crash artifacts, or other files from the staging
domain before role filters run, rather than trusting every file left by a prior
local build. Read-only Windows files are handled during private-backup cleanup.
Release target contracts separately exclude and forbid saves, screenshots,
demos, logs, and console history in manual and updater payloads.

Backslashes are accepted only as input separators and canonicalized to forward
slashes. Mapped-source suppression tracks the configured source path rather
than its inode, so the mapped path is not swept twice while a distinct,
legitimate hard-link alias remains a distinct package entry. Generated
updater configuration and local-manifest files use atomic replacement, so an
existing hard-linked JSON file is not mutated in place.

The C++ bootstrap validates local and remote manifest paths, release-index
asset names, launch and engine paths, cached pending-update state, install and
removal actions, and every ZIP member before filesystem work. ZIP names are
retrieved dynamically instead of trusting miniz's fixed stat buffer. The
central directory is preflighted against the signed manifest before extraction:
truncated names, embedded nulls, unsafe names, duplicates, case/prefix
collisions, unmanaged members, special types/bits, or mismatched sizes/modes
abort without expanding data. Per-file and aggregate expansion bounds are
enforced again while extracting.

A pending manifest must be a non-empty object with exact internal identity,
package size/hash, and unique per-file size/hash/mode data before cached or
newly discovered state can produce an install plan. Cached and discovered
payloads are additionally bound to the active launcher role, repository,
channel, launch path, and engine path, so a client pending update cannot be
consumed by the dedicated launcher or vice versa. Malformed complete cached
state is cleared from consideration and reported as a warning rather than
blocking launch of the installed game; absent or incomplete cached state
remains an ignorable fallback.

Install application anchors a canonical install root and walks every managed
path component without following symbolic links or Windows reparse points.
It rejects non-directory ancestors and pre-existing hard-linked leaf files,
preflights the complete plan, and repeats those checks during backup, copy,
removal, rollback, directory creation, and local-manifest writing. Copy and
hash measurement use the same held staging handle; destination replacement is
relative to held no-follow roots. The final implementation therefore closes
the earlier path-check/path-use redirector gap rather than relying on a
string-level preflight alone.

### Regression coverage

The Meson `release` suite now includes focused checks for:

- native portable-path and UTF-8 boundary tests;
- release-tool Python unit discovery;
- install-staging/package-assets unit tests;
- the existing headless bootstrap source contract.

Python tests cover empty and swapped metadata, exact bundle-local asset
ownership, release identity, real/fake MSI semantics and extracted-payload
binding, package and member hashes/modes/bounds, exact member case and encoding
flags, unsafe and prefix-colliding paths, mandatory embedded-manifest
semantics, cached-state context validation, mapped-source escapes,
symlink/reparse rejection where the host permits link creation, staging-tree
overlap and argument containment, tar hard-link materialization, atomic tree
and multi-file output replacement, and exclusion of staged test binaries. The
bundle regressions additionally cover case-insensitive metadata collisions,
deep and mixed layouts, direct/nested/cross-bundle asset aliases, hard-linked
metadata and assets, link/reparse root entries and ancestor aliases, and prove
that invalid layout is rejected before metadata reads, asset hashing, or output
publication.

## Validation

Final counts are deliberately deferred until the integrated working tree has
completed its full gate. Required evidence includes the release-tool Python
suite, semantic MSI database and extracted-payload tests with `msitools`, native
updater/root/transaction tests on Windows and POSIX, the
complete compile, every bounded headless Meson test, repository-wide pytest,
an end-to-end signed producer/verifier fixture, a refreshed and validated
`.install`, and a clean whitespace/diff check.

The validation remains entirely headless. It does not launch an interactive
client, control user input, or use operating-system screen capture.

The focused artifact-layout and index-composition validation was run on
Windows with bytecode and pytest caching disabled:

```text
PYTHONDONTWRITEBYTECODE=1 python -m pytest -q -p no:cacheprovider \
  tools/release/tests/test_verify_artifacts.py \
  tools/release/tests/test_compose_release_index.py
44 passed, 3 skipped, 38 subtests passed
```

Two skips are existing compose tests whose symbolic-link creation is marked
non-portable on Windows; the third explicitly records that the host did not
grant live symbolic-link creation to the new layout regression. The new
coverage still exercises Windows reparse-attribute recognition and hard-link
rejection, while its live symlink and root-alias cases run wherever the host
grants that capability. The complete `tools/release/tests` Python suite also
passed with 189 tests, 9
platform/capability skips, and 134 subtests under the same cache-disabled
settings.

## Compatibility and Scope

- No files under `q2proto/` were modified.
- No gameplay, network protocol, renderer implementation, or public updater
  metadata field was renamed.
- Valid release layouts are preserved. Unsafe or non-portable paths,
  incomplete bundles, semantically inconsistent metadata, corrupted packages,
  and missing local manifests are newly rejected.
- Cross-release file-to-directory and directory-to-file managed-path
  transitions are deliberately rejected before mutation. A release requiring
  one must use an intermediate layout; transactional support for that shape is
  not claimed.
- The updater transaction has an exclusive apply/recovery lock, but ordinary
  runtime readers do not yet participate in a process-wide shared lock. This
  lower-risk coordination gap remains accepted and explicit.
- Existing unrelated working-tree changes were preserved and are outside
  `DV-08-T13`.

## Subsequent Closure

The follow-up priorities identified by this audit were implemented in the same
2026-08-03 hardening round:

1. `DV-08-T02` authenticates exact release-index bytes with a threshold,
   repository/channel-scoped Ed25519 policy, binds the signed index to exact
   manifest/package descriptors, re-verifies cached evidence, and bounds HTTPS
   inputs.
2. `DV-08-T03` applies through held root-relative no-follow handles, verifies
   the predecessor local-manifest digest after taking the transaction lock,
   keeps recovery state under exact Windows ACL/POSIX mode rules, and uses a
   protected, actual-image-checked worker chain for elevation and recovery;
   approved apply additionally requires authenticated release evidence.
3. `DV-08-T14` restricts all producer/consumer managed paths to one
   printable-ASCII domain, records normalized `0644`/`0755` modes, restores
   them independently of `umask`, and rejects Unix special types/privileged
   bits.
4. `DV-02-T01` and `DV-02-T04` add a literal supported-platform PR matrix,
   canonical `.install` refresh/validation, locked signing dependencies,
   append-only revocation enforcement, repository Python gate, and bounded
   headless Meson selection.
5. `DV-08-T15` signs canonical generation/expiry times, a positive workflow
   sequence derived from `github.run_number * 100 + github.run_attempt`, and
   up to 1,024 exact-index-digest revocations. Nightly identity is anchored to
   the workflow creation time, policy/build artifacts are rerun-stable, tags
   are bound to the prepared commit, and GitHub publication proceeds through
   an exact-name/size/SHA-256-checked draft.
6. Release notes publish the public policy SHA-256, threshold, and key IDs;
   actions are immutable Node 24-compatible pins; Linux/macOS Meson and signing
   wheels are hash locked; and WiX is version locked.

The complete trust design, validation contract, Monocypher provenance, and
provisioning/rotation runbook are recorded in
`docs-dev/authenticated-release-atomic-updater-and-pr-ci-hardening-2026-08-03.md`.

The subsequent `DV-08-T17` through `T19` follow-up adds journaled managed
file/directory shape transitions, the stable process-lifetime runtime/update
lock, scanner-tolerant clean staging, and an exact pre-role stage validator.
Design and coverage are recorded in
`docs-dev/runtime-update-coordination-ci-and-release-capability-separation-2026-08-03.md`.

`DV-08-T16` remains external and operational. Checked-in stable/nightly jobs
now separate keyless candidate, protected authorization, and protected
publication capabilities and cryptographically authorize the complete
canonical request/asset set against an independently frozen policy. Production
completion still requires independently controlled KMS/custodian signing,
separately governed protected environments, Authenticode and macOS
signing/notarization, an out-of-band first-install policy/package fingerprint,
and hermetic toolchain inputs.
