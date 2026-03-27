# WORR SWOT and Task-Based Feature + Development Roadmaps

Date: 2026-02-27

## Purpose
Create a repository-grounded SWOT and convert it into actionable, task-based project roadmaps that can guide coordinated team execution.

## Status Updates
- `DV-02-T02` In Progress:
  - Nightly Linux/macOS jobs now install explicit Vulkan toolchain dependencies so renderer artifacts do not depend on hosted-image luck.
  - Windows MSYS2 nightlies/releases now install Vulkan headers/loader plus `glslangValidator` so Vulkan/RTX renderer artifacts build in CI.
  - The macOS Intel target now uses the supported `macos-15-intel` runner label instead of the retired `macos-13` label.
  - Linux nightly dependency names were updated for current Ubuntu runner images (`libdecoration0-dev`, no `libsdl3-dev` requirement under Meson fallback).
  - Implementation log: `docs-dev/macos-nightly-vulkan-support-2026-03-16.md`.
  - Recovered run `23146195642` by fixing MSYS2/C++20 client compatibility issues (`min`/`max` typing, non-debug `developer` usage), the RTX debug symbol linkage mismatch, and the MinGW Unicode updater entry-point path.
  - Implementation log: `docs-dev/msys2-nightly-build-recovery-2026-03-16.md`.
  - Recovered run `23148759868` by fixing three cross-platform CI regressions:
    - renamed repository version metadata from `VERSION` to `WORR_VERSION` so macOS case-insensitive filesystems no longer shadow libc++ `<version>`
    - replaced non-portable `std::sinf` usage in `sgame` with standard `std::sin` overloads so GCC/Linux builds complete
    - fixed WiX MSI generation by removing self-referential preprocessor defines, adding the required Product language, and hard-failing on `heat`/`candle`/`light` native command errors
  - Implementation log: `docs-dev/nightly-ci-cross-platform-recovery-2026-03-16.md`.
  - Recovered run `23152001787` by fixing three follow-up CI regressions:
    - changed the WiX PowerShell wrapper to pass explicit native argument arrays so switches like `-out` are not consumed as PowerShell parameters
    - restored the Linux nightly OpenGL header dependency with `libgl1-mesa-dev`
    - replaced `std::from_chars(float)` with portable strict `std::strtof` parsing in `sgame` command and sky-rotation paths for macOS libc++
  - Implementation log: `docs-dev/nightly-run-23152001787-recovery-2026-03-16.md`.
  - Recovered run `23153597827` by fixing another three-platform nightly regression set:
    - emitted explicit x64 WiX harvest/compile metadata so Windows MSI validation no longer trips `ICE80`
    - updated RTX debug line-rasterization setup to the current Vulkan `EXT` symbols used by hosted Linux headers
    - made `sgame` save metadata/serialization handle `size_t` cleanly with JsonCpp-facing explicit widths for macOS builds
  - Implementation log: `docs-dev/nightly-run-23153597827-error-warning-recovery-2026-03-16.md`.
  - Recovered run `23156641291` by fixing the Unix release-pack staging collision with the root `worr` executable and restoring GCC-compatible `q_unused` placement in `rend_gl`.
  - Implementation log: `docs-dev/nightly-run-23156641291-recovery-2026-03-16.md`.
  - Recovered run `23159773990` / job `67284227517` by converting the remaining RTX line-rasterization negotiation path in `src/rend_rtx/vkpt/main.c` from `KHR` to `EXT`, matching the Vulkan headers used on current Linux CI runners.
  - Implementation log: `docs-dev/nightly-run-23159773990-recovery-2026-03-16.md`.
  - Recovered run `23162552895` / job `67293973244` by fixing GCC/Linux-incompatible temporary `VEC2`/`VEC3` client callsites, keeping the C-only `-Wmissing-prototypes` warning path out of C++ engine targets, and hardening Linux validation-path issues in BSP patched-PVS path construction, zone stats printing, MVD multicast leaf handling, and OpenAL loader loop indexing.
  - Implementation log: `docs-dev/nightly-run-23162552895-linux-safety-recovery-2026-03-16.md`.
- `DV-04-T03` In Progress:
  - Recovered run `23153597827` by fixing three additional cross-platform failures:
    - emitted explicit x64 WiX harvest/compile metadata so Windows MSI validation no longer trips `ICE80`
    - updated RTX debug line-rasterization setup to the current Vulkan `EXT` symbols used by hosted Linux headers
    - made `sgame` save metadata/serialization handle `size_t` cleanly with JsonCpp-facing explicit widths for macOS builds
  - Removed first-party warning categories surfaced by the same run across `client`, `cgame`, `sgame`, `rend_gl`, and `rend_vk`, and reduced fallback dependency warning noise by quieting forced-fallback third-party builds and disabling HarfBuzz subproject tests in CI configure steps.
  - Aligned project-wide linker arguments for both C and C++ Meson targets so local Windows validation of the client executable uses the same linker configuration as the other binaries.
  - Implementation log: `docs-dev/nightly-run-23153597827-error-warning-recovery-2026-03-16.md`.
  - Recovered additional warning noise from run `23156641291` by removing `entity_iterable_t` constructor template-id syntax in `sgame` and extending quiet fallback warning suppression for third-party fallback builds.
  - Implementation log: `docs-dev/nightly-run-23156641291-recovery-2026-03-16.md`.
- `FR-03-T08` In Progress:
  - Tightened multiplayer menu routing so the match menu is only selected during an active multiplayer game session, instead of any `cl.maxclients > 1` state.
  - Split the session-only menu definitions (`dm_welcome`, `dm_join`, `join`, `dm_hostinfo`, `dm_matchinfo`) out of `src/game/cgame/ui/worr.json` into a dedicated embedded `src/game/cgame/ui/worr-multiplayer.json` asset loaded by cgame UI init.
  - Added a cgame-side session helper exposed through the UI access boundary so the menu module no longer depends on broad engine-state assumptions for multiplayer routing.
  - Implementation log: `docs-dev/match-menu-session-split-2026-03-23.md`.
  - Converted the multiplayer `MyMap` entry into a dedicated submenu flow with explicit availability/status messaging, preserved flag state across navigation, and successful queue cleanup/close behavior.
  - Implementation log: `docs-dev/match-menu-mymap-submenu-2026-03-23.md`.
