# Q2 AAS Generator Stage Archive Member Validation

Date: 2026-06-17

Tasks: `FR-04-T11`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice extends the generic staged-release validator so release checks can
require named members inside the base game archive and optionally verify their
SHA-256 hashes. The q2aas refresh path now feeds those requirements from the
q2aas stage report whenever `refresh_install.py --package-q2aas-aas` is used.

The practical effect is that a refreshed `.install/` payload no longer only
proves that `basew/pak0.pkz` exists. It also proves that the generated AAS data
which was staged and injected into the archive is still present under the
expected in-archive path and still matches the hash recorded by strict q2aas
validation.

## Implementation

Updated `tools/release/validate_stage.py`:

- Added `--required-archive-member MEMBER[=SHA256]`.
- Normalizes archive paths to POSIX-style member names.
- Rejects empty, absolute, traversal, directory, and drive-root-like member
  requirements before opening the archive.
- Opens the configured base game archive as a zip/pkz file.
- Fails on duplicate archive member names.
- Fails when a required member is missing.
- Fails when a required member hash does not match the requested SHA-256.

Updated `tools/refresh_install.py`:

- Reads the q2aas stage report when `--package-q2aas-aas` and
  `--platform-id` are both provided.
- Derives archive member names from each enabled `staged_output.aas` path using
  the same base-game-relative rule as the q2aas archive packaging helper.
- Passes each member and its recorded `staged_output.aas_sha256` to
  `tools/release/validate_stage.py`.

This keeps the release validator generic while allowing q2aas packaging to
participate in the normal staged-payload validation path.

## Validation

Python syntax validation:

```powershell
python -m py_compile tools\release\validate_stage.py tools\refresh_install.py
```

Direct staged release validation with a required packaged AAS member:

```powershell
python tools\release\validate_stage.py --install-dir .install --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --required-archive-member maps/mm-rage.aas=6459585e3c15eaa4170e23ca7465fc8255bd95b9b59d42e8615c39a67b707f9c
```

Refresh workflow validation:

```powershell
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas --q2aas-stage-report .tmp\q2aas\stage-report.json --q2aas-package-report .tmp\q2aas\refresh-package-archive-report.json --q2aas-package-audit-report .tmp\q2aas\refresh-package-archive-audit-report.json
```

Expected output includes:

- q2aas AAS archive packaging after `pak0.pkz` is rebuilt from `assets/`.
- archive-required q2aas package audit passes.
- generic release validation passes with `maps/mm-rage.aas` required by name
  and SHA-256.

## Credits

This is a WORR-native release validation and workflow integration change. It
does not import new upstream code. The q2aas data being validated remains based
on the credited `TTimo/bspc` snapshot recorded in
`docs-dev/q3a-botlib-aas-credits.md`.

## Next Work

- Fold required generated AAS members into any future release manifest/index
  metadata once the project defines the shippable AAS policy.
- Add the broader Q2 reference map set so this member validation covers more
  than the current `mm-rage` smoke map.
- Connect runtime BotLib loading to the packaged AAS lookup path.
