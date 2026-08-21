# Authenticated Release, Atomic Updater, and PR CI Hardening

Date: 2026-08-03
Status: Implemented in source and workflows; production enablement remains
subject to the external controls in `DV-08-T16`
Project tasks: `DV-08-T02`, `DV-08-T03`, `DV-08-T13`, `DV-08-T14`, `DV-08-T15`,
`DV-08-T17`, `DV-08-T18`, `DV-08-T19`, `DV-02-T01`, `DV-02-T02`,
`DV-02-T03`, `DV-02-T04`
Tracked follow-up: `DV-08-T16`

## Outcome

This work extends the artifact and path validation completed in
`DV-08-T13` into a complete release-authenticity and install-transaction
boundary:

| Task | Result |
| --- | --- |
| `DV-08-T02` | A native Ed25519 trust policy authenticates the exact release-index bytes before the bootstrap consumes release metadata. Stable and nightly packaging fail closed without a public trust policy, and publication fails closed without enough authorized private keys to meet the policy threshold. |
| `DV-08-T03` | Update application uses install-root-anchored, no-follow file operations, authenticates the bytes copied from the open staging handle, commits each file atomically, and writes the local manifest last. Private, bounded state supports deterministic restart recovery, and the Windows elevated-worker chain is staged and revalidated inside the protected install namespace before it can cross the privilege boundary. |
| `DV-08-T13` | The registry, platform metadata, exact bundle-local assets, manifests, archives, and updater configuration are one verifier contract. MSI validation opens the installer as a real database, extracts it in isolation, and binds the exact extracted client payload to the signed manifest; staging and package/index output sets are assembled before atomically replacing their destinations. |
| `DV-08-T14` | Producer and consumer paths use one ASCII-only portable domain. ZIP verification and extraction reject Unix special-file types, while POSIX installation preserves executable bits from valid regular-file entries. |
| `DV-08-T15` | Signed generation/expiry times, a positive monotonic channel sequence, and exact-index-digest revocations enforce freshness, anti-rollback, and emergency release withdrawal. Pull requests enforce append-only revocation history, release identity is stable across reruns where it must be, and publication uses an exact-digest-checked draft gate. |
| `DV-08-T17` | Transaction journal V3 records files, directories, absence, and POSIX modes so complete authenticated file/directory shape transitions can apply, roll back, and recover after restart without deleting unowned subtree contents. |
| `DV-08-T18` | The hosted engine holds a shared lock on the stable signed `worr_install.lock` for its whole lifetime, while apply/recovery hold the conflicting exclusive lock through cleanup. |
| `DV-08-T19` | Clean staging strips runtime user state, validates the registered pre-role runtime contract, and tolerates only bounded transient Windows scanner/share failures during rollback-capable directory publication. |
| `DV-02-T01` | Pull requests configure and compile the supported Windows, Linux, and macOS target matrix, run the repository Python suite and renderer guardrail, refresh `.install`, and run a bounded headless Meson test selection. |
| `DV-02-T02` | Pull requests expose separate external OpenGL, native Vulkan, and RTX build lanes with exact Meson targets and explicit renderer dependencies. |
| `DV-02-T03` | Each supported platform launches only the final staged dedicated runtime with isolated writable state, detached input, a readiness token, bounded process-tree cleanup, and an immutable-install check. |
| `DV-02-T04` | Every PR matrix build refreshes and validates the platform-specific pre-role staging payload, including registered runtimes/modules/renderers, notices, asset-pack format, forbidden user state, and the exact coordination lock; role-package verification separately binds generated configuration and manifests. |

The checked-in workflows intentionally fail before packaging or publication
while a required trust-policy or signing-key secret is absent, malformed, out
of scope, or below threshold. Secret provisioning is necessary but is not the
whole production trust posture: independent key custody, platform code
signing/notarization, an out-of-band first-install trust channel, and hermetic
build inputs remain operational work under `DV-08-T16`.

## Release Authenticity (`DV-08-T02`)

### Signed object and trust root

The authenticated object is the exact byte sequence of the channel release
index. The detached schema-1 envelope identifies that payload by:

- its exact filename;
- media type `application/vnd.worr.release-index.v3+json`;
- byte size and SHA-256 digest; and
- one or more Ed25519 signatures.

The signing input is domain-separated with
`WORR release index signature v1`, then length-prefixes the filename, media
type, and payload. Renaming an index, changing its media type, changing any
payload byte, or moving a signature onto a different payload therefore
invalidates verification. A key ID is the lowercase SHA-256 digest of the raw
32-byte Ed25519 public key; key documents whose ID and public key disagree are
rejected.

The public schema-1 trust policy contains a positive signature threshold and
public keys scoped to explicit repository and channel strings. Parsers reject
unknown or duplicate fields, duplicate keys or scopes, non-canonical base64url,
private material in a public policy, invalid thresholds, and oversized
collections. Repository and channel scopes are trimmed printable ASCII and
are checked before a signature contributes to the threshold. Producer and
consumer enforce the same operational maxima: a 32 MiB index, 1 MiB envelope,
32 policy keys, 64 signatures, 64 repository scopes and 64 channel scopes per
key, and 255-byte payload-name/scope strings.

`tools/release/release_signing.py` is the producer and offline verification
implementation. `src/updater/release_signature.cpp` independently implements
the consumer contract and verifies Ed25519 signatures with Monocypher. The
native known-answer fixture was generated through the Python implementation,
so it also guards the byte-level interoperability contract.