- `FR-02-T07` Done:
  - SDL video backend now creates Vulkan-capable windows for `r_renderer vulkan`/`rtx` instead of always forcing an OpenGL context.
  - Native Vulkan renderer now uses SDL Vulkan instance/surface helpers and enables portability enumeration/subset support required by MoltenVK-backed macOS devices.
  - Implementation log: `docs-dev/macos-nightly-vulkan-support-2026-03-16.md`.
- `FR-02-T03` Done:
  - User-facing launch/debug presets stay on `worr_x86_64(.exe)` and `worr_ded_x86_64(.exe)`, which are now bootstrap hosts that load `worr_engine_*` and `worr_ded_engine_*` in-process.
  - The short-lived explicit `worr_launcher_*` split was removed so the published binary names match the actual user launch path again.
  - Implementation logs: `docs-dev/bootstrap-hosted-engine-libraries-2026-03-24.md`, `docs-dev/vscode-bootstrap-debug-presets-2026-03-24.md`.
- `DV-08-T05` Done:
  - Nightly/stable staging now packages the canonical repo `assets/` tree directly into `.install/basew/pak0.pkz`.
  - Loose staged asset duplication between `assets/` and `.install/` was removed so runtime assets now have a single authored source and a single packaged staging form.
  - Client/server release archives now use explicit payload filters instead of packaging identical full `.install/` trees.
  - Artifact verification now validates manifest contents so role-specific payload regressions are caught before publish.
  - Stable release packaging now reuses the same platform-packaging path as nightlies.
  - Implementation log: `docs-dev/basew-gamedir-and-arch-runtime-layout-2026-03-16.md`.
- `DV-08-T06` Done:
  - Local staging, release archives, and the Windows MSI now use a single `basew/` gamedir instead of the earlier `baseq2/` + `worr/` split.
  - Release CI now builds with `-Dbase-game=basew -Ddefault-game=basew`, and gameplay/runtime defaults now resolve the WORR payload through `basew`.
  - `WORR_VERSION` remains `0.1.0`, and the stable release workflow still publishes cross-platform GitHub releases from that semver source of truth.
  - Implementation log: `docs-dev/basew-gamedir-and-arch-runtime-layout-2026-03-16.md`.
- `DV-08-T07` Done:
  - User-facing bootstrap executables now ship with explicit arch suffixes such as `worr_x86_64(.exe)`, `worr_ded_x86_64(.exe)`, and `worr_updater_x86_64.exe`.
  - Hosted engine libraries now follow the same arch-suffixed rule (`worr_engine_x86_64`, `worr_ded_engine_x86_64`), while game DLLs continue using `cgame_x86_64` and `sgame_x86_64`.
  - Release manifests and updater metadata now publish the launcher/engine pairing through `launch_exe` and `engine_library`.
  - Implementation logs: `docs-dev/basew-gamedir-and-arch-runtime-layout-2026-03-16.md`, `docs-dev/bootstrap-hosted-engine-libraries-2026-03-24.md`.
- `DV-08-T08` Done:
  - Nightly GitHub releases now publish as standard releases instead of GitHub prereleases.
  - Updater release discovery now filters the full GitHub release list by channel/tag and `allow_prerelease` policy, so stable installs do not drift onto nightly releases after that publishing change.
  - Nightly packaging no longer emits updater configs with `allow_prerelease=true`.
  - Implementation log: `docs-dev/nightly-release-non-prerelease-channel-selection-2026-03-16.md`.
- `FR-08-T04` Done:
  - Release metadata now publishes an explicit role-level updater contract through the release index instead of inferring package names inside the updater.
  - `worr_update.json` now points at a channel-specific release index asset, and updater discovery resolves role payloads from that index before fetching the remote manifest.
  - Implementation log: `docs-dev/desktop-bootstrap-updater-2026-03-23.md`.
- `DV-03-T06` Done:
  - Added release-tool SemVer ordering tests covering stable, prerelease, and nightly version strings.
  - Added release-index parser tests for missing role metadata and malformed payload fields.
  - Implementation log: `docs-dev/desktop-bootstrap-updater-2026-03-23.md`.
- `DV-08-T01` Done:
  - Added release-index parser fixtures for missing role payloads and missing required updater metadata.
  - Added target-contract tests that keep full-install updater payload coverage separate from split manual archive coverage.
  - Implementation log: `docs-dev/desktop-bootstrap-updater-2026-03-23.md`.
- `DV-08-T03` In Progress:
  - The new bootstrap apply worker now verifies staged file hashes, writes the local install manifest last, and restores backed-up files on failure.
  - Local build/package validation passed, but explicit live fault-injection coverage for failed extraction/apply paths is still pending.
  - Implementation log: `docs-dev/desktop-bootstrap-updater-2026-03-23.md`.
- `DV-08-T09` Done:
  - Desktop updater startup remains bootstrap-based on Windows, Linux, and macOS, with the client using a branded splash and the dedicated server using a console-first updater flow.
  - Normal startup now stays inside the user-facing bootstrap executables, which host the engine shared libraries in-process; the temp updater worker is reserved for approved file-replacement/update paths and relaunches the public bootstrap after a successful update.
  - Update prompts can now be deferred without forcing an exit, and `autolaunch` is respected after worker-applied installs.
  - Implementation logs: `docs-dev/desktop-bootstrap-updater-2026-03-23.md`, `docs-dev/bootstrap-hosted-engine-libraries-2026-03-24.md`.
- `DV-08-T11` Done:
  - Re-audited the updater pipeline against packaged artifacts, role-scoped staged installs, and local pending-update payloads.
  - Kept install-root normalization and real apply-time permission handling hardening in the bootstrap worker, added trace instrumentation plus a native Windows dedicated-server temp-worker/relaunch handoff path, closed the local Windows public-bootstrap approved-update gap, and added `tools/release/server_bootstrap_update_smoke.py` for deterministic local validation.
  - Implementation log: `docs-dev/updater-pipeline-audit-2026-03-25.md`.
