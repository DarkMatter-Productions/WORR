# Q3A BotLib and Q2 AAS Source Audit (2026-06-16)

Task IDs: `FR-04-T10`, `DV-07-T06`

Related docs:

- `docs-dev/plans/q3a-botlib-aas-port.md`
- `docs-dev/q3a-botlib-aas-credits.md`
- `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`

## Summary

This first implementation round makes the bot/AAS port import-safe. It pins the external BSPC baselines, records the local Quake III Arena snapshot limitation, starts concrete contributor attribution, and defines a hard source-import gate before any Q3A/BSPC code lands in WORR.

At the time of this audit pass, no Q3A or BSPC source files were imported into WORR. The follow-up `FR-04-T11` bootstrap imported the pinned `TTimo/bspc` snapshot into `tools/q2aas/` and is documented in `docs-dev/q2aas-generator-vendor-bootstrap-2026-06-16.md` and `docs-dev/q3a-botlib-aas-credits.md`. The audit workspace remains scratch-only under `.tmp/source-audit/`.

## Import Gate

Before source code import begins:

- Do not import directly from `E:\_SOURCE\_CODE\Quake-III-Arena-master` unless the file is matched to a commit-pinned upstream source or a local snapshot manifest is approved.
- Prefer commit-pinned upstream repos for copied code.
- Preserve original headers in direct imports.
- Add a `Modified for WORR` note when a copied file diverges.
- Add ledger rows in `docs-dev/q3a-botlib-aas-credits.md` in the same change set as the import.
- Keep Q3A/BSPC-derived code quarantined behind planned adapter/tool boundaries.

## Baseline Sources

| Source | Role | Ref / Hash | Audit Status |
|---|---|---|---|
| WORR | Target project | Local repository, GPL-2.0 license file present | Compatible GPL-family target; retain existing WORR/ZeniMax notices. |
| Local Q3A tree | Reference snapshot for BotLib, bot behavior, server bot glue, and original BSPC | `E:\_SOURCE\_CODE\Quake-III-Arena-master`; not a Git checkout | Reference only until matched to a pinned upstream commit or accepted snapshot manifest. |
| id Software Q3A public mirror | Candidate pinned Q3A import source | `https://github.com/id-Software/Quake-III-Arena` HEAD `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Use for commit-pinned comparison/import if it matches the local files needed. |
| `TTimo/bspc` | Required Q2 AAS generator baseline | `https://github.com/TTimo/bspc` HEAD `10d23c5ebd042ddc5d03e17de0f560f5076649dc` | Full history cloned to `.tmp/source-audit/TTimo-bspc` for contributor audit. |
| `bnoordhuis/bspc` | Fork lineage for `TTimo/bspc` | `https://github.com/bnoordhuis/bspc` HEAD `6c11357e6d79a89e88cda2fe0e67c99a8923e116` | Full history cloned to `.tmp/source-audit/bnoordhuis-bspc` for lineage audit. |

## License Review

- WORR ships a GPL-2.0 license file at repository root.
- The local Q3A source has `COPYING.txt` with GPL-2.0 text and relevant bot/BSPC source headers that state GPL version 2 or later.
- The local Q3A `README.txt` lists non-GPL exceptions for some unrelated source areas, but the bot/BSPC files inspected in this pass use the standard Q3A GPL headers.
- `TTimo/bspc` includes GPL-2.0 license text and its README says the program is licensed under GPL v2.0 and any later version.
- This is not legal advice; the engineering conclusion for this first pass is that the planned imports are GPL-family compatible if headers, license text, and provenance are retained.

## Local Q3A Reference File Hashes

The local Q3A tree is not a Git checkout. These hashes identify the reference files inspected in this pass.

| Local Path | Size | SHA-256 |
|---|---:|---|
| `code/botlib/be_interface.c` | 29195 | `E225FF396CF8992DBA318D27DF0A5E5414F9B93087D0DB5066F71BEEFECA40E9` |
| `code/botlib/be_aas_main.c` | 13538 | `CE3E5ECBA4742E0853C79EDD685B3D5317AEB9F9B836D437C4D2DFA872C81C0B` |
| `code/botlib/be_aas_file.c` | 24506 | `7306CF38153EDE8C7608B1D07FD174901A69639C5FF1E47CC11C2E616B08A9A5` |
| `code/botlib/be_aas_reach.c` | 152877 | `B5622A0E7C6D6DFBF8A82078F4629A3B2885D087EBA434A47DEDB83908C71548` |
| `code/botlib/be_ai_goal.c` | 53192 | `C51BF53DD63E8C4204E19573D455B761C698C10DF4F5B87269AE89C3C333908B` |
| `code/botlib/be_ai_move.c` | 113070 | `1E97578259E1A00B815252DD73B1625024DF0B062D0BAE0A89246A1D0F7B24E1` |
| `code/game/g_bot.c` | 22608 | `0074E666597B5E2F99D9C373317122F4044D5CD2B1918DAE25C699FCEC7681F4` |
| `code/game/ai_main.c` | 45513 | `E969A606253B2D0AA69E9DC1C46981EF3A2ED352140EE3CE637CF456FC23497E` |
| `code/game/ai_dmq3.c` | 158046 | `6452F29F14B9042A8E921166149EDE046D24E36734A74925D68CF46DD1A1FB46` |
| `code/game/ai_team.c` | 66734 | `6C72BA41C01F181AE4F7FFE29BE4C2962D4CEE12327F01F883DC1F8FE9384165` |
| `code/server/sv_bot.c` | 16211 | `D239FCEBC0A48EA989B188DEA6B61CF486F20A3BF4EFD9FB05DDE2100ED0503C` |
| `code/bspc/bspc.c` | 28389 | `985DF73E6F5E46FDEC58972ADAC96EE2525CBF7298963186A86DAD82E39A8BA8` |
| `code/bspc/map_q2.c` | 30781 | `53637F4C28070C218D80E54DFA8CAA2AC999B7A44DCB26CF6C027B8A209CE9F3` |
| `code/bspc/l_bsp_q2.c` | 30124 | `E0BD90956020E795DD1CC8A8E254F124AEE5412257B418C590387BC10E7960CB` |
| `code/bspc/q2files.h` | 11637 | `5016849C434F1A960CF4FFF1216A37C98BDD6C24402D9672F326FB84469CD8F4` |

