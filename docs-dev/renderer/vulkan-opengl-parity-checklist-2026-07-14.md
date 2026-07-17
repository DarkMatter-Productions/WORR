# Native Vulkan/OpenGL Parity Checklist

Date: 2026-07-14
Project task: `FR-01-T07`
Status: complete as an audited checklist; open implementation work is tracked
by `FR-01-T10..T15` and `FR-02-T13..T15`

## Outcome

This document is the canonical code-backed checklist for the native Vulkan
raster renderer versus the OpenGL renderer. It supersedes the feature-status
table in `docs-dev/vulkan-particles-parity-audit-2026-02-11.md`.

`FR-01-T01..T06` closed the original gameplay-visible blockers: particle
style, beam style, flares, MD2/MD5 presentation, all six sky faces, and bmodel
first-frame ownership. This audit confirms those paths in current source,
records the strength of their evidence, and turns every remaining known
visual, functional, diagnostic, or performance gap into a project task.

All Vulkan entries below mean native Vulkan implementations. Redirecting any
Vulkan path through OpenGL is prohibited.

## Status and evidence rules

- **PASS**: no known contract delta in the scoped feature and paired renderer
  evidence exists.
- **IMPLEMENTED**: the native path exists and code inspection found no known
  contract delta, but a durable paired capture is still absent.
- **PARTIAL**: the native path exists with a named behavioral delta.
- **MISSING**: OpenGL has a user-visible or engineering capability with no
  native Vulkan equivalent.
- **DEFERRED**: intentionally owned by a named dependency task.
- **Gate**: repository-owned manifest, thresholds, and retained report.
- **Scene**: paired runtime comparison documented in an implementation log.
- **Code**: source inventory or smoke evidence only.

A row does not become **PASS** merely because both renderers start, expose the
same public function, or happen to look similar in an uncontrolled scene.

## Visual and functional matrix