- `DV-08-T12` In Progress:
  - Analyzed the local Steam-distributed Quake Champions client install and confirmed that Steam owns build/depot updates while the game behaves like a branded session shell with embedded web/session components.
  - Refactored the desktop bootstrapper toward a session-shell model by introducing explicit install-sync planning, enabling same-version repair/synchronization decisions, and constraining removals to managed files tracked by the local install manifest.
  - The worker/apply path can now complete metadata-only syncs without downloading a package, and local validation repaired a deliberately missing dedicated engine DLL through the public bootstrap update flow.
  - Added a Windows client shared-window handoff slice so the hosted engine can adopt the bootstrap-owned splash window instead of creating a second native client window on that path.
  - Added an in-process client sync/apply path so approved client synchronization can stay in the launcher, restore managed files, and enter the hosted engine in the same bootstrap-owned window when the running bootstrap executable itself is not being replaced.
  - Replaced the old fixed-size placeholder splash with a display-profile-driven SDL3 session shell that resolves `r_geometry`, `r_fullscreen`, `r_fullscreen_exclusive`, `r_monitor_mode`, `r_display`, `autoexec.cfg`, and forwarded `+set` overrides before creating the client window.
  - The bootstrap now creates the client shell through SDL3's hidden property-based window path, applies the real fullscreen/window mode before showing it, and logs the resolved session-shell mode for validation.
  - Extended `tools/release/client_bootstrap_sync_smoke.py` so local validation can assert both the in-process repair/sync handoff and the expected bootstrap session-shell window mode.
  - Implementation logs: `docs-dev/client-bootstrap-session-shell-architecture-2026-03-25.md`, `docs-dev/bootstrap-session-shell-sync-refactor-2026-03-25.md`, `docs-dev/bootstrap-windows-client-shared-window-adoption-2026-03-25.md`, `docs-dev/bootstrap-client-in-process-sync-handoff-2026-03-25.md`, `docs-dev/bootstrap-session-shell-display-profile-window-creation-2026-03-25.md`.
- `DV-08-T10` Done:
  - Repaired the tracked vendored libcurl wrap patch so bootstrap-enabled local Windows builds now succeed against the `curl-8.18.0` fallback instead of failing on stale 8.15-era source paths.
  - Fixed the bootstrap updater's Windows `min`/`max` macro collision so the launcher/worker binaries compile cleanly with the vendored fallback enabled.
  - Implementation log: `docs-dev/libcurl-wrap-bootstrap-build-fix-2026-03-23.md`.
- `FR-01-T04` In Progress:
  - Completed Vulkan MD5 parity follow-up for frame resolve semantics and MD2/MD5 opaque-vs-alpha routing in `src/rend_vk/vk_entity.c`.
  - Implementation log: `docs-dev/vulkan-md5-mesh-frame-alpha-parity-fix-2026-02-27.md`.
  - Completed Vulkan MD5 `.md5scale` + scale-position parity and MD5 skin-routing correction in `src/rend_vk/vk_entity.c`.
  - Implementation log: `docs-dev/vulkan-md5-mesh-parity-revision-2026-02-27.md`.
  - Fixed Vulkan sky re-registration crash on same-map reload (`VK_World_SetSky` handle invalidation ordering) in `src/rend_vk/vk_world.c`.
  - Implementation log: `docs-dev/vulkan-sky-reregister-crash-fix-2026-02-27.md`.
  - Aligned Vulkan MD2 mesh decode topology handling with RTX/GL remap behavior in `src/rend_vk/vk_entity.c`:
    - Skip invalid triangles instead of hard-failing the whole model.
    - Remap/deduplicate MD2 vertices by `(index_xyz, st.s, st.t)` and emit compact index/vertex streams.
    - Added MD2 header/frame-size/skin-dimension bounds checks matching RTX-style validation.
  - Implementation log: `docs-dev/vulkan-md2-mesh-remap-parity-fix-2026-02-27.md`.
- `FR-06-T01` In Progress:
  - Fixed OpenAL loop-merge channel reuse so merged loops cannot reuse `no_merge` Doppler channels in `src/client/sound/al.cpp`.
  - This preserves projectile world-origin tracking for Doppler-marked loop sounds when mixed with non-Doppler loops using the same sample.
  - Implementation log: `docs-dev/audio-projectile-doppler-origin-merge-fix-2026-02-27.md`.
  - Fixed q2proto entity delta application so loop extension fields are applied in `src/client/parse.cpp`:
    - `Q2P_ESD_LOOP_VOLUME`
    - `Q2P_ESD_LOOP_ATTENUATION`
  - This prevents stale `loop_attenuation` states (for example `ATTN_LOOP_NONE`) from producing level-wide/full-volume projectile loops after entity reuse.
  - Implementation log: `docs-dev/audio-projectile-loop-attenuation-parse-fix-2026-02-27.md`.
  - Fixed OpenAL EAX spatial routing consistency in `src/client/sound/al.cpp`:
    - Non-merged channels now run the same per-source spatial effect update path used by merged loops (direct filter, air absorption, and auxiliary send updates).
    - EAX zone selection now uses uncapped nearest-zone matching (`FLT_MAX` + squared-distance checks), eliminating the hard 8192-unit selection ceiling and reducing unnecessary LOS traces.
    - EAX effect application now clears stale AL error state before property updates for reliable success/failure reporting.
  - Implementation log: `docs-dev/audio-eax-spatial-awareness-fixes-2026-02-27.md`.
  - Closed remaining EAX spatial-awareness gaps:
    - Restored real occlusion behavior in `src/client/sound/main.cpp` (`S_ComputeOcclusion`, `S_GetOcclusion`, `S_SmoothOcclusion`, `S_MapOcclusion`) with multi-ray tracing, material/transparency weighting, query rate limiting, and smoothing.
    - Replaced center-only LOS zone validation with multi-probe reachability checks in `src/client/sound/al.cpp` to reduce false zone misses in occluded/non-convex spaces.
  - Implementation log: `docs-dev/audio-eax-spatial-gap-closure-2026-02-27.md`.
  - Fixed excessive projectile loop attenuation fallback in shared client sound:
    - Added `S_GetEntityLoopDistMult(const entity_state_t *ent)` in `src/client/sound/main.cpp`.
    - For projectile-like/doppler loops with unset `loop_attenuation` (`== 0`), fallback now uses `ATTN_NORM` before conversion to distance multiplier.
    - This applies to both OpenAL and DMA loop paths through the shared `S_GetEntityLoopDistMult(...)` call sites.
  - Implementation log: `docs-dev/audio-projectile-loop-attenuation-fallback-fix-2026-02-27.md`.
  - Stabilized dense OpenAL Doppler loop mixes in `src/client/sound/al.cpp` for Issue #761:
    - Doppler-preserved same-sample loop groups now apply `1 / sqrt(count)` gain normalization instead of stacking full gain linearly.
    - Unmerged projectile/autosound loops now use a stable per-entity phase offset so identical loop samples do not all start in lockstep.
    - This reduces crackle/noise when many projectile loop emitters are active simultaneously while preserving per-entity Doppler spatialization.
  - Implementation log: `docs-dev/audio-eax-loop-doppler-mix-stability-2026-03-22.md`.