Before writing anything, the index composer computes lexical and filesystem
identities for every metadata/artifact input and for the index, signature, and
asset-list outputs. It rejects output/output, output/input, hard-link, and
same-file aliases. A direct CLI invocation therefore cannot sign an artifact's
old digest and then truncate that authenticated input through a colliding
output path.

### Signed index to package chain

The bootstrap does not parse and trust release-index fields before signature
verification. After verification it requires the expected schema, repository,
channel, platform, and role, rejects duplicate active-platform targets and
duplicate GitHub asset names, and resolves the exact updater manifest and ZIP
package declared for that role. The manifest must itself be schema 3 and its
platform ID/OS/architecture and build commit must match the selected signed
target and release index.

The signed target's `files` records authenticate the manifest and package by
exact name, kind, role, size, and SHA-256. The downloaded manifest is checked
against its signed index record before it is parsed. The manifest's package
record must then match the package integrity carried by the signed index. This
creates the following chain:

```text
embedded public trust policy
  -> exact release-index bytes + threshold Ed25519 signatures
     -> signed role-manifest name/size/SHA-256 descriptor
        -> exact raw manifest bytes
           -> manifest package descriptor equals the signed package descriptor
              -> exact package name/size/SHA-256 and downloaded bytes
                 -> exact staged and live managed-file paths/sizes/SHA-256 values
```

The publication verifier applies the same exactness one layer earlier. Each
`metadata-<platform>.json` document owns one flat artifact-bundle root, and
every named asset must be the unique direct file in that root. Recursive
fallback, cross-bundle lookup, mixed flat/nested layouts, portable-name aliases,
and duplicate metadata in a bundle are rejected. The composer consumes those
verified metadata records and independently checks that their registry,
repository, channel, version, commit, build, platform, role, artifact name,
kind, size, and digest identities agree before producing the index. A file from
one matrix artifact therefore cannot silently satisfy another platform's
record merely because its basename matches.

Windows installer verification is semantic as well as structural. A bounded
Compound File preflight rejects malformed storage before a platform parser is
invoked. Windows then opens the database through the system Windows Installer
API and queries the `File` table; non-Windows database verification uses
`msiinfo` from `msitools` to require and export the same table. Both paths fail
closed if the parser is unavailable, the database cannot be opened, the table
is absent, or it contains no payload row. A synthetic file with only plausible
Compound File header/root bytes is not accepted as an MSI.

Final publication verification additionally requires `msiextract`. It
preflights the complete File-table path set against the signed client manifest,
requires one exact install-root prefix, and rejects unsafe, duplicate,
case-colliding, or file/directory-prefix-colliding paths. Extraction occurs in
an isolated private directory; the resulting tree must contain exactly the
listed files and parent directories, with no symbolic link, special file,
missing path, extra path, or spelling/case drift. Every managed client file must
match the signed size and SHA-256, and the embedded local install manifest must
be the byte-exact canonical serialization of the corresponding client manifest
and install-state fields. POSIX permission modes are intentionally not compared
for MSI because Windows Installer does not represent that ZIP-specific contract.

Updater-bearing archives also have an exact configuration contract. The
embedded document's key set must be exactly `repo`, `channel`, `role`,
`release_index_asset`, `launch_exe`, `autolaunch`, `allow_prerelease`,
`preserve`, and `release_trust_policy`. Each value must equal the value derived
from the verified release metadata, role manifest, canonical preserve list,
channel policy (`autolaunch=true`; prerelease only for nightly), and signer
policy. Missing, extra, or merely plausible configuration fields fail
verification. The canonical preservation set covers the engine's actual
`basew/save/*` path as well as the older `basew/saves/*` spelling.

Only an explicitly generated `development` configuration may set
`allow_unsigned_development_index`. Stable and nightly package tools reject
that exception, and production package configuration requires an embedded
public trust policy. Private key material is never accepted in the updater
configuration or packaged trust policy.

### Signed freshness, anti-rollback, and revocation (`DV-08-T15`)

Every signed release index now carries these security fields inside the exact
bytes covered by the detached threshold signature:

- `generated_at_utc`: a canonical `YYYY-MM-DDTHH:MM:SSZ` UTC timestamp;
- `expires_at_utc`: a later timestamp in the same canonical UTC form;
- `release_sequence`: a positive unsigned 64-bit value that release operations
  must increase monotonically within a channel; and
- `revoked_release_index_sha256s`: canonical lowercase SHA-256 digests of exact
  raw signed release-index byte sequences that the release withdraws.

`compose_release_index.py` defaults the validity interval to 180 days for
stable releases and 14 days for nightly releases. An explicit interval must be
between 1 and 366 days, and the native consumer rejects any signed interval
longer than 366 days. The checked-in publication workflows pass
`github.run_number * 100 + github.run_attempt` as the channel release sequence;
the workflow bounds the attempt and the resulting signed integer before use.
Direct offline composition defaults to the current UTC epoch second but still
requires a positive value. Release operators remain responsible for preserving
monotonicity when composing outside those workflows.

The consumer applies a 24-hour clock tolerance in both directions: it rejects
an index generated more than 24 hours in the future and one whose expiry is
more than 24 hours in the past. This tolerance prevents small host-clock errors
from immediately stranding a client; it does not extend the signed validity
interval or remove the 366-day maximum.

The operational revocation source is the checked-in
`tools/release/revoked-release-indexes.json`. Its schema-1 `stable`, `nightly`,
and `development` channel lists start empty:

```json
{
  "schema_version": 1,
  "channels": {
    "stable": [],
    "nightly": [],
    "development": []
  }
}
```

The selected channel accepts at most 1,024 unique canonical digests per signed
index. To withdraw a release, an operator records the SHA-256 of that release's
exact raw index bytes under the affected channel before composing the next
release. A revocation names an index, not a signing key; signing-key compromise
still requires the policy rotation procedure below.

