# WORR Shadowmapping Replacement Analysis and Implementation Plan

## Executive diagnosis

The problem in WORR is not one isolated shader bug. It is a shadow system that has repeatedly been patched around foundational architectural mistakes: shadow visibility was tied to camera-centric visibility, shadow passes mutated main-view renderer state, caster bounds and bmodel transforms were wrong, shadow sampling used the wrong light vectors, and the cache/update story was rewritten several times in ways that even the project’s own later design notes explicitly reject. The strongest evidence is the sequence of internal fix notes themselves: the implementation guide says shadow rendering must not depend on camera PVS logic, later notes fix PVS2/PHS visibility, light-vector direction, per-pixel enablement, HOM caused by `visframe` leakage, and then a same-day “stability/fallback/cache expansion” path is superseded by a “rebuild from a clean baseline” document that removes the fallback/slot-churn stack entirely. fileciteturn29file0L1-L1 fileciteturn22file0L1-L1 fileciteturn25file0L1-L1 fileciteturn26file0L1-L1 fileciteturn46file0L1-L1 fileciteturn58file0L1-L1 fileciteturn38file0L1-L1 fileciteturn18file0L1-L1

The right conclusion is that WORR should stop accumulating local shadow fixes inside the legacy renderer state machine and instead replace the current implementation with a smaller, backend-neutral shadow frontend that preserves the Quake II Rerelease game-side contract, uses PVS2/PHS only where they are actually correct, and exposes separate GL and Vulkan backends under one deterministic selection, touching, caching, and quality policy. That is the only practical way to meet all four requirements at once: KEX replacement, efficient touching/culling, high quality, and compatibility with both non-RTX renderers. fileciteturn31file0L1-L1 fileciteturn36file0L1-L1 fileciteturn48file0L1-L1 fileciteturn30file0L1-L1

## What is actually broken

**The visibility domain is wrong unless it is explicitly separated.** WORR’s own implementation guide says shadow rendering must not be driven by camera PVS, because the light is effectively its own camera. The later PVS2 fix note then confirms that the system had been dropping shadow-relevant entities on the server and discarding lights in the renderer because it reused strict PVS/PHS handling and main-view `visframe` tests. A later dlight-culling note shows there was still a second-stage failure where lights chosen by PVS could be thrown away again by camera-frustum logic during UBO upload. That combination means the old system could be “correct” in one stage and invalidated in the next. fileciteturn29file0L1-L1 fileciteturn22file0L1-L1 fileciteturn56file0L1-L1 fileciteturn57file0L1-L1

**Main-view renderer state was contaminated by the shadow pass.** The HOM fix note is especially damning: shadow rendering incremented `glr.visframe` and stamped world `node->visframe`, then the main world pass could early-out and fail to restamp the camera-visible nodes, leaving the frame rendered with shadow-pass visibility instead of view visibility. That is not a tuning problem; it is proof that the current shadow pass is still too entangled with idTech2’s main visibility machinery. Any replacement that continues to share those stamps will remain fragile. fileciteturn58file0L1-L1

**Caster and occluder spatial tests were repeatedly wrong.** WORR had to fix MD2 caster bounds because alias models were being tested from origin-centered radius data rather than translated frame bounds; it had to fix brush entities because bmodel shadow passes were using the wrong transform and because single-sided surfaces were being dropped by culling; and the implementation guide separately calls out inline BSP models needing a transformed local-center test rather than raw `ent->origin`. Those are not cosmetic issues: they directly determine whether lights dirty, whether casters are included, and whether the resulting shadow volume is even spatially valid. fileciteturn22file0L1-L1 fileciteturn56file0L1-L1 fileciteturn29file0L1-L1 fileciteturn27file0L1-L1

**The shadow sampling contract was broken in multiple independent ways.** WORR had to fix one bug where shadow sampling used a fragment-to-light vector instead of a light-to-fragment vector, which flipped cubemap-face choice and effectively disabled shadows, and another where it sampled shadows using the normal-offset lighting position instead of the actual shadow-render origin. A separate enablement note says shadowmaps could render but never be applied at all when per-pixel lighting was off. Taken together, that shows the old implementation did not have a clean single definition of “the light in shadow space,” “the light in shading space,” and “the receiver path that consumes the result.” fileciteturn26file0L1-L1 fileciteturn25file0L1-L1 fileciteturn46file0L1-L1

