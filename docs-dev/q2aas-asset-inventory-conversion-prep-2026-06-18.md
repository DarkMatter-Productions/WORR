# Q2AAS Asset Inventory and Conversion Prep

Date: 2026-06-18

Tasks: `FR-04-T11`, `FR-04-T16`, `DV-07-T06`

## Purpose

This pass adds a small, runtime-independent asset inventory layer for the Q3A
BotLib/AAS port. It helps developers see which WORR maps already have BSP and
AAS coverage, which BSP-backed maps still need conversion, and which planned
reference-map slots are not staged yet.

The work intentionally stays outside runtime C++ and botfile assets. The touched
surface is limited to:

- `tools/aas_inventory/`
- `docs-dev/`
- `.tmp/aas_inventory/` report output

## Repository Layout Findings

Current local reference/source availability checked during this pass:

- `E:\_SOURCE\_CODE\Quake-III-Arena-master`: present.
- `E:\_SOURCE\_CODE\Quake-III-Arena-master\code\bspc`: present.
- `E:\_SOURCE\_CODE\Quake3e-master`: present.
- `E:\_SOURCE\_CODE\baseq3a-master`: present.

Current staged WORR map/AAS layout observed before adding the tool:

- `.install/basew/maps/mm-rage.bsp` is present.
- `.install/basew/maps/mm-rage.aas` is present.
- `.install/basew/pak0.pkz` is present, but its `maps/` members were not present
  in the filtered archive listing checked during this pass.
- `tools/q2aas/validation_manifest.json` currently anchors strict validation on
  `mm-rage` and lists pending reference categories such as `q2dm1`, `q2dm2`,
  open deathmatch, CTF, campaign/co-op, liquids, and mover-heavy maps.

## Tool Added

`tools/aas_inventory/inventory_aas_assets.py` scans loose files and packaged
archives for map-related inputs:

- Loose `.bsp`, `.aas`, and `.map` files.
- `.pkz` / `.zip` archive members.
- Quake II `.pak` archive directory members.

It classifies each discovered map as:

- `ready`: BSP and AAS are both present.
- `needs_conversion`: BSP is present but AAS is missing.
- `source_only`: map source is present but no BSP has been staged.
- `aas_without_bsp`: AAS is present without a paired BSP in scanned roots.

The default scan roots are `assets`, `.install`, and `refs`. The report also
cross-references `tools/q2aas/validation_manifest.json`, records required
manifest maps, and marks pending reference-map labels as `found` or
`not_staged`.

Primary command:

```powershell
python tools\aas_inventory\inventory_aas_assets.py --fail-on-missing-required-manifest
```

The default JSON report is written to:

```text
.tmp/aas_inventory/asset-inventory.json
```

Use `--fail-on-needs-conversion` when the desired gate is "every discovered BSP
must already have an AAS." That gate is intentionally separate from manifest
presence so developers can inventory newly staged BSPs before deciding whether
to fail CI.

## Current Inventory Result

The first local report found one map asset set:

- `mm-rage`: `ready`, with one BSP and one AAS.

No BSP-backed map currently needs conversion in the default scanned roots. The
pending reference-map labels from the manifest remain not staged, so the next
conversion-prep step is to stage real BSP inputs for the reference set and rerun
the inventory before expanding `tools/q2aas/validation_manifest.json`.

## Validation

Commands run:

```powershell
$env:PYTHONDONTWRITEBYTECODE='1'; python -B -m unittest tools.aas_inventory.test_inventory_aas_assets
$env:PYTHONDONTWRITEBYTECODE='1'; python -B tools\aas_inventory\inventory_aas_assets.py --fail-on-missing-required-manifest --fail-on-needs-conversion
```

Results:

- Unit tests passed: `Ran 3 tests ... OK`.
- Inventory exited `0` with `maps=1`, `ready=1`, `needs_conversion=0`,
  `manifest_required=1`, no missing required manifest maps, and all default
  reference source paths present.
- The report was written to `.tmp/aas_inventory/asset-inventory.json`.
