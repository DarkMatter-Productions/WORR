# Q3A BotLib and Q2 AAS Port Credits Ledger

Date: 2026-06-16

Related plan: `docs-dev/plans/q3a-botlib-aas-port.md`

Related tasks: `FR-04-T10`, `FR-04-T11`, `FR-04-T12`, `FR-04-T15`, `FR-04-T16`, `DV-07-T06`

## Purpose

Track source provenance, credits, licenses, and local modification notes for the Quake III Arena BotLib and Quake II AAS generator work. This ledger must be updated in the same change set as any imported, adapted, or substantially referenced upstream file.

## Initial Credit Sources

| Source | Role in Project | URL / Local Path | License / Notice | Credit Requirement |
|---|---|---|---|---|
| Quake III Arena source code | BotLib runtime, game bot behavior, server bot glue, original BSPC lineage | `E:\_SOURCE\_CODE\Quake-III-Arena-master` | GPL family headers in source files; retain original id Software notices. | Credit id Software and file-level authors/contributors where discoverable from headers/history. |
| `TTimo/bspc` | Required baseline for the WORR Quake II BSP-to-AAS generator | `https://github.com/TTimo/bspc` | README/license identify GPL-2.0-or-later. | Credit `TTimo/bspc`, retain license text, and record exact imported commit. |
| `bnoordhuis/bspc` | Fork lineage for `TTimo/bspc` | `https://github.com/bnoordhuis/bspc` | Verify during source audit before import. | Credit as upstream fork lineage and record exact source relationship. |
| WORR existing bot scaffolding | Local integration surface and existing helpers | `src/game/sgame/bots/*`, `src/game/sgame/client/client_session_service_impl.cpp`, `src/game/sgame/player/p_view.cpp` | Existing WORR/ZeniMax notices in files. | Preserve existing local notices and document WORR-native changes separately from upstream imports. |

## Imported File Ledger

No Q3A/BSPC source files have been imported as part of this planning change. Add rows here before or alongside the first import.

| WORR Path | Upstream Path / URL | Upstream Commit | Use Type | License | Copyright / Header | Contributors | Local Changes | Verification |
|---|---|---|---|---|---|---|---|---|
| TBD | TBD | TBD | Direct import / derivative / concept reference / native implementation | TBD | TBD | TBD | TBD | TBD |

## Contributor Discovery Checklist

For each imported upstream file or copied algorithm:

- [ ] Capture the upstream commit hash.
- [ ] Preserve the file's original copyright/license header.
- [ ] Run source history review where available, for example `git log --follow -- <path>`.
- [ ] Add distinct upstream contributors to the `Contributors` field when they can be identified from file history or headers.
- [ ] Add `Modified for WORR` notes when the local file diverges from upstream.
- [ ] Record whether the work is a direct import, derivative, concept reference, or clean WORR-native implementation.
- [ ] Verify release packaging includes required license/credit material if the imported code ships in binaries or source archives.

## Notes

- Do not use this ledger as legal advice. It is a project hygiene artifact that keeps engineering, review, and release work honest.
- If a file is only used as inspiration, record that as a concept reference rather than implying copied source.
- If future work consults additional projects such as Quake3e, baseq3a, or ioquake3, add them here before their code or algorithms influence implementation.