**The cache/update-touching story is not trustworthy in its current evolutionary form.** One note says static caches were invalidated globally by a scene hash over shadow-casting entities; the next replaces that with per-frame caster lists and dynamic-caster scans; a later same-day note adds hysteresis, sticky selection, and expanded cache residency; and the final rebuild note explicitly removes the no-slot fallback path, sticky/hysteresis tuning, and expanded LRU cache residency in favor of a simpler deterministic baseline. That is strong evidence that WORR does not have one stable cache model today; it has multiple partially abandoned ones. For the user’s performance goal, especially “update touching and culling through pvs2/phs,” that is the single biggest risk area. fileciteturn23file0L1-L1 fileciteturn24file0L1-L1 fileciteturn38file0L1-L1 fileciteturn18file0L1-L1

**The multiplayer/server compromise is real and must be treated as a deliberate policy, not an accidental side effect.** The PVS2 fix note explicitly says expanding shadow-aware visibility can send more entities and thus reveal some extra shadow-relevant presence information in multiplayer. That may still be the right trade if the goal is Quake II Rerelease parity, but it should be a narrowly defined policy and not something that spreads accidentally through unrelated networking code. fileciteturn22file0L1-L1

**The resource model is broader than it needs to be for the common case.** WORR’s notes show a color-texture ladder for shadowmap formats because some drivers reject `RG16F` array textures as color-renderable FBO attachments. That is consistent with the project’s moments-based path, but it also suggests the implementation is paying moment-texture complexity and compatibility cost even when the chosen filter mode is hard compare or PCF. OpenGL’s own guidance is that compare-enabled depth textures should be sampled through shadow samplers, and Vulkan’s depth rules likewise make depth-only sampling a first-class path. That means a better replacement should split “compare-depth shadow pages” from “moment shadow pages” instead of forcing one storage path to serve all filters. fileciteturn42file0L1-L1 citeturn9view6turn9view7turn9view5

## Where WORR diverges from KEX

KEX’s shadow system is not just “some shadowmaps.” The reverse-engineered `quake2ex_steam` notes describe a renderer-native architecture with clustered-light collection, split-local light uploads, a quadtree atlas allocator, cached per-light shadow-view blocks, a dedicated shadow render context, and worker queues for both cluster-tile generation and shadow submission. KEX also distinguishes between general dlight shadow records and tracked-entity shadow records, and its per-light records already carry the semantics WORR needs to mirror: face count, requested resolution, owner id, cache link, and whether the light casts shadows at all. fileciteturn31file0L1-L1 fileciteturn32file0L1-L1 fileciteturn33file0L1-L1

WORR, by contrast, is still fundamentally using the classic renderer and retrofitting shadow state around selected light slots, per-pass marking, and GL/Vulkan compatibility glue. The Vulkan story today is not a native shadow backend; the documented `vk_rtx 0` path simply aliases `vk_*` cvars into `gl_*` shadow controls and then falls back to the OpenGL renderer. That is useful as a compatibility bridge, but it is not a native Vulkan implementation and should not be mistaken for one. fileciteturn30file0L1-L1

The good news is that WORR already speaks the right game-side dialect for a KEX-style replacement. The selected repos show `RF_CASTSHADOW`, `RF_NOSHADOW`, `MAX_SHADOW_LIGHTS`, and the `CS_SHADOWLIGHTS` payload format on the rerelease side, while WORR’s game code already serializes point/cone type, resolution, intensity, fade values, style, cone angle, and cone direction into those configstrings. That means the replacement does **not** need a new gameplay/network contract. It needs a better renderer contract. fileciteturn36file0L1-L1 fileciteturn48file0L1-L1

