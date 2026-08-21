# Dependency Baseline Audit and Deterministic Verification

Date: 2026-08-03

Project task: `DV-06-T01`

## Outcome

WORR now has one reviewable dependency inventory at
`tools/dependencies/dependency_baseline.json` and a repository-local verifier
that proves the inventory, Meson declarations, wrap metadata, package
overrides, repository-local sources, and `subprojects/wraps.zip` agree.

This closes the duplicate-version and active-baseline audit required by
`DV-06-T01`. It does not claim that every dependency is the newest upstream
release. Updates remain intentional review work under the follow-on dependency
maintenance tasks.

## Active Wrap Baseline

| Wrap | Active version/pin | Scope | Source integrity |
| --- | --- | --- | --- |
| `cairo.wrap` | 1.18.4 / WrapDB 1.18.4-2 | transitive fallback | source SHA-256 |
| `ffmpeg.wrap` | meson-8.0.1 | direct FFmpeg fallback | HTTPS git tag exception |
| `fmt.wrap` | 12.0.0 / WrapDB 12.0.0-1 | direct | source and patch SHA-256 |
| `freetype2.wrap` | 2.14.1 / WrapDB 2.14.1-1 | direct | source SHA-256 |
| `glib.wrap` | 2.86.3 / WrapDB 2.86.3-1 | transitive fallback | source SHA-256 |
| `harfbuzz.wrap` | 12.3.2 / WrapDB 12.3.2-1 | direct | source SHA-256 |
| `jsoncpp.wrap` | 1.9.6 / WrapDB 1.9.6-1 | direct | source SHA-256 |
| `khr-headers.wrap` | repository-local snapshot | direct Windows headers | repository-local archive SHA-256 |
| `libcurl.wrap` | 8.18.0 | direct Windows fallback | source SHA-256 plus local override |
| `libjpeg-turbo.wrap` | 3.1.3 / WrapDB 3.1.3-1 | direct | source and patch SHA-256 |
| `libpng.wrap` | 1.6.53 / WrapDB 1.6.53-2 | direct | source and patch SHA-256 |
| `monocypher.wrap` | 4.0.3 | direct updater fallback | source SHA-256 plus local override |
| `nasm.wrap` | FFmpeg-managed redirect | transitive build tool | redirect to FFmpeg wrap |
| `openal-soft.wrap` | 1.24.3 / WrapDB 1.24.3-2 | direct | source and patch SHA-256 |
| `pixman.wrap` | 0.46.4 / WrapDB 0.46.4-1 | transitive fallback | source SHA-256 |
| `rmlui.wrap` | 6.2 | direct optional UI fallback | source SHA-256 |
| `sdl3.wrap` | 3.4.0 / WrapDB 3.4.0-2 | direct | source and patch SHA-256 |
| `sdl3_ttf.wrap` | 3.2.2 / WrapDB 3.2.2-2 | direct | source and patch SHA-256 |
| `zlib-ng.wrap` | 2.3.2 / WrapDB 2.3.2-2 | direct zlib fallback | source and patch SHA-256 |

The inventory records every section and field of all 19 active wraps. An
undeclared wrap, missing wrap, changed pin, changed provide alias, unexpected
field, invalid direct/transitive scope, empty active version, non-HTTPS source,
missing SHA-256, unsafe redirect, or duplicate source directory fails
verification.

## Meson and Package Override Contract

The verifier inventories literal dependency, fallback, and subproject
declarations in the root `meson.build` and every recursively discovered
`meson.build` below the bounded first-party `src/` and `tools/` scan roots. The
baseline records the exact set of consumer-bearing files and the declarations
in each file, so adding a dependency in a newly created build file cannot evade
the inventory. Generated, vendored, scratch, and protocol roots are explicitly
outside that scan boundary, including `.install/`, `.tmp/`, build roots,
`subprojects/`, `vendor/`, and read-only `q2proto/`. Direct dependency names
must map to the expected wrap or the explicit system dependency allow-list.

Repository-local patch directories are also exact:

- `packagefiles/curl` must remain referenced by `libcurl.wrap`, and its Meson
  project version must equal 8.18.0.
- `packagefiles/monocypher` must remain referenced by `monocypher.wrap`, and its
  Meson project version must equal 4.0.3.
- unreferenced, nonempty package override directories fail verification.
- symbolic links or special filesystem entries at the `packagefiles/` root or
  anywhere inside a referenced package override fail verification.