| Area | Vulkan status | Evidence and current boundary | Follow-up |
|---|---|---|---|
| Renderer lifecycle and public refresh API | **IMPLEMENTED** | `src/rend_vk/vk_main.c` implements registration, images, frame rendering, 2D drawing, raw video, screenshots, mode changes, and renderer queries declared by `inc/renderer/renderer.h`. | Add capability dump under `FR-02-T01/T02`. |
| 2D UI, console fonts, kfont, raw video, clip/scale, RmlUi geometry | **IMPLEMENTED** | Native paths are in `vk_main.c` and `vk_ui.c`; a guarded Vulkan RmlUi capture passes its layout/input assertions with 372 render frames and clean validation. UI geometry now stages to per-frame device-local vertex/index buffers. The headless 960x720 matrix proves exact core parity (691,200 pixels) plus manifest-bounded main-shell, performance-settings, shell quit-confirmation, and session leave-match/forfeit-confirmation parity under validation. Native gameplay gates now prove exact crosshair pixels/12-pixel mask and exact classic status-bar data/294-pixel detail mask after Vulkan gained the OpenGL-compatible point-filter policy. Broader inventory, chat, modern match HUD, other session-popup, and input-state coverage remains open. See `vulkan-ui-device-local-stream-2026-07-15.md`, `vulkan-rmlui-overlay-paired-telemetry-2026-07-15.md`, `vulkan-rmlui-overlay-matrix-expansion-2026-07-15.md`, `vulkan-rmlui-session-popup-parity-2026-07-15.md`, and `vulkan-hud-filter-parity-2026-07-15.md`. | Add live inventory/chat/modern-HUD and session-menu scenes under `FR-02-T05`. |
| Image formats, replacement textures, and wall/skin sampler wrapping | **PASS** | PNG/TGA/DDS override order, repeat sampling, and native device-supported anisotropy are paired on rerelease skins. The video UI drives shared r_anisotropy and r_texture_filter; gl_anisotropy, vk_anisotropy, and legacy gl_texturemode remain compatibility aliases. Vulkan defaults to the legal device anisotropy limit, safely rebinds native material descriptors after either change, and retains a valid 1x fallback on unsupported adapters. See vulkan-renderer-parity-pass-2026-06-12.md, vulkan-gpu-md2-residency-material-parity-2026-07-17.md, vulkan-shared-anisotropy-control-parity-2026-07-17.md, and vulkan-shared-texture-filter-control-parity-2026-07-17.md. | Keep in general smoke sequence. |
| Opaque BSP base textures, legacy/decoupled authored lightmaps, light styles, brightness/modulate, and `r_intensity` | **PASS** | Paired `q2dm1` scenes plus the exact legacy v38 lightmap gate added here. Vulkan now derives the same 16-unit grid metadata as OpenGL before atlas packing; shader/atlas paths are in `vk_world.c` and `vk_world_shadow.frag`. | Linear-light/output contract remains `FR-02-T13`. |
| Global `r_fullbright` visual behavior | **PASS** | The strengthened `FR-01-T06` gate uses a real dark world lightmap and is pixel-identical at `r_fullbright 1`. Entity lighting, world fragments, and visual `R_LightPoint` all bypass lighting. | Gameplay lightlevel is separately renderer-neutral under `FR-01-T09`. |
| Dynamic world/entity lights and raster shadow receiving | **PASS** | Native backend uses the shared shadow frontend, depth/EVSM page resources, dynamic-light receiver shaders, and validated materialization. The per-frame transient caster stream now stages into device-local Vulkan vertex storage and a completed flashlight scene has clean Vulkan validation. See `FR-02-T09..T12` and `vulkan-shadow-device-local-stream-validation-2026-07-15.md`. | Capacity/material work is `FR-02-T14`; sun separation is `FR-02-T15`. |
| Six-face skybox, rotation, seams, and foreground masking | **PASS** | Six strict manifest scenes pass with clean validation. The native Vulkan sky cube is immutable device-local geometry; the compact frame stream carries its legacy rotation and preserves camera-relative projection. Compatible native faces copy once into a 2D array and submit in one draw; incompatible sets retain the six-face Vulkan fallback. See `vulkan-sky-seam-parity-2026-07-14.md`, `vulkan-static-sky-and-liquid-validation-2026-07-15.md`, and `vulkan-sky-texture-array-submission-2026-07-15.md`. | None known. |
| Inline BSP models and first-frame transforms | **PASS** | Generated transformed scene has zero RGB error and IoU `1.0`. Ordinary bmodels now retain immutable native Vulkan local geometry and use a per-frame transform/light instance; special flags keep the established native CPU fallback. See `vulkan-bmodel-first-frame-parity-2026-07-14.md` and `vulkan-static-inline-bsp-residency-2026-07-15.md`. | Add broader moving/rotated-bmodel scenes under `FR-02-T05`. |
| MD2/MD5 frame resolve, skins, animation, smooth normals, shells, item color, rim/IR/tracker, and outlines | **PASS** | FR-01-T04 has paired scenes, MD2/MD5 outline manifests, native stencil stages, and validation evidence across the linked implementation logs. The GPU-MD2 path now has a forced stock-model gate with zero over-threshold pixels and an exact 2,035-pixel glow mask under validation. | Extend nightly scene selection under FR-02-T05; see vulkan-gpu-md2-residency-material-parity-2026-07-17.md. |
| Viewweapon depth hack, projection, glow, and shadow receiving | **PASS** | Paired gameplay work is recorded in `vulkan-viewweapon-dlight-glow-fixes-2026-06-12.md` and `viewweapon-shadow-receiver-2026-06-13.md`. | Add durable viewweapon manifest under `FR-02-T05`. |
| Sprites | **IMPLEMENTED** | `VK_Entity_AddSprite` handles frame selection, billboarding, alpha, depth hack, texture binding, and native batching. | Add paired sprite/cutout scene under `FR-02-T05`. |
| Particles and additive particle style | **IMPLEMENTED** | `FR-01-T01` maps the GL blend contract to prebuilt Vulkan pipelines and retains batch coalescing. Runtime smoke exists; no repository pixel manifest exists. | Add effect scene under `FR-02-T05`. |
| Beams and `RF_GLOW` lightning | **PASS** | `FR-01-T02` documents paired straight/lightning comparisons for billboard and 12-sided modes. | Promote the retained scene to a manifest under `FR-02-T05`. |
| `RF_FLARE`, fade, orientation, and occlusion | **PASS** | `FR-01-T03` documents paired direct/occluded scenes; Vulkan query reads are asynchronous and never wait. | Promote the retained scene to a manifest under `FR-02-T05`. |
| Alpha-tested world/model cutouts | **IMPLEMENTED** | Static world and inline-BSP cutouts now have a generated paired gate: a 32-alpha front world material must reveal an opaque static backdrop while an alpha-tested inline BSP remains visible. The 235,200-pixel crop is exact under validation; both feature probes have matching counts and IoU 1.0. Fences, sprites, and alpha-tested shadow casters remain uncovered. See vulkan-alpha-test-world-bmodel-parity-2026-07-16.md. | FR-02-T05 scene coverage; caster materials in FR-02-T14. |
| Flowing/warp surface animation and water presentation | **PARTIAL** | Vulkan matches OpenGL flow scroll, per-pixel turbulent warp, transparent-liquid refraction, and the full-screen underwater sine warp natively. It copies scene color into sampled Vulkan memory for the depth-preserving liquid and post-process passes; the fragment paths retain derivative refract displacement, alpha compensation, and `vk_warp_refraction`/`vk_waterwarp` controls. The liquid load pass is pipeline/framebuffer-compatible and explicit native color/depth barriers provide its post-copy ordering. The paused transparent receiver holds 128,000 pixels to maximum RGB error `1 / 1 / 1` with no pixel over 1; the real-contents static underwater receiver holds 307,200 pixels to `1 / 1 / 2` with no pixel over 2, both under validation. Broader liquid material coverage remains open. | `FR-01-T10`; storage follow-up in `FR-01-T13`. See `vulkan-world-animation-device-local-2026-07-14.md`, `vulkan-liquid-refraction-2026-07-15.md`, `vulkan-underwater-waterwarp-visual-parity-2026-07-16.md`, `vulkan-static-sky-and-liquid-validation-2026-07-15.md`, and `windows-headless-renderer-capture-2026-07-15.md`. |
| Transparent world/entity ordering | **PARTIAL** | Vulkan emits each transparent static world face as an independent batch and sorts it back-to-front per frame. Entity batches now carry native opaque, alpha-back, post-liquid, and alpha-front phases so transparent liquid follows OpenGL ordering rather than taking an all-entity snapshot. The generated two-layer `SURF_TRANS33`/`SURF_TRANS66` fixture passed twice with zero over-threshold pixels and a 1.0 blend-mask IoU; broader material-family proof remains absent. | `FR-01-T10`; see `vulkan-transparent-world-ordering-2026-07-14.md` and `vulkan-liquid-refraction-2026-07-15.md`. |
| Glowmaps / emissive replacement maps | **PARTIAL** | Vulkan discovers canonical paired _glow.pcx wall/skin images under r_glowmaps, preserves truecolour replacement precedence, uses wall alpha to lift lightmaps, adds premultiplied skin emission, and supplies runtime r_glowmap_intensity without an OpenGL route. The indexed-PCX wall gate exact-compares 50,000 glow-lit pixels and locks a matching-colour IoU of 1.0; the forced stock-GPU-MD2 gate now has zero pixels over threshold and an exact 2,035-pixel glow mask after native anisotropic material sampling was enabled. Broader replacement-pack and material-family sampling remains open. | FR-01-T11; see vulkan-glowmap-emissive-parity-2026-07-15.md, glowmap-wall-companion-visual-parity-2026-07-15.md, model-skin-glowmap-visual-parity-2026-07-15.md, and vulkan-gpu-md2-residency-material-parity-2026-07-17.md. |
| Full-screen liquid/powerup and damage vignette blends | **PASS** | `VK_UI_DrawScreenBlend` is queued between 3D and HUD and was paired on lava/damage presentation in `vulkan-renderer-parity-pass-2026-06-12.md`. | Add durable blend scene under `FR-02-T05`. |
| Fog | **PARTIAL** | Vulkan consumes global, height, and sky fog refdef data in the existing world/entity receiver shaders through `vk_fog`, with no new pass or descriptor. Generated worldspawn maps exact-compare global and height fog; the real six-face sky route has a bounded sky-fog comparison; a fogged `SURF_TRANS33`/`SURF_TRANS66` scene locks the transparent blend to a one-level RGB envelope; the deterministic 8,192-particle receiver exact-compares; a start-on native `target_laser` locks the `RF_BEAM` receiver to a 128,000-pixel paired crop; an ordinary `misc_model` BFG sprite locks a 144,000-pixel fogged sprite crop; and an authored `misc_flare` now locks a 160,000-pixel direct-flare crop plus a 98,550-pixel unattenuated-red mask at IoU `1.0`. Broader specialised-effect sampling remains open. | `FR-01-T12`; see `vulkan-native-fog-baseline-2026-07-15.md`, `vulkan-authored-global-fog-parity-2026-07-15.md`, `vulkan-authored-height-fog-parity-2026-07-15.md`, `vulkan-authored-sky-fog-parity-2026-07-15.md`, `vulkan-transparent-world-fog-parity-2026-07-15.md`, `vulkan-particle-fog-parity-2026-07-15.md`, `vulkan-beam-fog-parity-2026-07-15.md`, `vulkan-sprite-fog-parity-2026-07-15.md`, `vulkan-flare-fog-contract-2026-07-15.md`, `vulkan-flare-fog-visual-parity-2026-07-15.md`, and `windows-headless-renderer-capture-2026-07-15.md`. |
| Bloom | **PARTIAL** | Vulkan has native scene threshold/knee/firefly bloom, downscaled separable Gaussian blur, final saturation/intensity composition, and bloom-only extraction for wall glow, opaque model skins, translucent colour shells, additive `RF_RIMLIGHT`, and direct depth-hack/view-weapon receivers. The established wall, MD2 glow, world-shell, and rim gates remain in place; a real instant-Quad view-weapon shell holds a 212,800-pixel crop to `0.02055 / 0.01820 / 0.01680` mean RGB error. Its late compatible colour-load pass runs after the now strict paired transparent-liquid receiver and has clean validation. The depth-disabled rim fallback remains on constrained non-sampleable formats. Native `vk_bloom_levels` now builds and blends the same one-to-six-level weighted mip pyramid as OpenGL, with a validation-backed level-three shell gate. Native `vk_showbloom` now presents the level-zero native blurred image directly, bypassing final display transforms as `gl_showbloom` does; its full 960x720 paired gate has maximum RGB error one and only 0.280382% differing pixels under validation. Broader material-family emission and liquid coverage remain open; the normal scene pass remains single-target when bloom is disabled. | `FR-01-T12`, diagnostic coverage `FR-01-T15`; see `vulkan-native-bloom-baseline-2026-07-15.md`, `vulkan-bloom-emission-visual-parity-2026-07-16.md`, `vulkan-model-bloom-emission-visual-parity-2026-07-16.md`, `vulkan-shell-bloom-emission-visual-parity-2026-07-16.md`, `vulkan-rim-bloom-emission-visual-parity-2026-07-16.md`, `vulkan-rim-bloom-occlusion-visual-parity-2026-07-16.md`, `vulkan-depthhack-viewweapon-bloom-visual-parity-2026-07-16.md`, `vulkan-bloom-mip-pyramid-parity-2026-07-16.md`, `vulkan-bloom-debug-preview-parity-2026-07-16.md`, and the bloom manifests. |
| HDR, tone mapping, auto exposure, color correction, split toning, and LUT | **PARTIAL** | Vulkan has native static ACES tone-mapping controls (`vk_hdr`, exposure, white, gamma) plus brightness/contrast/saturation/tint correction, split toning, 2D LUT grading, and bloom composition in the shared waterwarp final pass; identity settings skip the copy and fullscreen draw. The static HDR enabled/disabled gates each exact-compare 50,000 pixels, with the enabled non-identity backdrop locked at `[18, 38, 96]`. A split-tone-only gate now exact-compares 50,000 pixels at non-identity `[9, 10, 34]`; OpenGL correctly activates its final target for independent split/LUT stages, matching Vulkan. The ordinary video-mode lifecycle now rebuilds the native compositor resources, and a hidden-native-surface combined correction capture exact-compares 50,000 pixels with OpenGL. The 2D LUT coordinate path now matches OpenGL texel-space indexing for both strip orientations and its image-backed horizontal/vertical gates exact-compare 50,000 pixels each. The current Vulkan scene copy is swapchain-format LDR, so floating HDR range, auto exposure, and the renderer-neutral linear-output contract remain open. Underwater waterwarp has a dedicated paired gate. | `FR-02-T13`, then `FR-01-T12`; see `vulkan-static-hdr-tonemap-parity-2026-07-16.md`, `vulkan-split-toning-visual-parity-2026-07-16.md`, `vulkan-color-correction-postprocess-2026-07-15.md`, `vulkan-color-correction-visual-parity-2026-07-15.md`, `vulkan-lut-coordinate-parity-2026-07-16.md`, `vulkan-underwater-waterwarp-visual-parity-2026-07-16.md`, `vulkan-native-bloom-baseline-2026-07-15.md`, and `windows-headless-renderer-capture-2026-07-15.md`. |
| Gameplay DOF, CRT, and fixed/dynamic resolution scaling | **PARTIAL** | Vulkan has native depth-aware gameplay DOF: shared `r_dof`, focus-distance/range controls, centre-depth fallback, menu rectangle state, a quarter-resolution two-pair Gaussian blur, and a frame-local depth composite before the existing Vulkan post-process chain. Real inventory-layout fixed-focus, centre-depth/automatic-range, wide explicit-range, and compact translucent-menu receivers each hold 307,200 pixels to maximum RGB error `1 / 1 / 1`, MAE at most `0.004466 / 0.011849 / 0.004063`, and zero pixels over error one; all paired DOF-disabled controls are pixel-identical. Vulkan uses OpenGL's shared sigma scaling and four alternating total blur passes, and preserves the current native composite through the menu rectangle state to match OpenGL's retained post target without an OpenGL route. It also has native `r_crt*` scene presentation with OpenGL-equivalent scanline, phosphor, gamma, and shadow-mask controls; a one-pixel native scanline-phase correction now lets fixed unmasked and shadow-mask-layout-2 CRT captures exact-compare 50,000 pixels with OpenGL while the UI overlay remains unfiltered. Isolated rectangle clipping and broader dynamic-scene DOF coverage, remaining CRT mask/UI coverage, HDR, and fixed/dynamic resolution scaling remain open. | `FR-01-T12` and performance measurement in `FR-01-T15`; see `vulkan-dof-control-parity-2026-07-15.md`, `vulkan-dof-visual-parity-2026-07-16.md`, `vulkan-native-crt-baseline-2026-07-15.md`, `vulkan-crt-visual-parity-2026-07-15.md`, and `windows-headless-renderer-capture-2026-07-15.md`. |
| Native screenshots and deterministic capture | **PASS** | Vulkan copies the presented swapchain image to a host buffer; `FR-01-T04..T06` use repository capture tooling successfully. | Preserve as required evidence for all parity tasks. |
| World-space debug primitives/text | **IMPLEMENTED** | Native Vulkan now rasterizes shared debug lines/shapes and stroke-font text with separate depth/no-depth pipelines, persistent lifetime handling, `cleardebuglines`, and a validation-backed real-client smoke. | Keep the owned smoke in `FR-01-T08` evidence. |
| Runtime feature counters, show-tris/origins, CPU/GPU timings, and missing-feature warnings | **PARTIAL** | `vk_stats` and the renderer stat panel report per-domain draw/vertex/index/upload totals, scene/query counts, debug capacity, CPU frame time, and native completed-frame GPU total plus upload/shadow/scene/composition timings. Native `vk_showbloom` passes the full-frame paired diagnostic gate while directly sampling the native level-zero blur image and bypassing display transforms. Native `vk_showtearing` now alternates a presentation-final red/white clear after all scene and UI work, matching OpenGL's diagnostic contract; startup-frame count can select opposite valid colours, so it is intentionally phase-aware rather than a naive pixel gate. Native `vk_showtris` now owns the four OpenGL bit categories through portable native line lists, a near-plane depth-tested 3D stream, a no-depth flare stream, and a scissor-preserving UI replay. Paired world/inline-BSP and HUD/UI captures have matching changed bounds with validation-clean Vulkan output; broader effect/material scenes remain open. Native `vk_showorigins` now queues depth-tested 16-unit RGB axes with the entity's scaled transform, excluding view, inline-BSP, beam, flare, and weapon-model paths as OpenGL intends. Its structural gate and a validation-clean Vulkan headless capture pass, but the paired image gate is blocked: the current headless OpenGL `gl_showorigins` path enters `GL_DrawNullModel` with the cvar enabled yet emits no RGB axis pixels. A repaired OpenGL origin baseline and reproducible budgets remain open. | `FR-01-T15`; see `vulkan-showtris-diagnostic-parity-2026-07-16.md`, `vulkan-bloom-debug-preview-parity-2026-07-16.md`, `vulkan-tearing-diagnostic-parity-2026-07-16.md`, `vulkan-origin-diagnostic-parity-status-2026-07-16.md`, and `vulkan-gpu-frame-timing-2026-07-15.md`. |
| Renderer-neutral gameplay light query | **IMPLEMENTED** | The client engine samples authored BSP lightmaps/lightgrid plus map lightstyles directly, including inline BSP model hits. `V_SetLightLevel` no longer calls backend-adjusted `R_LightPoint`; gamma/intensity/fullbright/dlights stay presentation-only. | Preserve the renderer-free `FR-01-T09` regression target and engine build gate. |