The important nuance is that WORR should borrow KEX’s **principles**, not cargo-cult all of KEX’s machinery into `rend_gl`. KEX’s atlas and clustered uploads make sense because the KEX renderer is already built around clustered lights and frame-local shadow-view records. WORR is not. An elegant replacement for WORR should therefore preserve KEX-visible behavior and data semantics while introducing a smaller intermediate architecture: a backend-agnostic shadow frontend, persistent per-light/per-view cache records, deterministic selection, and a backend page allocator that can start as fixed array pages in GL and later evolve into a fuller atlas if it becomes justified. fileciteturn31file0L1-L1 fileciteturn18file0L1-L1

## The replacement architecture

The replacement should be organized around a **shared shadow frontend** that is completely independent from the camera render list and from the GL/Vulkan backend. Its job is to collect lights from three sources — rerelease-style shadowlights from `CS_SHADOWLIGHTS`, runtime dlights, and optional sun cascades — and to collect a separate shadow-caster list for all relevant models and bmodels, including entities that might not be drawn in the current camera pass but can still cast into visible space. Candidate light selection should use PVS2, area bits, and screen/influence scoring only to determine whether a light can matter to the current camera; actual shadow occluder selection must then use light-space sphere/cone/cascade volume tests against BSP nodes and caster bounds, not camera PVS. That clean separation is the core correction WORR has been repeatedly circling around in its own notes. fileciteturn29file0L1-L1 fileciteturn22file0L1-L1 fileciteturn27file0L1-L1

For **touching and cache invalidation**, the frontend should track touched leaf/cluster sets for dynamic casters and influence leaf/cluster sets for static lights. The first pass is a cheap bitset overlap against PVS2/light-cluster coverage; the second pass is a precise sphere-vs-bounds or cone-vs-bounds test. That gives the user the performance property they asked for: updates are driven by touched clusters and overlap tests, not by rescanning every entity for every light every frame. WORR’s own evolution from global scene hashes to caster lists and then to a simpler rebuild already points toward this, but the next replacement should make it explicit and permanent. fileciteturn23file0L1-L1 fileciteturn24file0L1-L1 fileciteturn18file0L1-L1

For **resource layout**, I recommend splitting the storage path into two families. Hard compare, PCF, and PCSS should use compare-capable depth pages; VSM and EVSM should use moment pages plus optional mip generation. That aligns with standard shadow-map practice, reduces pointless color-bandwidth work in the common compare path, and maps cleanly to both APIs. OpenGL already exposes layered framebuffer attachment and shadow samplers for depth arrays/cubemap arrays, while Vulkan formalizes cube-array layer ordering, depth-format support, and explicit synchronization between the shadow pass and the main pass. WORR can still keep its manual cube-face math and 2D-array emulation if that is the easiest way to guarantee GL/VK shader parity, but it should stop forcing that choice on every filter mode. citeturn9view6turn9view7turn7search5turn9view4turn9view5turn11view0

For **quality**, WORR is already targeting the right family of techniques: PCF as the baseline introduced by classic depth-map shadowing, VSM for filterable moments with known light-bleeding tradeoffs, EVSM/ESM-style warped moments to reduce leakage, PCSS for variable penumbra softness, and PSSM/CSM for sun shadows. The replacement should keep those options, but selection must be deterministic and quality modes must be budgeted: PCSS should be limited to the top few lights, sun cascades should remain separate from local-light slot pressure, and nonzero slope/normal-offset bias defaults should ship from day one rather than being left at “debug-clean room” values. citeturn8view0turn8view1turn4search3turn9view2turn10search5 fileciteturn53file0L1-L1 fileciteturn24file0L1-L1

For **Vulkan**, the near-term compatibility answer remains the existing `vk_rtx 0` bridge, but the real long-term answer is a native Vulkan shadow backend under the same frontend. Vulkan explicitly supports image views over array layers and cube arrays, and Khronos’s own samples show that shadowmapping is naturally modeled as a prior render pass with explicit `VkImageMemoryBarrier` synchronization into the main pass, which also makes multithreaded command recording a sensible optimization path. That is extremely compatible with a future KEX-like “Shadow Gen” style backend, but only if WORR first centralizes shadow-view creation and dirty-list emission in one renderer-neutral module. fileciteturn30file0L1-L1 fileciteturn31file0L1-L1 citeturn9view3turn9view4turn9view5