The pull-request workflow loads the revocation document from the PR base commit
and requires every prior channel and digest to remain present in the proposed
document. This makes the checked-in registry append-only through the normal
merge path; emergency or administrative changes that bypass required checks
remain an operational governance concern.

Discovery considers at most three GitHub feed pages of 100 releases each,
newest first, within one shared 15-second deadline. Each newer feed candidate
must pass threshold authentication, repository/channel identity checks, and
current freshness validation before its revocation set can influence the walk.
Those authenticated revocations accumulate across the bounded walk and are
applied to every older fallback candidate, so skipping an otherwise unusable
newer release cannot bypass its withdrawal of an older index. A candidate that
revokes its own exact digest is also rejected. This is exact-byte revocation:
reformatting or regenerating metadata produces a different index digest and
requires a separately authenticated release sequence.

A schema-2 pending cache is update-candidate evidence, not durable installed
state. On resume, its exact index, signature, manifest, identity, sequence,
freshness, and current policy are revalidated; an expired cached candidate or
one revoked by authenticated newer discovery evidence cannot be installed.

The authenticated evidence retained with an already installed release has a
different lifetime. It is reverified against the current policy and must remain
internally valid, but publication expiry is not reapplied as an installation
expiry. Its authenticated `release_sequence`, exact release-index digest, and
managed-file manifest remain the rollback floor and removal authority after the
publication window closes. For such an authenticated install, the consumer
rejects a lower candidate sequence and rejects a different index digest at the
same sequence; the same sequence and same digest remains available for repair.

### Authenticated cached resume

Pending state schema 2 retains the raw release index, detached envelope, and
raw update manifest. It also records derived metadata for diagnostics, but
those recorded assertions are not treated as proof.

Before a cached update can produce a plan, the bootstrap:

1. enforces bounded state, index, signature, and manifest sizes;
2. re-verifies the retained envelope and exact index bytes under the currently
   embedded public policy;
3. reselects the active platform and role from the authenticated index;
4. re-derives paths, asset names, release identity, package integrity, and
   verified key IDs instead of trusting their cached copies;
5. verifies the retained raw manifest against the signed size and digest; and
6. parses and validates the manifest only after that authentication succeeds.

Legacy pending-state schemas and incomplete or unverifiable evidence are
ignored as update candidates. They cannot block launch of the installed game,
and they cannot authorize a download or install. The explicit unsigned
development mode follows the same re-derivation path but is restricted to the
`development` channel.

The committed local install manifest carries the same raw signed-index,
signature-envelope, and raw-manifest evidence. Before that manifest may
authorize deletion of a managed file, the bootstrap revalidates the evidence
under the current policy, requires the reconstructed authenticated manifest to
equal the local document, and verifies that the live file still has the prior
signed size and digest. If authentication is missing or the live file changed,
the updater preserves the stale-file candidate instead of deleting it.

### Bounded network inputs

The updater bounds release-list, release-index, signature-envelope, manifest,
and cached-state bodies before parsing or retaining them. All metadata and
package requests are HTTPS-only, including redirects. The unsigned development
index exception changes authenticity policy; it does not weaken transport
protocol restrictions. Failure to apply libcurl's initial or redirect protocol
restriction is itself fatal rather than silently continuing with a weaker
transport policy.

Package transfer is constrained by the size authenticated in the release
index and by a 4 GiB consumer implementation ceiling. Publication applies the
stricter GitHub asset limit of 2 GiB minus one byte. The write callback aborts
before crossing its bound, the final byte count must be exact, and any failed,
short, or oversized transfer removes its partial file. The package digest is
checked before extraction. This prevents an authenticated small-package record
from being used as an unbounded disk-write sink.

Producer and final verifier enforce the consumer's installability limits before
publication: at most 99,999 managed files, at most 32 MiB for an update
manifest, at most 4 GiB per managed file, at most 16 GiB expanded payload, at
most 160 MiB for the embedded authenticated local manifest, and at most 8 MiB
for one release's worst-case serialized transaction-journal contribution. The
native transition journal supports 200,000 records and 16 MiB so disjoint
predecessor/next release sets that each satisfy the producer limit can be
represented. The final verifier checks declared and actual package sizes,
expanded sizes, entry modes, and prefix collisions. A correctly signed but
unusable release is therefore rejected at production time instead of being
discovered first by an installed client.

## Deterministic Workflow and Publication Boundary

Reruns reuse the run-owned public-policy artifact identity keyed only by
`github.run_id`; uploads explicitly replace that run-owned artifact, and every
platform/signing job downloads the same bytes from the visible
`release-policy/` directory. Platform metadata uses the same run ID as its
build identity, so a rerun does not create a mixed matrix merely because
`github.run_attempt` changed. Platform artifact uploads likewise use explicit
replacement semantics.

Nightly date, tag, display name, and commit-window boundary are derived from
the workflow run's GitHub `created_at` timestamp rather than the wall clock of
the current attempt. A rerun after midnight therefore remains attached to the
original nightly identity. Immediately before composition, stable and nightly
jobs confirm that the checked-out commit is the prepared commit and that an
existing release tag, if present, resolves to that same commit.

The publication helper creates or reuses a draft, clears assets left by a
failed draft attempt, uploads the complete intended set, and compares the
remote asset names, states, byte sizes, and GitHub-reported SHA-256 digests with
the local files. Only that exact set can be promoted from draft. It repeats the
check after publication; a rerun may accept an already published release only
when its complete assets and release metadata already match. It does not edit
or silently reconcile a mismatched public release.