## Baseline Snapshot (Repository-Derived)
- Codebase scale is substantial: approximately 733 `*.c`/`*.cpp`/`*.h`/`*.hpp` files and approximately 426k lines across `src/` and `inc/`.
- Workload concentration is heavily in gameplay and rendering:
  - `src/game/sgame`: 259 files, approximately 128k lines
  - `src/rend_gl`: 39 files, approximately 49k lines
  - `src/client`: 48 files, approximately 41k lines
  - `src/rend_rtx`: 65 files, approximately 38k lines
  - `src/rend_vk`: 15 files, approximately 26k lines
- cgame UI is already a large system:
  - `src/game/cgame/ui/worr.json`: 1869 lines
  - `src/game/cgame/ui/ui_widgets.cpp`: 114k+ bytes
- Build and release automation is mature:
  - Local staging standard: `.install/` via `tools/refresh_install.py`
  - Automated release paths: `.github/workflows/nightly.yml`, `.github/workflows/release.yml`
  - Platform packaging and metadata tooling under `tools/release/`
- Protocol compatibility has dedicated boundaries and tests:
  - `q2proto/` integrated as read-only in policy
  - `q2proto/tests/` contains protocol flavor build tests
- Existing technical debt is visible:
  - 200+ `TODO/FIXME/HACK/XXX` markers in first-party `src/` and `inc/` (excluding legacy and third-party trees)
  - Bots are structurally present but not functionally complete (`src/game/sgame/bots/bot_think.cpp` frame hooks are empty)
  - Local TODOs still include major items (`TODO.md`)
- Documentation volume is high and active:
  - `docs-dev/` carries extensive subsystem writeups (renderer, cgame, font, build, release)
  - Some docs show drift against current code paths, signaling curation needs

## SWOT

## Strengths
- Broad gameplay foundation already exists in `sgame`, with many game modes and systems wired (`src/game/sgame/g_local.hpp`, `src/game/sgame/gameplay/*`, `src/game/sgame/match/*`).
- Strong multi-renderer strategy is already implemented: OpenGL (`src/rend_gl`), native Vulkan raster (`src/rend_vk`), and RTX/vkpt (`src/rend_rtx`).
- Release and staging discipline is materially stronger than typical forks (`tools/refresh_install.py`, `tools/release/*`, nightly workflow automation).
- cgame and JSON UI architecture is robust and extensible (`src/game/cgame/*`, `src/game/cgame/ui/*`).
- Documentation culture is real and ongoing (`docs-dev/` has high-frequency change logs and design analyses).
- Clear policy boundaries already exist for critical risks:
  - no Vulkan-to-OpenGL fallback policy
  - q2proto compatibility guardrails

## Weaknesses
- Scope breadth is high enough to fragment focus without enforced project governance.
- Automated quality gates are underdeveloped for core engine/gameplay paths (CI is release-centric, not full PR validation-centric).
- Vulkan parity is still incomplete in multiple high-visibility areas (particle styles, beam styles, flare behavior, post-process parity).
- Bot behavior is not yet production-capable despite plumbing being present.
- Technical debt markers are spread across gameplay, client, renderer, and server paths.
- Cvar namespace modernization is only partially applied (`g_` still dominates many new sgame controls despite `sg_` preference).
- Dependency lifecycle complexity is high (multiple vendored versions of several libraries under `subprojects/`).
- Documentation freshness is uneven (some architecture docs lag current filenames/wiring).

## Opportunities
- Differentiate WORR through native Vulkan parity plus predictable performance improvements.
- Leverage already-rich game mode set into a clear competitive and cooperative offering roadmap.
- Exploit nightly + updater tooling to move toward rapid, measurable, low-friction iteration.
- Convert documentation volume into execution strength by binding work to project IDs and status workflows.
- Add targeted automated tests and smoke harnesses to reduce regression risk as refactors continue.
- Complete C++ migration with module boundaries that reduce long-term maintenance cost.
- Consolidate dependency versions and improve reproducibility/security posture.
- Use analytics/observability for performance baselines and release quality gates.

## Threats
- Regression risk is high due to surface area and currently limited automated gameplay/renderer test coverage.
- Cross-platform support can drift if changes are validated mainly on one host/toolchain.
- Feature creep can outrun finishing work unless work-in-progress limits are enforced.
- Upstream divergence from Q2REPRO, KEX, and reference idTech3 patterns can increase integration cost over time.
- Team coordination cost will rise with parallel renderer/gameplay/UI tracks unless roadmap ownership is explicit.
- Dependency sprawl increases update effort and potential security exposure windows.
- Documentation debt can lead to incorrect decisions when implementation and docs disagree.
- Public expectations around compatibility and re-release parity can be missed without milestone-level acceptance criteria.

## Project Backbone Model (Mandatory Operating Approach)