## Checklisted implementation plan

**Freeze the contract before changing any more code.** The repo contains contradictory shadow documents, and the 2026-02-16 rebuild note is clearly intended to supersede the same-day fallback/cache-expansion experiment. Treat that rebuild as the current source of truth unless the live code proves otherwise, and explicitly blacklist reintroduction of the removed fallback stack. fileciteturn18file0L1-L1 fileciteturn38file0L1-L1

- [ ] Create a single `docs-dev/renderer/shadowmapping-replacement-baseline.md` that states the canonical contract.
- [ ] Inventory every current shadow cvar and classify it as **keep**, **rename**, **compat alias**, or **delete**.
- [ ] Remove or archive abandoned cvars and paths such as no-slot unshadowed fallback, sticky slot retention, and any cache residency logic that is coupled to active-slot order.
- [ ] Add a build-time grep/regression check that fails if removed cvars or removed fallback paths reappear.
- [ ] Add a runtime assert that no shadow pass is allowed to mutate main-view `visframe` or main-view cluster cache state.

**Build a backend-neutral shadow frontend.** WORR already has the right gameplay-side light payload. The replacement should define a renderer-side contract instead of scattering shadow policy across `main.c`, `world.c`, `shader.c`, and backend glue. fileciteturn36file0L1-L1 fileciteturn48file0L1-L1

- [ ] Introduce `shadow_frontend.h/.c` or equivalent with these core records:
  - [ ] `shadow_light_desc_t`
  - [ ] `shadow_view_desc_t`
  - [ ] `shadow_caster_t`
  - [ ] `shadow_cache_key_t`
  - [ ] `shadow_page_id_t`
  - [ ] `shadow_backend_ops_t`
- [ ] Encode light classes explicitly:
  - [ ] point light = 6 views
  - [ ] cone/spot light = 1 view
  - [ ] sun = N cascades, separate scheduler
- [ ] Preserve rerelease semantics:
  - [ ] `RF_CASTSHADOW`
  - [ ] `RF_NOSHADOW`
  - [ ] `CS_SHADOWLIGHTS` format
  - [ ] explicit point/cone type, cone direction, cone angle, fade/style fields
- [ ] Add a shadow-only “tracked entity light” class for lights bound to actors or movers, mirroring the role KEX gives tracked entity shadow records.
- [ ] Keep all shadow feature decisions in the frontend; backends should only allocate/render/sample pages they were asked to.

**Replace visibility and touching with the right two-stage model.** The replacement should use camera-space visibility only for *candidate light relevance*, and light-space tests for *actual occluder drawing*. That distinction is the most important fix in the whole system. fileciteturn29file0L1-L1 fileciteturn22file0L1-L1 fileciteturn56file0L1-L1 fileciteturn57file0L1-L1

- [ ] Server-side:
  - [ ] Keep PHS for sound/beam style visibility only.
  - [ ] Keep a narrowly scoped PVS2 expansion for shadow-relevant entities.
  - [ ] Add a cvar or policy flag for multiplayer servers that want stricter replication behavior.
- [ ] Renderer-side candidate selection:
  - [ ] Cache `PVS2` masks by `(world_bsp, viewcluster1, viewcluster2)`.
  - [ ] Reject lights whose influence volume does not overlap that mask.
  - [ ] Reject lights blocked by area-bit rules only at the candidate stage.
  - [ ] Never perform a later camera-frustum-only kill that can override the candidate decision.
- [ ] Renderer-side caster collection:
  - [ ] Build a dedicated caster list every frame, independent from camera-visible entity lists.
  - [ ] Include brush models, alias models, skeletal models, and shadow-only local-player representation where applicable.
  - [ ] Exclude beams, sprites, and explicit `RF_NOSHADOW`.
- [ ] Update touching:
  - [ ] Store touched leaves/clusters per dynamic caster.
  - [ ] Store influence leaves/clusters per static light.
  - [ ] Use bitset overlap as the cheap reject.
  - [ ] Use sphere/AABB or cone/AABB as the precise confirm.
  - [ ] Dirty only overlapping static-light views, never all static lights globally.