Release notes publish the public trust-policy file alongside its SHA-256,
threshold, and authorized key IDs. That fingerprint gives operators and
mirrors a stable value to compare, but a fingerprint displayed only by the
same GitHub release is not an independent first-install trust anchor. The
out-of-band distribution requirement remains in `DV-08-T16`.

## Signing and Key-Rotation Operations

### Required GitHub Actions secrets

Stable releases require:

- `WORR_STABLE_RELEASE_TRUST_POLICY_B64`
- `WORR_STABLE_RELEASE_SIGNING_KEY_B64`

Nightly releases require:

- `WORR_NIGHTLY_RELEASE_TRUST_POLICY_B64`
- `WORR_NIGHTLY_RELEASE_SIGNING_KEY_B64`

Each value is canonical standard base64 of the complete UTF-8 JSON document,
with no surrounding whitespace. Stable and nightly should use independent key
material unless an explicit operational risk decision says otherwise. The
trust-policy secret is public material, but it remains a required controlled
input because every platform package must embed the same intended policy. The
prepare job decodes it once and uploads a 90-day, run-ID-scoped policy
artifact with explicit rerun replacement; every platform build and the final
signing job download that exact artifact. Before private keys are materialized,
final artifact verification
opens every updater package and requires its embedded policy to equal the
signer's policy document. The private-key secret must be restricted to the
protected publication environment and must never be committed, logged,
uploaded as an artifact, or copied into `.install`.

The public trust-policy values must be available to each workflow's `prepare`
job. Private stable and nightly key bundles belong in the separately protected
`stable-release-signing` and `nightly-release-signing` environments used only
by the final publication jobs.

A minimal public policy has this shape (use real derived values and the exact
repository/channel identity):

```json
{
  "schema_version": 1,
  "threshold": 1,
  "keys": [
    {
      "key_id": "<sha256-of-public-key>",
      "algorithm": "ed25519",
      "public_key": "<canonical-unpadded-base64url>",
      "repos": ["owner/repository"],
      "channels": ["stable"]
    }
  ]
}
```

A public policy must never contain `private_key`.

The repository helper can generate a one-key policy and its matching private
document. Run it only in the intended offline custody environment and write to
paths outside the checkout:

```text
python tools/release/release_signing.py generate-key \
  --private-key-path <secure-offline-path>/stable-private.json \
  --trust-policy-path <secure-offline-path>/stable-policy.json \
  --repo owner/repository --channel stable
```

The signing-key secret accepts either one schema-1 private-key document or a
rotation bundle:

```json
{
  "schema_version": 1,
  "keys": [
    {
      "schema_version": 1,
      "algorithm": "ed25519",
      "key_id": "<sha256-of-public-key>",
      "public_key": "<canonical-unpadded-base64url>",
      "private_key": "<canonical-unpadded-base64url>"
    }
  ]
}
```

The workflow decodes private keys only into a private runner directory, creates one
non-link file per key with owner-only permissions where supported, verifies
key consistency and repository/channel authorization, and emits a private
path-list document for the composer. Duplicate identities, external or linked
paths, and a key set below the scoped trust-policy threshold are rejected.
Direct signing and composer entry points convert key paths to lexical absolute
paths without resolving away a final link, then validate with `lstat`/the
platform equivalent; a symlink or reparse key input therefore remains visible
and fails closed.
Release artifacts are downloaded and verified before private keys are
materialized. The private directory exists only across index composition and
an `always()` cleanup step removes it before release-note generation or
publication; cleanup validates that the target is a non-link, prefix-matched
direct child of the runner temporary root before recursive removal.

The publication-side signing environment installs only binary wheels from a
hash-locked requirements file: `cryptography==46.0.3`, `cffi==2.1.0`, and
`pycparser==3.0`. Release workflow actions are referenced by immutable commit
SHAs rather than mutable version tags, and the selected action releases use
the Node 24 action runtime. Linux/macOS release and PR builds install the exact
hash-locked Meson wheel from `tools/release/build-tool-requirements.txt`, and
Windows installer jobs request the exact WiX Toolset package version. These
controls pin important orchestration inputs; they do not make hosted runner
images, compilers, SDKs, MSYS2/Chocolatey/Apt/Homebrew repositories, or every
system dependency hermetic. Closing that wider reproducibility boundary is a
`DV-08-T16` operational follow-up.

All keys in the current threshold bundle are decoded in one GitHub job and are
therefore in one runner compromise domain. A threshold greater than one
enforces publication and rotation policy, but it does not yet provide
independent M-of-N custodian security. That stronger property requires keys in
separate offline/KMS/custodian contexts plus a workflow that aggregates
detached signatures without co-locating private material and is tracked by
`DV-08-T16`.

The offline CLI already supplies the safe aggregation primitive for that future
workflow. Each custodian can sign the same exact index independently, after
which a keyless aggregator accepts repeated `--signature` inputs, verifies every
partial envelope against the public policy and repository/channel scope,
rejects duplicate key identities or payload mismatches, emits a deterministic
combined envelope, and re-verifies the configured threshold:

```text
python tools/release/release_signing.py aggregate \
  --payload <release-index.json> \
  --signature <custodian-a.sig.json> \
  --signature <custodian-b.sig.json> \
  --output-signature <release-index.sig.json> \
  --trust-policy <public-policy.json> \
  --repo owner/repository --channel stable
```

The checked-in GitHub Actions workflows now separate keyless candidate,
protected authorization, and protected publication jobs/environments, but the
authorization job still materializes the configured threshold keys together.
It therefore improves least privilege and reviewer separation without claiming
independent custodian/KMS threshold security.