## Performance and modernization matrix

| Mechanism | Current native Vulkan state | Assessment | Task |
|---|---|---|---|
| Pipeline lifecycle | World/entity/UI/shadow variants are created with swapchain resources, not during entity draws. Flare-free frames skip flare pipelines. | **GOOD BASELINE** | Preserve. |
| Material batching | Consecutive descriptor-compatible triangles coalesce into batches; special opaque/item/alpha/additive ordering is explicit. | **GOOD BASELINE**, but not a general sort key. | `FR-01-T10/T14`. |
| Lightmap style updates | Changed faces update bounded atlas subrects rather than rebuilding the whole image in the ordinary case. | **GOOD BASELINE**; world upload totals now expose the byte cost. | Break out per-phase budgets in `FR-01-T15`. |
| Sky geometry and submission | The 36-vertex cube is immutable device-local geometry. Its 64-byte current-frame record carries animated world controls plus three legacy sky-rotation rows, avoiding the prior 1,728-byte per-frame cube rewrite. Compatible source faces copy once into a six-layer native array, reducing the fixed fixture's sky from six descriptor/draw pairs to one draw. | **HOT-PATH CLOSED**; validate/map changes perform the one-time staged copy, incompatible sets use the native fallback. | `FR-01-T13/T15`; see `vulkan-static-sky-and-liquid-validation-2026-07-15.md` and `vulkan-sky-texture-array-submission-2026-07-15.md`. |
| Flare visibility | Result reads use availability with no wait; resets are coalesced and only flare frames bind query pipelines. | **GOOD BASELINE**; query count is now emitted by `vk_stats`. | GPU timings/budgets in `FR-01-T15`. |
| Static world storage | Static vertices are copied once at map registration through a host-visible staging buffer into an immutable device-local vertex buffer. | **GOOD BASELINE**; upload scheduling remains part of `FR-01-T13`. |
| Warp/flow updates | The CPU world-sized animation array and per-frame full-stream copy are gone. The native shader receives an eight-byte `{ time, effects_enabled }` instance stream each frame. | **HOT-PATH CLOSED**; measure representative map costs under `FR-01-T15`. |
| Entity/model submission | Eligible MD2 frame data and eligible MD5 mesh/weight data now live in immutable device-local buffers. MD2 interpolation and MD5 weighted vertex skinning run in native Vulkan vertex shaders from compact current-frame instance streams. The MD2 static upload descriptors now target their actual device buffers, release before CPU storage, and coalesce only effect-compatible batches; a forced stock GPU-MD2 capture proves the path under validation. Ordinary inline BSP models now also retain immutable local geometry and submit one current-frame transform/light instance; special bmodel flags, sprites, beams, and particles retain the bounded native transient stream. | **PARTIAL** | FR-01-T14: complete remaining static residency, batching/indirect submission, and a general transient ring; see vulkan-gpu-md2-submission-2026-07-15.md, vulkan-gpu-md5-skinning-2026-07-15.md, vulkan-static-inline-bsp-residency-2026-07-15.md, and vulkan-gpu-md2-residency-material-parity-2026-07-17.md. |
| MD5 skinning | Eligible replacement meshes use native device-local bind-pose mesh/weight data plus a frame-local GPU joint palette; the vertex stage reconstructs weighted position and normal. CPU frame/joint interpolation and item-colorize/outline fallback remain. | **PARTIAL** | `FR-01-T14`: establish paired runtime evidence and consider GPU joint interpolation only after measured benefit. |
| Frames in flight | Two bounded native frame contexts (capped by swapchain image count) own command buffers, acquire semaphores, fences, depth attachments/framebuffers, scene-copy images/descriptors, bloom ping-pong images/external descriptors, menu-focus images, timestamp ranges, and frame-local UI/entity/world/shadow/debug streams. The CPU waits only before reusing its current slot; an acquired swapchain image waits for the slot that last used it. Liquid refraction, colour/LUT/CRT composition, bloom, and menu focus can overlap. Only rare all-slot bloom target reallocation drains submissions before resource replacement. | **PARTIAL** | `FR-01-T15`: benchmark the overlap and close broader performance budgets; see `vulkan-bounded-frames-in-flight-2026-07-15.md`. |
| Shadow allocation | Native pages work, and the transient caster stream now grows geometrically; fixed worst-case page arrays and active capacity policy remain. | **SCALING GAP** | `FR-02-T14`; see `vulkan-shadow-stream-capacity-2026-07-15.md`. |
| Profiling and budgets | Vulkan records CPU-frame time, per-domain byte/draw/vertex/index/upload totals, and completed command-buffer GPU totals plus coarse shadow/scene/composition timings through native timestamps. OpenGL now adds a non-blocking native `GL_TIMESTAMP` frame pair around its ordinary render stream, while retaining phase telemetry. `gpu_frame_ms`/`gpu_frame_valid` are the shared GPU-budget metric; legacy `gpu_ms` and phases remain diagnostic. The analyzer verifies fixture/config/log hashes plus hardware/driver provenance before accepting a budget. Static-world fast-light coverage now reports candidates and every eligibility blocker, preventing stale/non-specialized capture data from being mistaken for an active path. The opaque inline-BSP static-light shader now omits an unreachable alpha discard after its native gate proves the material opaque: two validation-enabled 100-sample reruns reduce its entity GPU p50 from `0.350 ms` to `0.241 ms`, scene p50 from `0.749 ms` to `0.638–0.644 ms`, and completed-frame p50 from `0.778 ms` to `0.668–0.675 ms` while retaining exact paired output. No representative-map GPU budget is established yet. | **PARTIAL** | `FR-01-T15`; optimize only against captured before/after budgets; see `vulkan-bmodel-fast-lit-early-depth-2026-07-16.md`, `vulkan-static-world-fast-lit-coverage-2026-07-16.md`, `vulkan-opengl-full-frame-gpu-timing-2026-07-16.md`, and `vulkan-inline-bsp-budget-revalidation-2026-07-16.md`. |