- [ ] World occluder marking:
  - [ ] Mark BSP nodes/surfaces by light sphere, cone, or cascade frustum intersection.
  - [ ] Use dedicated shadow generation counters for marks.
  - [ ] Do not reuse `glr.visframe`, `node->visframe`, or main-view PVS stamps.

**Replace slot-churn caching with persistent view residency.** Selection and residency must become separate concepts. KEX does this at a larger scale with cached shadow-view blocks and an atlas; WORR should copy that separation even if it begins with fixed array pages instead of a full atlas. fileciteturn31file0L1-L1 fileciteturn32file0L1-L1 fileciteturn33file0L1-L1 fileciteturn18file0L1-L1

- [ ] Define a stable cache key per shadow view:
  - [ ] owner light identity
  - [ ] view type/face
  - [ ] projection parameters
  - [ ] resolution
  - [ ] filter family
  - [ ] world revision id
- [ ] Define cache modes cleanly:
  - [ ] mode 0 = no reuse
  - [ ] mode 1 = static reuse unless dynamic caster overlap dirties the view
  - [ ] mode 2 = world-only reuse
- [ ] Replace “active slot == cache slot” logic with:
  - [ ] persistent resident pages
  - [ ] active frame bindings pointing at those pages
- [ ] Use LRU only for residency eviction, not for light scoring.
- [ ] Ban unshadowed no-slot fallback entirely for the replacement baseline.
- [ ] Add optional page-pool growth policy, but do **not** build a KEX-style atlas until deterministic residency works with fixed pages.
- [ ] Add explicit cache dirtiness reasons for debugging:
  - [ ] moved caster
  - [ ] animated caster
  - [ ] changed light params
  - [ ] changed filter family
  - [ ] world BSP change
  - [ ] eviction

**Split the resource model into compare-depth and moment paths.** This is the biggest general improvement beyond simple bug fixing. It makes the common case cheaper and cleaner on both GL and Vulkan. fileciteturn42file0L1-L1 citeturn9view6turn9view7turn9view5

- [ ] Implement two storage families:
  - [ ] depth pages for hard compare, PCF, and PCSS
  - [ ] moment pages for VSM and EVSM
- [ ] Keep one shared cube-face transform helper for both APIs.
- [ ] Decide one cross-backend face-storage policy for v1:
  - [ ] either 2D-array face pages with manual face selection
  - [ ] or real cube/cube-array pages where available
- [ ] If using 2D-array emulation for parity, document that choice and keep it identical across GL/VK.
- [ ] Generate mip chains only for moment paths when softness/filtering needs them.
- [ ] Add per-platform format ladders:
  - [ ] GL: depth compare formats for compare path, RG16F/RG32F family for moments
  - [ ] VK: device-queried depth formats and color formats with explicit feature checks
- [ ] Record and expose which format/materialization path each backend actually selected.

**Ship separate GL and Vulkan backend adapters under the same frontend.** WORR currently has a compatibility bridge, not a native Vulkan implementation. Preserve that bridge, but make it temporary rather than architectural. fileciteturn30file0L1-L1 citeturn9view3turn9view4turn11view0

- [ ] GL backend:
  - [ ] use `glFramebufferTextureLayer` or equivalent layered attachment path
  - [ ] implement compare-sampler sampling for depth pages
  - [ ] implement moment sampling plus mipmaps for VSM/EVSM
  - [ ] keep backend-local resource binds out of selection logic
- [ ] Vulkan backend:
  - [ ] define `VkImage`/`VkImageView` page allocation model
  - [ ] use explicit barriers between shadow and main pass
  - [ ] support per-layer or cube-array views cleanly
  - [ ] support multithreaded command recording for shadow generation once correctness is stable
- [ ] `vk_rtx 0` bridge:
  - [ ] keep existing `vk_* -> gl_*` aliasing for compatibility
  - [ ] make it read/write the same frontend shadow policy, not a forked copy
  - [ ] remove backend-specific behavior drift from cvar handling

**Make quality deterministic and budgeted.** WORR already supports many shadow filters, but the replacement should turn them into a controlled product rather than a bag of options. fileciteturn53file0L1-L1 fileciteturn24file0L1-L1 citeturn8view0turn8view1turn4search3turn9view2turn10search5