For a guarded offline handoff, the composer also supports `--defer-signing`.
That mode writes the immutable index and a canonical, relocatable, bounded
signing request that binds the exact index and public-policy digests,
repository/channel, intended signature/authorization/asset-list outputs, and
the exact asset set derived from signed index metadata. It intentionally
creates no publish-ready output. Before keys are materialized, the authorization
job independently reconstructs the candidate from verified artifact metadata
and workflow-bound version/tag/commit/sequence/validity/revocations. Each
`sign-request` partial contains both an index signature and a distinct
domain-separated signature over the request. Request-driven aggregation
revalidates both threshold sets and only then atomically creates the updater
signature, request-authorization envelope, and canonical asset list. The
publisher independently anchors the public policy and admits no unsigned
extra asset. This prevents an incomplete or altered ceremony from becoming
publishable and preserves the request plus both envelopes in the public release
for later audit.

### Provisioning gate

Before enabling the first production run, an administrator must:

1. generate stable and nightly Ed25519 keys offline;
2. retain recoverable offline copies under the project's key-custody policy;
3. create public policies with the exact GitHub `owner/repository` value and
   the matching `stable` or `nightly` channel;
4. base64-encode the JSON documents canonically and configure all four GitHub
   Actions secrets in the scopes described above;
5. run a non-publishing workflow validation or disposable-repository release
   that proves package configs contain only the expected public policy and the
   detached signature verifies independently;
6. record key IDs, custodians, creation date, intended channel, threshold, and
   rotation/revocation state outside the repository's public source tree; and
7. verify installer ownership/permissions prevent unprivileged replacement of
   the installed `worr_update.json` trust root or any ancestor before an
   updater is allowed to run with elevated rights.

No release workflow should be activated before this provisioning gate is
complete, and production distribution additionally requires the external
`DV-08-T16` controls documented below. An absent secret is a release blocker
by design, not a reason to select the unsigned development escape hatch.

### Rotation sequence

Rotation must preserve an overlap period because an installed bootstrap uses
the policy embedded in its current package when it verifies the next index.
A safe one-key-to-one-key rotation is:

1. Generate the new key offline and add its public key to the policy while the
   old key remains authorized.
2. Put both private documents in the schema-1 signing bundle and publish an
   overlap release signed by both keys. Keep the policy threshold compatible
   with clients that know only the old key.
3. After the overlap policy has reached the supported installed population,
   publish another release still signed by both keys whose packaged policy
   removes the old key.
4. Only after that transition is deployed, remove the old private key from the
   CI bundle, revoke it in the operational register, and publish with the new
   key alone.

For a threshold increase, first distribute a policy that contains every key
needed by the future threshold while signatures still satisfy the old policy.
Never change the policy and signer set in a way that leaves either the old or
new installed population below threshold. Do not mutate secrets during an
active release run.

Discovery walks matching releases from up to three GitHub feed pages of 100
releases each and may fall back to an older overlap release whose index the
installed policy can verify. The walk is deliberately capped at 300 candidates
and a shared 15-second discovery deadline; it does not paginate indefinitely.
As described under `DV-08-T15`, revocations from authenticated, current newer
candidates remain effective while the walk considers older fallbacks. This does
not replace policy overlap: an index signed only by an unknown new key cannot
authenticate anything to an old client. Keep at least one overlap release
within the bounded candidate window until every supported installed policy has
advanced. A client that misses the whole window must be moved forward through
an independently distributed transition package. Key retirement must therefore
be based on the supported installed-policy population, not merely elapsed time.

## Atomic Install Transaction (`DV-08-T03`)

`AnchoredFileTree` keeps the staging and install roots open for the transaction
lifetime and resolves each descendant one component at a time without following
redirects. Rollback state is no longer assigned a caller-selected temporary
root: it has the stable, reserved location `.worr-update/` beneath the held
install root so a new process can recover it deterministically.

- Windows uses root-handle-relative native opens with
  `OBJ_DONT_REPARSE`/`FILE_OPEN_REPARSE_POINT`, validates handles after open,
  and performs root-relative atomic replacement through the open temporary
  file handle.
- POSIX uses `openat`, `mkdirat`, `renameat`, and `unlinkat` with
  `O_NOFOLLOW`, validates regular files with `fstat`, and preserves the staged
  permission mode during replacement.

Both implementations reject special files, redirecting ancestors, and
multiply linked managed leaves. The staged size and SHA-256 are calculated
while copying from the already opened source handle, immediately before the
temporary file is renamed into place. A path validation followed by a changed
source path therefore cannot substitute unauthenticated bytes.

The transaction creates `.worr-update/rollback`, `.worr-update/journal`, and
`.worr-update/lock` through the same anchored interface. Payload plans may not
collide with this reserved state namespace. The journal is capped at 16 MiB and
200,000 records, and the plan is rejected before mutation if it cannot fit.
Every recovery read also rechecks the existing private file rather than
repairing an attacker-weakened permission boundary during parsing.

On POSIX, the top `.worr-update` directory is exact `0755` so a non-privileged
launcher can determine whether recovery is needed, while its state children
are exact owner-only `0700` directories and `0600` single-link files owned by
the effective user. On Windows, ACLs are protected and compared as exact
owner/ACE/mask/inheritance sets. Elevated state children grant full access only
to Administrators and Local System; only the top directory grants
Authenticated Users a non-inherited read/list/traverse ACE. Unelevated
per-user state also includes that user's full-access identity. A non-blocking
exclusive lock rejects a concurrent transaction or recovery.