The modernization order is deliberate: instrumentation first, then eliminate
the full-world dynamic upload, then replace CPU-expanded entity streaming,
then enable multiple safe frames in flight. That order creates evidence for
each optimization and prevents throughput changes from obscuring visual
regressions.

### Static-world no-fog receiver refinement

The default-on native `vk_world_fast_lit_no_fog` switch selects dedicated
plain/glow-map static-light SPIR-V only while the current refdef has neither
global nor height fog; sky-only fog remains valid for opaque receivers.
Validation-backed ordinary, global-fog, and height-fog matrices retain exact
paired output. A current-binary 100-sample control pair records 0.399 ms
opaque-world GPU p50 with the specialization disabled and 0.393 ms enabled,
with unchanged 18 draws/4,800 bytes and one telemetry-proven no-fog draw per
frame. It is a localized 1.5% world-phase improvement, not a renderer-wide
GPU budget. See `vulkan-static-world-fast-lit-no-fog-2026-07-16.md`
under `FR-01-T15`.

## Evidence index for completed FR-01 work

| Task | Feature | Durable evidence |
|---|---|---|
| `FR-01-T01` | Particle styles | `vulkan-particle-style-parity-2026-07-12.md` |
| `FR-01-T02` | Beam styles/lightning | `vulkan-beam-style-parity-2026-07-12.md` |
| `FR-01-T03` | Flares/occlusion | `vulkan-flare-occlusion-parity-2026-07-12.md` |
| `FR-01-T04` | MD2/MD5 and presentation flags | `vulkan-special-model-flag-parity-2026-07-14.md`, `vulkan-alias-outline-parity-automation-2026-07-14.md`, and `assets/renderer_parity/fr01_alias_outline_manifest.json` |
| `FR-01-T05` | Six-face sky | `vulkan-sky-seam-parity-2026-07-14.md`, `vulkan-static-sky-and-liquid-validation-2026-07-15.md`, and `assets/renderer_parity/fr01_sky_seams_manifest.json` |
| `FR-01-T06` | Bmodel first frame, legacy lightmaps, and global fullbright | `vulkan-bmodel-first-frame-parity-2026-07-14.md` and `assets/renderer_parity/fr01_bmodel_first_frame_manifest.json` |
| `FR-01-T08` | Native debug primitives, runtime counters, and capability diagnostics | `vulkan-debug-overlay-telemetry-2026-07-14.md` and `tools/renderer_parity/run_vk_debug_smoke.py` |
| `FR-01-T09` | Renderer-neutral gameplay lightlevel | `gameplay-light-query-parity-2026-07-14.md` and `tools/renderer_parity/gameplay_light_test.c` |