- [ ] Light selection:
  - [ ] one deterministic score function
  - [ ] stable sort
  - [ ] explicit weights for intensity, distance, projected influence, and strict-PVS2 overlap
  - [ ] optional minimum budgets for static vs dynamic lights if needed
- [ ] PCSS:
  - [ ] enforce `gl_shadow_pcss_max_lights`
  - [ ] apply PCSS only to top-N lights
  - [ ] fallback all others to PCF or moment filtering
- [ ] Biasing:
  - [ ] ship nonzero slope bias defaults
  - [ ] ship nonzero normal offset defaults
  - [ ] apply bias scale consistently to local lights and sun cascades
- [ ] Sun shadows:
  - [ ] keep them on a separate budget from local lights
  - [ ] keep texel snapping and cascade stability
  - [ ] add proper cascade blending if the current path still lacks it
- [ ] Alpha/transparency:
  - [ ] v1: explicit no-shadow for translucent casters unless alpha-tested
  - [ ] v2: add alpha-tested caster mode
  - [ ] v3: consider weighted/opacity shadowing only if clearly worth the complexity
- [ ] Content compatibility:
  - [ ] keep projectile/explosion self-shadow suppression
  - [ ] add an optional model-path exclusion list similar to KEX’s `r_noEntityCastShadowList`

**Instrument everything and make debugging first-class.** WORR already added useful debug tools; the replacement should treat them as mandatory, because the history of this system shows that many bugs only become obvious when you can see which lights, pages, and casters the renderer thinks are active. fileciteturn61file0L1-L1

- [ ] Keep and extend live overlays for:
  - [ ] all candidate lights
  - [ ] selected lights
  - [ ] cones/cascades
  - [ ] caster bounds
  - [ ] cache residency/page ids
  - [ ] dirty reasons
- [ ] Add counters for:
  - [ ] candidate lights
  - [ ] selected lights
  - [ ] off-PVS2 rejects
  - [ ] area-bit rejects
  - [ ] dynamic caster overlap checks
  - [ ] dirtied views
  - [ ] reused views
  - [ ] evictions
  - [ ] per-backend GPU time
- [ ] Add “freeze selection” and “freeze dirtying” modes.
- [ ] Add one-shot dumps for a selected light, a selected page, and a selected caster overlap chain.
- [ ] Add golden repro maps and scripted demos for:
  - [ ] off-PVS light affecting visible space
  - [ ] moving bmodel near shadowlight
  - [ ] translated MD2 bounds
  - [ ] projectile self-shadow case
  - [ ] sun cascade shimmer case
  - [ ] HOM regression case

## Acceptance criteria and open questions

The replacement is done only when the following are all true: shadow passes do not touch main-view visibility state; off-camera but influence-overlapping lights and casters still affect visible surfaces through PVS2-aware candidate selection; dynamic caster movement dirties only overlapping static-light views instead of globally invalidating the cache; point, cone, and sun shadows all run through the same frontend contract; GL and native Vulkan backends pick the same shadow views for a frame given the same scene; and `vk_rtx 0` remains a compatibility bridge instead of a second, diverging shadow implementation. fileciteturn58file0L1-L1 fileciteturn22file0L1-L1 fileciteturn18file0L1-L1 fileciteturn30file0L1-L1

The highest-confidence open questions are small but important. First, for cross-backend parity, it is still a design choice whether v1 should stay with manual six-face 2D-array emulation or move to true cube/cube-array compare sampling where supported; the safest answer is “keep one representation across GL and Vulkan first, then optimize.” Second, the right multiplayer policy for shadow-aware `clientpvs2` expansion needs an explicit decision because the current docs already acknowledge the information/exposure tradeoff. Third, a KEX-like atlas may eventually be worth it, but only after deterministic page residency and dirtying are solid; implementing the atlas first would repeat the same “complexity before correctness” trap that the WORR rebuild note is already trying to escape. citeturn11view0turn9view4turn9view6 fileciteturn22file0L1-L1 fileciteturn31file0L1-L1 fileciteturn18file0L1-L1