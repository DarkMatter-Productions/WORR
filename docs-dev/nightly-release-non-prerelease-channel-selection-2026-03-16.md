# Nightly Releases Without GitHub Prerelease Flag (2026-03-16)

Task IDs: `DV-08-T08`

## Summary
Nightly releases now publish as normal GitHub releases instead of GitHub
prereleases.

To keep stable and nightly updater behavior correct after that change, the
Windows updater no longer relies on GitHub's global `releases/latest` endpoint.
It now evaluates the full release list and filters entries by channel/tag plus
the `allow_prerelease` policy.

## Problem
The previous release model depended on this split:

1. stable releases were regular GitHub releases
2. nightly releases were GitHub prereleases
3. stable updater configs used `releases/latest`
4. nightly updater configs used `releases`

That only worked while GitHub's prerelease flag separated stable and nightly.
Once nightly releases are no longer flagged as prereleases, `releases/latest`
can return a nightly release, which would make stable installs see nightly
payloads.

## Implementation
### Workflow changes
- `.github/workflows/nightly.yml` now publishes the nightly tag with
  `prerelease: false`.
- Nightly packaging no longer passes `--allow-prerelease`, so generated
  `worr_update.json` files stay aligned with the new non-prerelease release
  model.

### Updater release selection changes
- `src/updater/worr_updater.c` now always queries
  `https://api.github.com/repos/<repo>/releases`.
- Release JSON parsing now reads the GitHub `prerelease` boolean per release.
- Channel selection now applies before accepting a release:
  - `stable` accepts non-`nightly` tags
  - `nightly` accepts tags containing `nightly`
- `allow_prerelease` now means "permit prerelease-tagged releases to be chosen"
  instead of switching between `releases` and `releases/latest`.

## Result
- Nightly tags are published as standard GitHub releases.
- Stable updater configs still resolve stable releases only.
- Nightly updater configs still resolve nightly releases only.
- GitHub release selection no longer depends on the prerelease UI flag for
  channel separation.

## Validation
Validation performed locally:

```powershell
python -m py_compile tools/package_release.py tools/release/package_platform.py
meson compile -C builddir worr_updater_x86_64
```

Manual code validation:
- confirmed `.github/workflows/nightly.yml` no longer sets `prerelease: true`
- confirmed nightly packaging no longer passes `--allow-prerelease`
- confirmed updater release selection now filters the full GitHub release list
  by channel and prerelease policy