## Portfolio Structure
- Portfolio: `WORR 2026 Execution Portfolio`
- Projects:
  - `P-FEATURE`: player/admin-visible outcomes
  - `P-DEVELOPMENT`: engineering quality, architecture, and delivery capability

## Task Metadata Schema
Each task must include:
- `ID`: stable identifier (`FR-xx-Tyy` or `DV-xx-Tyy`)
- `Epic`: roadmap epic ID
- `Area`: subsystem (`rend_vk`, `cgame`, `sgame`, `tools/release`, etc.)
- `Priority`: `P0`, `P1`, `P2`
- `Dependencies`: IDs that must be completed first
- `Definition of Done`: explicit acceptance criteria

## Workflow States
- `Backlog`
- `Ready`
- `In Progress`
- `In Review`
- `Blocked`
- `Done`

## Cadence
- Weekly: backlog grooming and dependency resolution
- Biweekly: milestone review against exit criteria
- Per release train: roadmap delta review and reprioritization

## Definition of Ready
- Scope and subsystem boundaries are explicit.
- Dependencies are known and linked.
- Validation strategy is defined (build/test/runtime checks).

## Definition of Done
- Code merged and documented.
- Staging/packaging impact validated if applicable.
- Corresponding roadmap task marked complete.

## Feature Roadmap (Task-Based Project)

## Timeline
- Phase F1 (2026-03-01 to 2026-04-30): parity blockers and UI completion groundwork
- Phase F2 (2026-05-01 to 2026-08-31): major gameplay and renderer differentiation
- Phase F3 (2026-09-01 to 2026-12-31): feature hardening, polish, and release readiness

## Epic FR-01: Native Vulkan Gameplay Parity
Objective: close gameplay-visible parity gaps versus OpenGL while preserving native Vulkan policy.

Primary Areas: `src/rend_vk/*`, `src/client/renderer.cpp`, `docs-dev/vulkan-*.md`

Exit Criteria:
- Vulkan supports all essential gameplay rendering paths used in core multiplayer and campaign flows.
- Known parity blockers from Vulkan audits are closed or explicitly deferred with owner/date.

Tasks:
- [ ] `FR-01-T01` Implement Vulkan equivalents for particle style controls (`gl_partstyle` parity map to `vk_/r_` cvars).  
  Dependency: none. Priority: P0.
- [ ] `FR-01-T02` Implement Vulkan beam style parity (`gl_beamstyle` behavior equivalents).  
  Dependency: `FR-01-T01`. Priority: P0.
- [ ] `FR-01-T03` Add `RF_FLARE` behavior parity in Vulkan entity path.  
  Dependency: none. Priority: P1.
- [ ] `FR-01-T04` Complete MD2 and MD5 visual parity pass with map-driven validation scenes.  
  Dependency: none. Priority: P0.
- [ ] `FR-01-T05` Resolve remaining sky seam/artifact issues for all six faces and transitions.  
  Dependency: none. Priority: P0.
- [ ] `FR-01-T06` Finalize bmodel initial-state correctness on first render frame.  
  Dependency: `FR-01-T04`. Priority: P0.
- [ ] `FR-01-T07` Add Vulkan parity checklist doc and per-feature status table in `docs-dev/renderer/`.  
  Dependency: `FR-01-T01..T06`. Priority: P1.
- [ ] `FR-01-T08` Add Vulkan runtime debug overlays/counters for missing-feature detection.  
  Dependency: none. Priority: P1.

## Epic FR-02: Renderer Role Clarity (OpenGL vs Vulkan vs RTX)
Objective: ensure each renderer has a clearly defined role and quality target.

Primary Areas: `meson.build`, `src/client/renderer.cpp`, `src/rend_vk/*`, `src/rend_rtx/*`

Exit Criteria:
- Renderer selection behavior is explicit and documented.
- Vulkan raster and RTX path-tracing are clearly differentiated in functionality and messaging.

Tasks:
- [ ] `FR-02-T01` Produce renderer capability matrix (`opengl`, `vulkan`, `rtx`) and include cvar mapping.  
  Dependency: none. Priority: P0.
- [ ] `FR-02-T02` Add runtime command to dump active renderer capabilities to log/console.  
  Dependency: `FR-02-T01`. Priority: P1.
- [x] `FR-02-T03` Align launch/debug presets with current renderer names and expected modes.  
  Dependency: none. Priority: P1.
- [ ] `FR-02-T04` Validate and document fallback/error behavior for missing renderer DLLs.  
  Dependency: none. Priority: P1.
- [ ] `FR-02-T05` Add parity smoke map sequence for each renderer in nightly validation.  
  Dependency: `DV-02-T03`. Priority: P1.
- [ ] `FR-02-T06` Publish renderer support policy page under `docs-user/` for end users.  
  Dependency: `FR-02-T01`. Priority: P2.
- [x] `FR-02-T07` Add SDL/MoltenVK Vulkan window/surface support for macOS and other SDL-backed platforms.  
  Dependency: none. Priority: P0.

## Epic FR-03: JSON UI Rework Completion
Objective: complete modern menu coverage and remove remaining UX gaps for core settings and flows.

Primary Areas: `src/game/cgame/ui/*`, `src/game/cgame/ui/worr.json`, menu proposal docs

Exit Criteria:
- Main menu, in-game menu, and settings hierarchy are complete and stable.
- High-value missing widgets are implemented or replaced by approved alternatives.

Tasks:
- [ ] `FR-03-T01` Convert current menu proposal into implementation backlog with explicit widget tickets.  
  Dependency: none. Priority: P0.
- [ ] `FR-03-T02` Implement dropdown overlay behavior (no legacy spin-style fallback for new pages).  
  Dependency: `FR-03-T01`. Priority: P0.
- [ ] `FR-03-T03` Implement palette picker widget for color-centric settings.  
  Dependency: `FR-03-T01`. Priority: P1.
- [ ] `FR-03-T04` Implement crosshair tile/grid selector with live preview.  
  Dependency: `FR-03-T01`. Priority: P1.
- [ ] `FR-03-T05` Implement model preview widget for player visuals pages.  
  Dependency: `FR-03-T01`. Priority: P1.