## Legacy lightmap and `r_fullbright` closure made during this audit

The T06 entity fix had registered the shared cvar and corrected entity/CPU
lighting, but a lightmapped static world fragment could still sample its
authored lightmap. OpenGL rebuilds the world without lightmaps when
`r_fullbright` is enabled.

The audit fixture first exposed that native Vulkan also relied on BSPX
`DECOUPLED_LM` metadata already being present. Legacy Quake II v38 faces only
store texture axes and a light-data offset; OpenGL derives their 16-unit
lightmap grid while building surface vertices. `VK_World_PrepareLightmapGeometry`
now performs the same derivation before Vulkan atlas allocation, validates
extent/data bounds, and preserves the supplied path for decoupled maps. The
fixture registers as `mode=legacy prepared=1 rejected=0` with six lightmapped
world vertices.

Native Vulkan now carries the global fullbright state in
`ShadowPages.shadow_moment_tuning.w`. `vk_world_shadow.frag` selects white
lighting when either that runtime bit or the authored per-surface fullbright
flag is set. The branch is evaluated per fragment, so a cvar change requires
no world rebuild, vertex upload, descriptor update, or pipeline recreation.

The generated T06 fixture was strengthened with an authored `81 x 61` dark
world lightmap. Its original config sets `r_fullbright 1`; a companion config
sets it to `0`. The first scene proves the runtime bypass while the second
proves legacy lightmap sampling. Final evidence:

```text
evidence root:                    .tmp/renderer-parity/fr01-bmodel-lightmap-final2/
fullbright/bmodel crop pixels:    170000
legacy-lightmap crop pixels:      34000
maximum RGB error (both):         0 / 0 / 0
mean absolute RGB error (both):   0.0 / 0.0 / 0.0
pixels over threshold (both):     0
bmodel mask pixels:               47300 / 47300
dark-lightmap pixels:             34000 / 34000
mask IoU (both):                  1.0
process failures:                 none
Vulkan validation errors:         none
fullbright capture SHA-256:        742FE7EB5518E469EBD3F9FE0A66D3738C520CAE3E3C210EE7A2DF2FE5A1D992
fixture BSP SHA-256:               1B65468863A0D849A319AAF9B12EC00ABDF7CFD995FC733B6ACA99B4B70AE873
```

## Required acceptance pattern for open rows

Every row promoted to **PASS** must include:

1. a native Vulkan implementation with no OpenGL routing;
2. an explicit OpenGL behavior contract and cvar mapping;
3. a repository-owned scene/config/manifest when the output is visual;
4. a machine-readable report with feature-specific thresholds;
5. a Vulkan validation-layer run with no VUID or validation error;
6. focused tests plus a renderer build;
7. refreshed `.install/` assets and binaries; and
8. an implementation log tied to the owning project task.