## `TTimo/bspc` Q2 Generator File Hashes

These are the current pinned `TTimo/bspc` files most relevant to `FR-04-T11`.

| Upstream Path | SHA-256 at `10d23c5ebd042ddc5d03e17de0f560f5076649dc` |
|---|---|
| `README.md` | `0E5B64626E20B2363C3CE69A9C13261EAFF4313A35DA19428BDCF39FE520063B` |
| `LICENSE` | `189B1AF95D661151E054CEA10C91B3D754E4DE4D3FECFB074C1FB29476F7167B` |
| `bspc.c` | `6956BFE09042E544E9010D86137B9EF65DACB66648994959C5633C8D7A880FA8` |
| `map_q2.c` | `ABB3B3B5219BCA7DCD67BD590788963257D8D58B8AC76BC543617683F0F52C73` |
| `l_bsp_q2.c` | `72247C80C4F3683EA5A743DE239D97E6C3C6A7739AC7BAD2A3490F84B8F8C439` |
| `q2files.h` | `4F4DE688294D3932F3D0C7E0C9EBEE1F277FFCC5734E300AD2BA25F0BABAA67E` |

## Contributor Baseline

`TTimo/bspc` contributors found through full repository history:

- Ben Noordhuis `<info@bnoordhuis.nl>`
- Chris Brooke `<chris.brooke@justfixit.co.uk>`
- Joel Baxter `<joel.baxter@neogeographica.com>`
- Thomas Köppe `<tkoeppe@google.com>`
- Timothee "TTimo" Besset `<ttimo@ttimo.net>`
- Victor Luchits `<vluchits@gmail.com>`

`bnoordhuis/bspc` contributors found through full repository history:

- Ben Noordhuis `<info@bnoordhuis.nl>`
- Chris Brooke `<chris.brooke@justfixit.co.uk>`

File-specific contributors for the first Q2 AAS generator candidates:

| File | Contributors Seen in `TTimo/bspc` History |
|---|---|
| `bspc.c` | Ben Noordhuis, Joel Baxter, Thomas Köppe, Timothee "TTimo" Besset |
| `map_q2.c` | Ben Noordhuis |
| `l_bsp_q2.c` | Ben Noordhuis, Thomas Köppe |
| `q2files.h` | Ben Noordhuis |

Note: id Software is retained as the original source credit from file headers. The contributor list above is for the public Git history of the BSPC fork lineage, not a replacement for original file copyright.

## Architecture Differences Captured

The plan now explicitly tracks these Q3A-to-WORR differences before code import:

- VM trap imports versus native WORR `sgame` imports.
- Q3 server fake-client allocation versus WORR client session services.
- Q3 movement, item, weapon, and powerup assumptions versus Q2/Q2R gameplay.
- Q3 BSP/AAS defaults versus Q2 IBSP38/Q2R map handling.
- Q3 `baseq3`/`pk3` paths versus WORR `basew`/`.install`/`pak0.pkz`.
- Q3 debug primitives versus WORR debug draw imports.
- Protocol sensitivity: bot control should remain server-side and avoid q2proto changes unless separately justified.

## First Import Result

The first code import is the generator work for `FR-04-T11`, not the runtime BotLib. That keeps the first code step isolated to `tools/q2aas/`, proves that Q2 maps can produce usable `.aas`, and avoids coupling BotLib runtime work to unvalidated nav data.

Executed bootstrap slice:

- Created `tools/q2aas/`.
- Vendored the pinned `TTimo/bspc` baseline at `10d23c5ebd042ddc5d03e17de0f560f5076649dc`.
- Preserved GPL headers and included upstream `LICENSE`.
- Built only a standalone generator target at first.
- Keep generated scratch files under `.tmp/q2aas/`.
- Do not stage AAS assets under `.install/` until validation rules exist.

## Remaining Phase 0 Work

- Match any Q3A BotLib runtime files selected for import against the public id Software mirror or another commit-pinned source.
- Add exact imported-file ledger rows before copying new files into `src/` or `tools/`; the `tools/q2aas/` BSPC snapshot is now recorded in the ledger.
- Decide whether future imported code should be vendored as a subtree/snapshot or copied into WORR-owned directories with source notes; `tools/q2aas/` uses a documented copied vendor snapshot.
- Decide where license text will be included in source and packaged artifacts once imported code ships.