- [ ] `FR-03-T06` Audit and complete settings page cvar wiring for Video/Audio/Input/HUD/Downloads.  
  Dependency: `FR-03-T02..T05`. Priority: P0.
- [ ] `FR-03-T07` Add menu regression checklist (navigation, conditionals, scaling, localization).  
  Dependency: `FR-03-T06`. Priority: P1.
- [ ] `FR-03-T08` Complete split between engine-side and cgame-side UI ownership where still mixed.  
  Dependency: `FR-03-T06`. Priority: P1.
- [x] `FR-03-T09` Complete multi-monitor settings hierarchy and monitor targeting behavior for fullscreen modes.
  Dependency: `FR-03-T06`. Priority: P1.
- [x] `FR-03-T10` Align the fixed-layout main menu framing with Quake II rerelease reference captures.
  Dependency: none. Priority: P1.

## Epic FR-04: Bots and Match Experience
Objective: evolve bot and match systems from structural presence to reliable gameplay experience.

Primary Areas: `src/game/sgame/bots/*`, `src/game/sgame/match/*`, `src/game/sgame/gameplay/*`

Exit Criteria:
- Bots can join, navigate, fight, and participate in primary supported modes without obvious dead behavior.
- Match flow automation remains stable with bots in common scenarios.

Tasks:
- [ ] `FR-04-T01` Define bot MVP behavior set (spawn, roam, engage, objective awareness).  
  Dependency: none. Priority: P0.
- [ ] `FR-04-T02` Implement frame logic in `Bot_BeginFrame` and `Bot_EndFrame`.  
  Dependency: `FR-04-T01`. Priority: P0.
- [ ] `FR-04-T03` Add weapon selection heuristics and situational item use.  
  Dependency: `FR-04-T02`. Priority: P1.
- [ ] `FR-04-T04` Add team mode awareness (CTF/TDM/etc.) to bot utility state updates.  
  Dependency: `FR-04-T02`. Priority: P1.
- [ ] `FR-04-T05` Add map-level nav validation pass and bot spawn diagnostics.  
  Dependency: `FR-04-T02`. Priority: P1.
- [ ] `FR-04-T06` Add bot participation checks to match/tournament/map-vote flows.  
  Dependency: `FR-04-T02`. Priority: P1.
- [ ] `FR-04-T07` Provide bot tuning cvars in preferred naming convention (`sg_` for new controls).  
  Dependency: `FR-04-T01`. Priority: P2.

## Epic FR-05: Asset and Format Expansion
Objective: expand supported content formats without breaking current workflows.

Primary Areas: `src/renderer/*`, `src/rend_gl/*`, `src/rend_vk/*`, `inc/format/*`

Exit Criteria:
- Planned format support (IQM and extended BSP variants) has either landed or has approved implementation tracks with owners.

Tasks:
- [ ] `FR-05-T01` Build full format support matrix (current vs target) for MD2/MD3/MD5/IQM/BSP variants/DDS.  
  Dependency: none. Priority: P0.
- [ ] `FR-05-T02` Define IQM implementation plan and shared loader boundaries.  
  Dependency: `FR-05-T01`. Priority: P1.
- [ ] `FR-05-T03` Define extended BSP support plan (`IBSP29`, `BSP2`, `BSP2L`, `BSPX`) with compatibility rules.  
  Dependency: `FR-05-T01`. Priority: P1.
- [ ] `FR-05-T04` Add renderer-side format fallback diagnostics for unsupported assets.  
  Dependency: `FR-05-T01`. Priority: P2.
- [ ] `FR-05-T05` Add staged asset validation checks to packaging pipeline for new formats.  
  Dependency: `DV-02-T04`. Priority: P1.
- [ ] `FR-05-T06` Add user-facing docs describing supported asset formats and caveats.  
  Dependency: `FR-05-T01..T05`. Priority: P2.

## Epic FR-06: Audio, Feedback, and Accessibility
Objective: improve clarity and accessibility of gameplay feedback while preserving style.

Primary Areas: `src/client/sound/*`, `src/game/cgame/*`, localization and font docs

Exit Criteria:
- Critical feedback channels (audio cues, UI text, readability) are configurable and regression-tested.

Tasks:
- [ ] `FR-06-T01` Consolidate spatial audio follow-up backlog into implementation tasks.  
  Dependency: none. Priority: P1.
- [ ] `FR-06-T02` Complete graphical obituaries/chatbox enhancement track and integrate with localization.  
  Dependency: none. Priority: P1.
- [ ] `FR-06-T03` Add accessibility pass for text backgrounds, scaling, contrast defaults, and fallback fonts.  
  Dependency: none. Priority: P1.
- [ ] `FR-06-T04` Add presets for competitive readability vs immersive presentation.  
  Dependency: `FR-06-T03`. Priority: P2.
- [ ] `FR-06-T05` Add QA script/checklist for multi-language font rendering in main HUD/menu surfaces.  
  Dependency: `FR-06-T03`. Priority: P1.

## Epic FR-07: Multiplayer Operations and Match Tooling
Objective: harden map vote, match logging, tournament, and admin workflows.

Primary Areas: `src/game/sgame/match/*`, `src/game/sgame/menu/*`, `src/game/sgame/commands/*`

Exit Criteria:
- Admin and competitive flows are stable across map transitions and match-state changes.

Tasks:
- [ ] `FR-07-T01` Add end-to-end validation scenarios for map vote, mymap queue, and nextmap transitions.  
  Dependency: none. Priority: P1.
- [ ] `FR-07-T02` Harden tournament veto/replay flows with explicit error handling and state resets.  
  Dependency: none. Priority: P1.
- [ ] `FR-07-T03` Improve match logging artifact schema/versioning for downstream tooling.  
  Dependency: none. Priority: P2.
- [ ] `FR-07-T04` Add command-level audit for vote/admin privileges and abuse controls.  
  Dependency: none. Priority: P1.
- [ ] `FR-07-T05` Add server-operator docs for new competitive tooling and expected cvars.  
  Dependency: `FR-07-T01..T04`. Priority: P2.

## Epic FR-08: Online Ecosystem Foundations
Objective: prepare for web integration, identity, and service coupling without destabilizing core runtime.