Active `.wrap` descriptors must likewise be regular files rather than symbolic
links, directories, or special entries. Baseline wrap keys are validated as
plain `.wrap` filenames before any repository path is resolved or archive input
is read.

## Duplicate and Stale Drift Resolved

The audit found and resolved these tracked inconsistencies:

- `subprojects/wraps.zip` was a second, manually maintained copy of the wraps.
  Its 17 entries matched their standalone files byte for byte, but it omitted
  active `rmlui.wrap` and `monocypher.wrap`.
- `subprojects/.wraplock` was an empty transient lock artifact.
- `subprojects/nasm.wrap.disabled` duplicated the active `nasm.wrap` redirect.
- `subprojects/packagefiles/openal-soft/meson.build` was not referenced by the
  active OpenAL wrap and still declared obsolete OpenAL Soft 1.23.1 while the
  active wrap pins 1.24.3.
- the repository-local `khr-headers.tar.xz` had no hash in its wrap.

The transient lock, duplicate disabled wrap, and obsolete unreferenced OpenAL
override were removed. The KHR wrap now binds the local archive to SHA-256
`50d3a176430248c7e887ff9b078a341adc3b69387f027a4114e97015719a0350`.

Ignored downloaded checkouts and package-cache files were not deleted. The
verifier scans immediate local dependency checkouts by normalized dependency
family and reports stale, duplicate, or unmanaged versioned trees. These are
warnings by default because they are machine-local ignored state; use
`--strict-local-trees` when a clean dependency workspace is required.

## Deterministic Wrap Archive

`tools/dependencies/rebuild_wraps_archive.py` rebuilds `wraps.zip` from the
baseline's sorted wrap names. The builder and verifier both validate and
consume the baseline archive contract; changing its canonical path,
compression, mode, timestamp, or schema fields fails instead of being silently
ignored. Every entry:

- has the exact bytes of its standalone wrap;
- uses the fixed ZIP timestamp `1980-01-01T00:00:00`;
- uses stored compression, avoiding zlib-version-dependent output;
- uses regular-file mode 0644;
- has no archive comment, entry comment, or extra metadata.

The verifier regenerates the expected archive in memory and compares the whole
file byte for byte. Missing, extra, reordered, stale, recompressed, or
metadata-different entries therefore fail closed.

## Explicit Trust Exceptions

### FFmpeg Meson port tag

`ffmpeg.wrap` is the sole git wrap. It selects the GStreamer Meson port tag
`meson-8.0.1` with a shallow HTTPS clone. A tag is not an immutable content
hash, so the baseline records this as an explicit exception rather than
presenting it as hash-pinned provenance. A future dependency update should
prefer an immutable commit or a hash-pinned source archive if the Meson port
can support one.

### Repository-local KHR headers

`khr-headers.wrap` has no upstream URL or upstream release identity. It uses a
curated repository-local archive containing the KHR, GL, and EGL headers needed
by the Windows fallback build. Trust is anchored to the reviewed repository
commit and the wrap's exact archive SHA-256. Updating the headers requires an
intentional archive replacement, hash update, baseline update, notice/license
review, and supported Windows renderer build validation.

No other active wrap is permitted to use a mutable git revision or a
repository-local source archive without adding a reviewed baseline exception.

## Usage and Validation

Verify repository-controlled state:

```text
python tools/dependencies/verify_dependency_baseline.py
```

Require ignored local checkouts to match the baseline too:

```text
python tools/dependencies/verify_dependency_baseline.py --strict-local-trees
```

Rebuild the archive after an intentional wrap update:

```text
python tools/dependencies/rebuild_wraps_archive.py
python tools/dependencies/verify_dependency_baseline.py
```

Focused tests:

```text
python -m pytest -q -p no:cacheprovider tools/dependencies/tests/test_verify_dependency_baseline.py
```

The focused suite covers the live repository contract, byte-reproducible ZIP
metadata, invalid archive contracts and wrap names, missing/stale archive
content, non-regular standalone wraps, local-source hash drift, declarations in
new or existing Meson build files, package-override version drift,
packagefiles-root link rejection where the host permits link creation, stale
disabled wraps, and non-destructive duplicate local-tree reporting.

## Remaining Lifecycle Work

This baseline enables, but does not complete, the follow-on DV-06 work:

- `DV-06-T02`: policy for pruning superseded ignored downloads and caches;
- `DV-06-T03`: dependency update checklist, security review, notices, and
  regression matrix;
- `DV-06-T04`: recurring maintenance cadence and ownership.