Download, extraction, and staged-payload validation finish before the live
transaction begins, so a failure in those phases has no installed mutation to
roll back. A privileged/root worker uses a random protected
`.worr-update/staging-*` child rather than a caller-writable system temporary
directory. Before extraction, the ZIP central directory must equal the signed
manifest: there may be no unmanaged member; names, regular-file type, declared
size, and normalized `0644`/`0755` mode must match; one file may not exceed
4 GiB; and total expanded content may not exceed 16 GiB. Extraction repeats
the member checks, and staged validation hashes from held no-follow handles.
Scope-bound cleanup removes the temporary package and stage on every exit.

Relaunch occurs only after a successful transaction and pending-state commit;
a relaunch failure reports the launch error but deliberately does not undo an
already authenticated, complete install, which remains available for the next
manual launch.

The transaction validates every install, removal, and manifest path and every
expected digest before mutation. It then backs up each original just before
its mutation, atomically installs or removes requested files, and atomically
writes the local manifest last. Cancellation and filesystem errors enter the
same rollback path. Rollback visits every recorded mutation in exact reverse
order and continues after an individual restore failure; if any restore fails,
the final error reports both the primary apply error and every rollback error.

An update plan records the exact size and SHA-256 of the authenticated local
manifest from which it was derived. After acquiring the exclusive transaction
lock and completing recovery, apply reopens and rechecks that predecessor
manifest before the first mutation. A second updater process cannot race a
stale plan past the signed sequence/digest rollback floor.

Before each live mutation, the updater atomically persists enough state to
distinguish an unprepared backup from a backup that is ready to restore. An
`applying` journal is rolled back in exact reverse order after a restart. A
`committed` marker is authoritative: restart recovery cleans retained backups
but never rolls the completed install back. A `rolled-back` marker likewise
turns a later recovery into cleanup. If recovery itself is interrupted, the
journal and backups remain in place so the next recovery repeats safely.

`RunBootstrapFlow` invokes recovery before `--skip-update-check`, updater
configuration loading, pending-state handling, or any engine launch. On Windows,
a protected install that denies recovery access uses a two-stage handoff. The
installed updater first elevates only to copy itself, through anchored handles,
to `.worr-update/elevated-worker.exe`; that protected child
waits for the staging parent to exit and then performs the approved apply or
narrow `--recover-only` operation. Privileged entry validates the actual
process-image path, install-root/ancestor/worker ownership and DACL boundary,
and handoff shape. An approved apply additionally requires the approved
configuration digest, authenticated local manifest, and installed updater
size/digest/mode. Recovery-only deliberately does not depend on a parseable
release manifest, but it can act only on already protected, exact-ACL journal
state. Direct privileged invocation, a writable ancestor, a reparse or
multiply linked worker, or caller-supplied UI/launch paths fail closed. A normal
journal-free launch performs a read-only recovery check and does not create
`.worr-update/`.

The native test suite covers:

- successful add, replacement, removal, manifest commit, progress, and POSIX
  mode preservation;
- faults after temporary creation and write, before atomic replacement or
  removal, after parent-directory durability barriers, and after journal,
  backup, install, removal, manifest, commit-marker, rollback-marker, and
  cleanup checkpoints;
- a simulated locked/permission-denied file;
- progress-callback cancellation;
- same-size and changed-size staging substitution during the open-handle copy;
- reverse rollback ordering, new-manifest removal, and pre-mutation collision
  rejection;
- continued best-effort restore plus an explicit combined error when rollback
  itself is incomplete;
- interrupted-apply restart recovery, interruption during recovery, idempotent
  repeated recovery, and committed-marker cleanup without rollback;
- rejection of concurrent transaction locks and payload use of the reserved
  state namespace;
- private persistent-state permissions and bounded journal parsing; and
- a journal-free recovery no-op that requires no install-root write access.

The updater's retry policy remains outside the transaction. Each handled
exception or cancellation is all-or-rollback, while an abrupt process
interruption is recovered before the next launch or update attempt.

### Shape-transition and runtime-lock follow-up (`DV-08-T17`, `DV-08-T18`)

Journal V3 extends the original file-only state with absent, regular-file, and
directory records plus POSIX directory modes; V2 interrupted journals remain
recoverable. A file-to-directory migration removes the authenticated old leaf
before creating parent directories shallowest first. A directory-to-file
migration first proves that every regular leaf in the complete old subtree is
present in the authenticated removal set, rejects empty/unowned/special/link
contents, removes leaves then directories deepest first, and installs the new
file only after the old shape is gone. Reverse journal order restores parents,
children, files, and modes correctly during ordinary rollback or restart.

The separate private transaction lock serializes updater instances but could
not prove that an engine had stopped reading managed libraries/assets. The
stable signed top-level `worr_install.lock` closes that boundary. Hosted engine
launch takes an existing-file shared advisory lock before library inspection
and holds it until after `engine_main` returns and the library unloads. Apply
and recovery take the conflicting exclusive lock before private-state creation
or mutation and retain it through cleanup. The exact empty `0644` file is
required in every payload, reserved from update plans, and validated after
locking; the public lock API neither creates it nor changes mode/ACL. Windows
child launch also restricts inherited handles to the intended three standard
I/O handles through `STARTUPINFOEX`.

Detailed ordering, platform semantics, and focused coverage are recorded in
`docs-dev/runtime-update-coordination-ci-and-release-capability-separation-2026-08-03.md`.

## Portable Paths and ZIP Semantics (`DV-08-T14`)

Managed release paths are now deliberately limited to printable ASCII. This
removes the cross-platform ambiguity between Python Unicode case-folding,
normalization forms, filesystem normalization, and the C++ ASCII collision
key. Existing WORR staged assets are within this domain.