Primary Areas: updater, release index tooling, future external services

Exit Criteria:
- Online roadmap is decomposed into incremental, testable tasks with security and reliability guardrails.

Tasks:
- [ ] `FR-08-T01` Define service boundary document for engine, game module, updater, and external web services.  
  Dependency: none. Priority: P1.
- [ ] `FR-08-T02` Define authentication and identity model (Discord OAuth or alternative) with threat model.  
  Dependency: `FR-08-T01`. Priority: P2.
- [ ] `FR-08-T03` Define server browser data contract between in-game UI and backend service.  
  Dependency: `FR-08-T01`. Priority: P2.
- [x] `FR-08-T04` Define CDN/update channel strategy aligned with existing release index format.  
  Dependency: none. Priority: P2.
- [ ] `FR-08-T05` Stage a minimal public server deployment runbook and monitoring checklist.  
  Dependency: `FR-08-T01`. Priority: P2.

## Development Roadmap (Task-Based Project)

## Timeline
- Phase D1 (2026-03-01 to 2026-04-30): governance and quality-gate foundation
- Phase D2 (2026-05-01 to 2026-08-31): test automation, architecture cleanup, and CI scale-up
- Phase D3 (2026-09-01 to 2026-12-31): hardening, sustainability, and release excellence

## Epic DV-01: Project Governance and Team Workflow
Objective: make project tracking mandatory and consistent across all major work.

Primary Areas: `AGENTS.md`, `README.md`, `docs-dev/projects` process docs

Exit Criteria:
- All significant initiatives are tracked with epic/task IDs and lifecycle states.

Tasks:
- [ ] `DV-01-T01` Establish canonical project board template and required fields.  
  Dependency: none. Priority: P0.
- [ ] `DV-01-T02` Define naming conventions for epics/tasks/milestones and enforce in docs.  
  Dependency: `DV-01-T01`. Priority: P0.
- [ ] `DV-01-T03` Define WIP limits and escalation rules for blocked tasks.  
  Dependency: `DV-01-T01`. Priority: P1.
- [ ] `DV-01-T04` Add project status review ritual (weekly) with owners and outputs.  
  Dependency: `DV-01-T01`. Priority: P1.
- [ ] `DV-01-T05` Require roadmap task references in major PR descriptions and dev docs.  
  Dependency: `DV-01-T02`. Priority: P0.

## Epic DV-02: CI and Validation Pipeline Expansion
Objective: move from release-focused automation to continuous confidence for day-to-day development.

Primary Areas: `.github/workflows/*`, `tools/release/*`, build scripts

Exit Criteria:
- Every non-trivial change path has automated build/test/smoke coverage before merge.

Tasks:
- [ ] `DV-02-T01` Add PR CI workflow for configure + compile on Windows/Linux/macOS.  
  Dependency: none. Priority: P0.
- [ ] `DV-02-T02` Add matrix targets for external renderer libraries (`opengl`, `vulkan`, `rtx`) in CI.  
  Dependency: `DV-02-T01`. Priority: P0.
- [ ] `DV-02-T03` Add runtime smoke launch checks against `.install/` staging for each platform.  
  Dependency: `DV-02-T01`. Priority: P1.
- [ ] `DV-02-T04` Add staged payload validation for format/manifest completeness in PR CI.  
  Dependency: `DV-02-T01`. Priority: P1.
- [ ] `DV-02-T05` Add failure triage guide and flaky test quarantine workflow.  
  Dependency: `DV-02-T01`. Priority: P2.

## Epic DV-03: Automated Test Strategy
Objective: expand meaningful automated tests across protocol, gameplay, renderer, and tooling.

Primary Areas: `q2proto/tests`, `src/common/tests.c`, future test harness paths

Exit Criteria:
- Core regression-prone systems are covered by deterministic tests and smoke checks.

Tasks:
- [ ] `DV-03-T01` Integrate `q2proto/tests` into main CI path and publish result artifacts.  
  Dependency: `DV-02-T01`. Priority: P0.
- [ ] `DV-03-T02` Add unit-level tests for high-risk shared utilities (`files`, parsing, cvar helpers).  
  Dependency: none. Priority: P1.
- [ ] `DV-03-T03` Add deterministic server game rule tests for match-state transitions.  
  Dependency: none. Priority: P1.
- [ ] `DV-03-T04` Add renderer smoke scenes with pixel/hash tolerance checks for key features.  
  Dependency: `DV-02-T03`. Priority: P1.
- [ ] `DV-03-T05` Add bot scenario tests for spawn, navigation, and objective behavior.  
  Dependency: `FR-04-T02`. Priority: P2.
- [x] `DV-03-T06` Add updater/release index parser tests for stable and nightly channels.  
  Dependency: none. Priority: P1.

## Epic DV-04: Architecture and Code Quality
Objective: reduce maintenance overhead and complete key modernization tracks.

Primary Areas: `meson.build`, `src/client/*`, `src/game/*`, naming policy docs

Exit Criteria:
- Module boundaries are cleaner, duplication is reduced, and coding standards are enforceable.

Tasks:
- [ ] `DV-04-T01` Define C/C++ migration target map with boundaries and no-go zones.  
  Dependency: none. Priority: P1.
- [ ] `DV-04-T02` Complete client/cgame ownership map for duplicated behavior paths.  
  Dependency: none. Priority: P1.
- [ ] `DV-04-T03` Add static analysis and warning-as-error policy for first-party code in CI.  
  Dependency: `DV-02-T01`. Priority: P1.
- [ ] `DV-04-T04` Create cvar namespace modernization plan (`g_` to `sg_` for new server-side controls).  
  Dependency: none. Priority: P1.
- [ ] `DV-04-T05` Track and burn down top 100 first-party TODO/FIXME markers by severity.  
  Dependency: none. Priority: P1.
- [ ] `DV-04-T06` Add subsystem ownership map (maintainers by directory) for faster review routing.  
  Dependency: none. Priority: P2.

## Epic DV-05: Performance and Observability
Objective: establish measurable performance baselines and regression visibility.

Primary Areas: renderers, server frame loop, profiling/logging tools

