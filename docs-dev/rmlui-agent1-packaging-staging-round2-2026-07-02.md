# RmlUi Agent 1 Packaging Staging Round 2

Date: 2026-07-02

Owner lane: Agent 1, platform/runtime/packaging

Task IDs: `FR-09-T02`, `FR-09-T01`, `FR-09-T09`

## Summary

This round makes the existing asset packager stage loose RmlUi source assets
beside the packaged archive. A normal `tools/package_assets.py` run now mirrors
both `assets/botfiles/` and `assets/ui/rml/` into
`.install/<base-game>/`, while still writing every source asset into the
archive.

No changes were made to `tools/refresh_install.py` or
`tools/stage_install.py`.

## Changed Files

- `tools/package_assets.py`
  - Adds `ui/rml` to the default loose asset mirror paths.
  - Adds an explicit RmlUi loose-asset release member collector.
  - Validates every staged RmlUi member against both the archive member hash and
    the loose `.install/<base-game>/ui/rml/` mirror.
  - Leaves RmlUi staging optional for temporary/test asset roots that do not yet
    contain `ui/rml`, but validates it strictly when the source root exists.
- `tools/test_package_assets.py`
  - Adds a temporary RmlUi fixture and verifies default `package_assets` output
    mirrors `ui/rml` loose.
  - Verifies RmlUi files remain present in the `.pkz` archive and byte-identical
    in the loose staging tree.
  - Extends stale loose-file coverage so both `botfiles` and `ui/rml` mirrors
    are refreshed on repeated runs.

## Task Mapping

`FR-09-T02`: Establishes the `.install/<base-game>/ui/rml/` staging path for
RmlUi document/theme/source assets through the default asset packaging workflow.

`FR-09-T01`: Preserves the round-1 source asset layout contract by treating
`assets/ui/rml/` as the mirrored runtime root.

`FR-09-T09`: Adds focused validation that the RmlUi migration assets in the
archive and loose staging mirror match the authored source files.

## Validation Performed

- `python -m pytest tools/test_package_assets.py`
  - Result: `13 passed in 2.37s`

## Notes

The packager continues to archive the full `assets/` tree. The new loose
RmlUi mirror is intentionally additive and follows the existing botfiles loose
mirror behavior, including removal of stale staged files before copying the
current source tree.