The shared producer/consumer policy also rejects absolute or drive-qualified
paths, traversal and empty components, control and Windows-invalid characters,
trailing spaces or dots, reserved device names, overlong components or paths,
and ASCII case/separator collisions. Tests include both precomposed and
decomposed non-ASCII aliases to ensure they fail instead of reaching a
platform-dependent normalization decision.

For ZIP payloads:

- the verifier accepts only regular-file Unix types (or entries without a Unix
  type), and rejects symbolic links, devices, FIFOs, sockets, directories,
  encryption, privileged mode bits, and malformed member names;
- the signed manifest records a normalized mode of exactly `0644` or `0755`,
  and archive verification/extraction requires the member to agree;
- extraction repeats the Unix type, size, path, and mode checks before
  accepting the extracted entry; and
- on POSIX, general directories become exact `0755` and files become exact
  `0644` or `0755`, independent of process `umask`; the anchored install copy
  verifies and preserves that expected mode when the file is committed.

Windows ignores Unix execute bits but still rejects special Unix file types.
Tar publication remains regular-file-only and dereferences accepted hard-link
aliases during production.

## Monocypher 4.0.3 Provenance and Notice

The native verifier uses the upstream Monocypher 4.0.3 release from:

`https://monocypher.org/download/monocypher-4.0.3.tar.gz`

The Meson wrap pins SHA-256:

`8cc9bc341a66249016db9bd70e9142d8d0aef9945973744b1ac05dbc55d8ee66`

The local subproject builds `src/monocypher.c` and the optional
`src/optional/monocypher-ed25519.c` into a private static dependency. Upstream
offers the project under 2-clause BSD or CC0; WORR reproduces the complete
upstream dual-license text. The corresponding notice is tracked as
`src/updater/third_party/MONOCYPHER-LICENSE.txt`, staged to
`licenses/MONOCYPHER-LICENSE.txt`, and required by the release notice contract
so every updater-bearing package reproduces the notice.

## Pull-Request and Full-Test Repairs (`DV-02-T01` through `DV-02-T04`)

`.github/workflows/pull-request.yml` uses a literal, reviewed allowlist for its
Windows, Linux, and macOS runners and platform IDs; pull-request-controlled
code cannot select an arbitrary runner label. Each platform configures and
compiles the bootstrap-enabled build, refreshes `.install`, and therefore runs
the normal platform stage validator. It then uses
`tools/run_fast_headless_tests.py` to select only Meson tests with a declared
timeout at or below the configured bound and refuses commands that directly
launch an interactive client. This keeps automated launches aligned with the
repository's headless and input-free policy.

The `.install` refresh begins from a clean sibling tree and publishes it with
the same rollback-capable directory swap used by release-layout staging. It no
longer layers build outputs over an older staging root, so retained screenshots,
saves, logs, crash data, or other local files cannot become package candidates.
Late staging failure preserves the previous complete tree, and Windows cleanup
retries read-only backup files before considering publication complete. A
bounded retry also covers only Windows access/share/lock violations caused by
a scanner briefly holding a freshly built image. The Meson runtime tree is
treated as untrusted staging input: root runtime configs, console history,
saves, screenshots, demos, and logs are stripped before publication. The
platform validator requires registered runtimes, modules, all three client
renderers, notices, the asset pack, and the exact empty coordination lock, and
rejects links, portable case aliases, or forbidden user paths. Target contracts
independently apply the same forbidden paths to manual and update manifests.

The Linux Python job runs the complete `tools` pytest tree and the
shadowmapping guardrail after installing the same hash-locked signing/crypto
requirements used by publication. It also compares the release-revocation
registry with the PR base commit and rejects removals. `pytest.ini` gives
collection a repository-level boundary that excludes generated builds, staged
output, scratch data, and downloaded subprojects. The q2aas discovery test now
collects in that context, the FR-10 parser no longer contains an invalid regex
escape, and stale Vulkan source-contract tests follow the current native
renderer source layout. The bot-release acceptance unit builds an explicit
temporary package/AAS/scenario fixture instead of silently depending on an old
`.install` or `.tmp` tree, so a clean checkout exercises the same gate.
These changes repair the full Python gate; they do not alter renderer runtime
behavior.

`DV-02-T02` and `DV-02-T03` are complete. Three independently named Ubuntu
lanes build the exact external OpenGL, native Vulkan, and RTX targets. After
each supported platform refreshes `.install`, a dedicated-only harness starts
the public staged server bootstrap with detached input, loopback/port-zero
networking, isolated writable state, a one-use engine readiness token, a
bounded timeout, process-tree cleanup, and an install-tree before/after
snapshot. Windows uses the no-window job-object path. No CI lane starts a
client, initializes client input, captures the mouse, or performs screen
capture. The detailed design and coverage are recorded in
`docs-dev/runtime-update-coordination-ci-and-release-capability-separation-2026-08-03.md`.

## Validation Contract

Focused producer, consumer, transaction, workflow, and collection regressions
were added with the implementation. Before this integrated working tree is
handed off or used for a release, it must satisfy all of the following:

- Python signing, rotation-bundle, secret-cleanup, package, verifier, stage,
  workflow-contract, and bootstrap source-contract tests pass;
- native release-signature, portable-path/ZIP-attribute, anchored-root, and
  install-transaction tests pass on Windows, with POSIX-specific coverage
  compiled and run on Linux or WSL;
- the complete build and all bounded headless Meson tests pass;
- the repository-wide pytest run collects cleanly and passes apart from
  explicitly supported environment skips;