Exit Criteria:
- Baseline metrics exist and regressions can be identified quickly in development and CI.

Tasks:
- [ ] `DV-05-T01` Define canonical benchmark scenes/maps for renderer and gameplay performance checks.  
  Dependency: none. Priority: P1.
- [ ] `DV-05-T02` Add standardized perf capture commands and output schema.  
  Dependency: `DV-05-T01`. Priority: P1.
- [ ] `DV-05-T03` Add lightweight frame-time and subsystem timing instrumentation toggles.  
  Dependency: none. Priority: P1.
- [ ] `DV-05-T04` Add nightly trend report for key performance metrics.  
  Dependency: `DV-05-T02`. Priority: P2.
- [ ] `DV-05-T05` Add performance budget thresholds for major renderer and server paths.  
  Dependency: `DV-05-T01`. Priority: P2.

## Epic DV-06: Dependency Lifecycle and Security Hygiene
Objective: reduce dependency sprawl and improve update confidence.

Primary Areas: `subprojects/`, Meson wraps, release/build docs

Exit Criteria:
- Dependency versions are intentional, documented, and reviewable with lower drift risk.

Tasks:
- [ ] `DV-06-T01` Audit duplicate vendored versions and define active baseline per dependency.  
  Dependency: none. Priority: P0.
- [ ] `DV-06-T02` Remove or archive superseded dependency trees not needed for reproducible builds.  
  Dependency: `DV-06-T01`. Priority: P1.
- [ ] `DV-06-T03` Add dependency update checklist including security notes and regression tests.  
  Dependency: `DV-06-T01`. Priority: P1.
- [ ] `DV-06-T04` Add monthly dependency maintenance review cadence and owner.  
  Dependency: `DV-06-T01`. Priority: P2.

## Epic DV-07: Documentation Quality and Traceability
Objective: keep docs synchronized with implementation and projects.

Primary Areas: `docs-dev/`, `docs-user/`, root docs

Exit Criteria:
- Significant implementation changes have corresponding current docs with task references.

Tasks:
- [ ] `DV-07-T01` Add docs freshness audit for architecture docs that reference moved/renamed paths.  
  Dependency: none. Priority: P1.
- [ ] `DV-07-T02` Require task ID linkage in all new significant `docs-dev` change logs.  
  Dependency: `DV-01-T02`. Priority: P1.
- [ ] `DV-07-T03` Add concise subsystem index pages (`renderer`, `game`, `build`, `release`) for discoverability.  
  Dependency: none. Priority: P2.
- [ ] `DV-07-T04` Add user-doc parity pass whenever user-visible cvars/features are changed.  
  Dependency: none. Priority: P1.

## Epic DV-08: Release and Updater Hardening
Objective: ensure staged artifacts, update metadata, and updater behavior remain reliable under growth.

Primary Areas: `tools/release/*`, `tools/refresh_install.py`, `src/updater/worr_updater.c`

Exit Criteria:
- Release artifacts are consistently valid and updater behavior is deterministic across channels.

Tasks:
- [x] `DV-08-T01` Add test fixtures for release index parsing edge cases (missing assets, mixed channels, malformed metadata).  
  Dependency: `DV-03-T06`. Priority: P1.
- [ ] `DV-08-T02` Add checksum/signature policy review for package trust model.  
  Dependency: none. Priority: P2.
- [ ] `DV-08-T03` Add rollback and failed-update recovery validation scenarios.  
  Dependency: none. Priority: P1.
- [ ] `DV-08-T04` Add release readiness checklist tied to roadmap milestone gates.  
  Dependency: `DV-01-T01`. Priority: P1.
- [x] `DV-08-T05` Split client/server archive payloads and stage the canonical repo assets as `basew/pak0.pkz`.  
  Dependency: none. Priority: P1.
- [x] `DV-08-T06` Unify local and published runtime layouts under a single `basew/` gamedir and make release binaries boot that layout by default.  
  Dependency: `DV-08-T05`. Priority: P1.
- [x] `DV-08-T07` Standardize arch-suffixed bootstrap/engine binary names and updater metadata across supported platforms.  
  Dependency: `DV-08-T06`. Priority: P1.
- [x] `DV-08-T08` Align nightly release publishing and updater channel selection after dropping GitHub prerelease publishing.  
  Dependency: `DV-08-T07`. Priority: P1.
- [x] `DV-08-T09` Implement the cross-platform desktop bootstrap updater flow with bootstrap/engine-library split, splash-first startup, and role-scoped installer staging.  
  Dependency: `DV-08-T07`. Priority: P0.
- [x] `DV-08-T10` Repair the vendored libcurl wrap and bootstrap launcher Windows build path so local fallback builds can compile and stage the desktop updater layout.  
  Dependency: `DV-08-T09`. Priority: P1.
- [x] `DV-08-T11` Stabilize the Windows public-bootstrap-to-temp-worker approved-update handoff and add deterministic local automation for that path.  
  Dependency: `DV-08-T09`. Priority: P0.
- [ ] `DV-08-T12` Convert the client bootstrap into a long-lived session shell that owns the display/window lifecycle, keeps updater UX in-process, and reserves the external worker for locked-file replacement and relaunch only.
  Dependency: `DV-08-T09`, `DV-08-T11`. Priority: P1.

## Immediate 90-Day Priority Queue (2026-03-01 to 2026-05-31)
- [ ] `P0` `FR-01-T01` Vulkan particle style parity
- [ ] `P0` `FR-01-T04` MD2/MD5 parity pass
- [ ] `P0` `FR-03-T02` JSON dropdown overlay
- [ ] `P0` `FR-04-T02` Bot frame logic implementation
- [ ] `P0` `DV-01-T01` Project board template rollout
- [ ] `P0` `DV-02-T01` PR CI workflow
- [ ] `P0` `DV-03-T01` Integrate q2proto tests into CI
- [ ] `P0` `DV-06-T01` Dependency baseline audit

## Governance Note
This roadmap is intended to be the live planning source for WORR 2026 execution. Any significant new initiative should be added here first as an epic/task set (or linked as a child project) before implementation starts.