Performance tasks additionally require before/after CPU time, GPU time,
draw/batch counts, and upload-byte evidence on the same scene sequence. FPS
alone is not an acceptable optimization measurement.

## Validation for this checklist pass

Passed:

```text
python tools/gen_vk_world_spv.py --validate
python tools/renderer_parity/generate_bmodel_first_frame_fixture.py --asset-root assets --validate --json
python tools/renderer_parity/test_compare_captures.py
python tools/test_package_assets.py
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_bmodel_first_frame_manifest.json --run-root .tmp/renderer-parity/fr01-bmodel-lightmap-final2 --vulkan-validation --json-output .tmp/renderer-parity/fr01-bmodel-lightmap-final2/results-runner.json
```

The comparison suite passed 4 tests, including focused-scene selection;
packaging passed 14 tests; staged asset validation packaged 330 assets. The Vulkan renderer target compiled and
linked. The engine target invocation was blocked by unrelated concurrent
networking compile inconsistencies (`carrier_command_shape` call arity and
undeclared native-readiness pilot symbols); existing engine/OpenGL staged
artifacts remained valid for the paired capture.

Staged SHA-256 values after this pass:

```text
70909BC06A162FA7BD367A908B0BAB7D7A182CCD691ED1CB5BEDA26DC31BD9A4  .install/worr_engine_x86_64.dll
2357AB1887D2083175FADD5950DAF764E0DFAFD3991E2D32836EB7D926756DC9  .install/worr_vulkan_x86_64.dll
D242B399152B7E6E93AE80A5D832665B18C3B38AB78C41D59CEB5464E185B494  .install/worr_opengl_x86_64.dll
82C16BCAF2B5DCD5E5AED2E1B5B3CCC31054058D58BA74008387D4D61CDF10A8  .install/basew/pak0.pkz
1B65468863A0D849A319AAF9B12EC00ABDF7CFD995FC733B6ACA99B4B70AE873  .install/basew/maps/worr_fr01_bmodel_first_frame.bsp
```

## Checklist conclusion

`FR-01-T07`, `FR-01-T08`, and `FR-01-T09` are complete: the repository now
has one current evidence-graded parity source of truth, native debug/runtime
diagnostic evidence, and a backend-independent gameplay-lightlevel path. The
renderer is not yet globally feature-complete or performance-superior:
glowmaps, post-processing, transparent ordering, GPU-resident geometry, GPU
skinning, and measurable performance gates remain open under explicit tasks.
This distinction is intentional and
prevents the completion of individual tasks from being misreported as
completion of the renderer objective.