- targeted static analysis reports no diagnostics in the new updater modules;
- a signed producer-to-index fixture verifies and a tampered index, manifest,
  package, cache, signature, scope, or threshold fails closed;
- `.install` is refreshed after the final build and includes the Monocypher
  notice; and
- `git diff --check` is clean.

No interactive client, fullscreen game, input control, or operating-system
screen capture is part of these gates.

## Validation Record

The implementation includes focused regressions for exact bundle ownership,
semantic MSI parsing and extracted-payload binding, archive size/mode/path
limits, atomic producer output sets, draft publication reconciliation, rerun
identity, append-only
revocations, signing thresholds, freshness/replay behavior, private-state
permissions, protected elevation, predecessor-manifest races, POSIX modes, and
restart recovery. The final integrated pass completed with 1,568 Python tests
passing (8 skipped; 369 subtests), all 216 Meson tests passing, a clean Windows
build, an atomic `.install` refresh/validation, and a real staged Windows
dedicated smoke. The release-capability subset accounts for 58 passing tests
(3 skipped; 34 subtests).

## `DV-08-T16` External and Operational Follow-ups

The checked-in stable/nightly workflows now enforce source-level capability
separation. A contents-read/keyless candidate job verifies artifacts and emits
a canonical relocatable request. A contents-read signing-environment job
independently reconstructs the candidate from workflow-bound identity,
revocations, validity, sequence, target metadata, and artifact digests before
materializing keys; it produces the updater index signature plus a distinct
domain-separated threshold signature over the entire request. A separate
publishing-environment job has contents-write but no private keys, independently
anchors the frozen public policy, admits only the exact signed/index-derived
asset set, and verifies all authorization measurements before `GH_TOKEN` can
reach a GitHub call. The request and both envelopes are published for durable
audit. Frozen policies are uploaded without overwrite and later selected by
immutable artifact ID rather than a replaceable name. Signing and publication
both enforce a 24-hour candidate-generation window; publication binds stable
and nightly prerelease/latest semantics and verifies the actual, boundedly
peeled Git tag commit before mutation and again after publication. Environment
protection/reviewer separation must still be configured by repository
administrators.

The source controls above do not complete these production trust requirements:

- **Independent signing custody.** Move threshold keys into independently
  administered offline/KMS/custodian contexts. No runner, process, identity,
  or secret store may access enough keys to satisfy the production threshold;
  the keyless aggregation path must retain auditable approvals, rotation,
  recovery, and incident-response evidence.
- **Platform and initial-distribution identity.** Authenticode-sign the Windows
  bootstrap/updater/MSI and sign plus notarize macOS deliverables. Publish the
  first-install package and public trust-policy fingerprint through an
  independently authenticated project channel so a new user does not have to
  trust a fingerprint obtained only from the release it is meant to verify.
- **Hermetic build inputs.** Replace mutable hosted images and package-manager
  resolution with pinned, attestable compiler/SDK/toolchain/dependency images
  or equivalent reproducible inputs. The current immutable action SHAs,
  hash-locked Python wheels, exact WiX version, source-wrap hashes, artifact
  digests, and draft reconciliation are valuable controls but do not by
  themselves prove bit-for-bit reproducibility.

These items require repository administration, external credentials/services,
and release-operator governance. They are not represented as completed by the
checked-in implementation.

## Compatibility and Residual Risk

- No `q2proto/` file or network protocol is changed.
- Pending-state schema 1 is intentionally rejected; backward compatibility is
  subordinate to refusing unauthenticated resume state.
- `worr_update.json` is the local trust root because it contains the public
  policy and repository/channel identity. The privileged worker now validates
  its actual image, the config, authenticated installed evidence, and the
  owner/DACL/link boundary of the install root and relevant ancestors before
  acting. The initial installer and administrators must still establish and
  retain that protected ownership boundary; the updater cannot repair an
  already untrusted first installation by elevating it.
- Managed non-ASCII release paths are intentionally rejected. A future task
  may introduce Unicode only with one specified normalization and collision
  algorithm implemented identically by every producer and consumer.
- Freshness and exact-index revocation rely on a usable local clock and on
  operators maintaining `revoked-release-indexes.json`. The deliberate 24-hour
  skew window can briefly admit an expired index, while a clock outside that
  tolerance can reject an otherwise valid update. A repository host can also
  withhold a newer revocation-bearing release or push it outside the bounded
  300-candidate, 15-second discovery window. Authenticated metadata enforces
  the defined authenticity and rollback rules when the required evidence is
  available; it cannot prevent denial of update availability.
- The persistent journal and backups provide deterministic recovery after an
  abrupt process interruption. POSIX replacement, removal, journal, and backup
  mutations use file and parent-directory `fsync` barriers, so the recovery
  protocol also covers a sudden host power loss on filesystems that honor those
  barriers. Windows flushes temporary file contents but does not use a proven
  directory or volume metadata barrier; Windows therefore claims process-crash
  recovery only, not sudden-power-loss recovery.
- Rollback restores prior file bytes and, on POSIX, permission bits. It does not
  claim to restore ownership, timestamps, ACLs, extended attributes, or other
  filesystem metadata.
- Ed25519 authenticity does not provide availability. A repository host,
  network intermediary, or local attacker may still block update discovery;
  they cannot create an accepted release without meeting the configured key
  threshold.
- Key compromise is an operational incident. Remove the affected key from the
  policy through an overlap release where possible, rotate all affected
  channel secrets, and update the external key register. If the last trusted
  key is compromised, recovery requires distributing a newly trusted package
  through an independent trusted channel.
