# WORR RmlUi UI Migration Roadmap

Date: 2026-07-01

Status: Living roadmap for replacing the current menu/UI presentation stack with
RmlUi.

Primary tasks: `FR-09-T01`, `FR-09-T02`, `FR-09-T03`, `FR-09-T04`,
`FR-09-T05`, `FR-09-T06`, `FR-09-T07`, `FR-09-T08`, `FR-09-T09`,
`FR-09-T10`, `FR-03-T08`, `DV-03-T07`, `DV-04-T02`, `DV-07-T02`, and
`DV-07-T04`.

Supporting linked tasks: `DV-06-T01`, `FR-07-T01`, and `FR-07-T02`.

Execution status: `Active/round-75 menu-coverage-refinement accepted`.
Round 75 is the latest coordinator-accepted validation baseline. Earlier
parallel rounds produced source asset scaffolds, mock contracts, shared
theme/component contracts, smoke and route-contract checkers, package-asset
staging for loose `.install/basew/ui/rml/` assets, a guarded client
runtime-switch stub, shared `migration_phase` progression metadata, first
controller fixtures, and route-ownership metadata. Round 5 added a
dependency-free runtime document probe (`ui_rml_asset_root` and
`ui_rml_probe [route_id]`), promoted five selected shell/settings routes to
`controller_stub`, and added static RML semantics plus progress-report
validation tools. Round 6 expanded the client probe registry to all 57 tracked
routes plus `core.runtime_smoke`, added controller-contract validation,
runtime asset path/staged loose-file validation, JSON progress-report output,
and promoted five more shell/settings routes to `controller_stub`. Round 7
added reusable runtime registry drift and controller-stub coverage checks,
import-aware runtime asset validation, controller-contract progress reporting,
and promoted the remaining low-risk settings routes to `controller_stub`. The
Round 8 coordinator pass added menu-entrypoint and `runtime_stub` eligibility
validation, promoted exactly `main`, `game`, and `download_status` to
`runtime_stub`, added JSON output for runtime asset validation, and extended
progress reports with phase progression and route lists. The accepted migration
Round 9 coordinator pass added static navigation graph validation, controller
fixture validation, detailed runtime asset manifest output, a parity checklist
manifest/checker, progress reporting over all discovered route metadata files,
and promoted `addressbook`, `keys`, `legacykeys`, and `weapons` to
`controller_stub`. The accepted Round 10 coordinator pass added static command
and cvar inventory reporting, promoted `servers`, `demos`, `players`, and
`ui_list` to `controller_stub`, added parity checklist and command/cvar
inventory summary output to the progress reporter, and recorded a proposed
dependency decision/audit path for future first-class RmlUi integration. The
accepted Round 11 coordinator pass added static data-model/data-binding
inventory reporting, phase-consistency validation, dependency-decision
validation, progress-report data-model summaries, and promoted
`singleplayer`, `skill_select`, `loadgame`, and `savegame` to
`controller_stub`. The
accepted Round 12 coordinator pass promoted `downloads`, `quit_confirm`,
`gameflags`, and `startserver` to `controller_stub`, added starter metadata
coverage for the multiplayer hub plus all 25 session/match routes, added
condition-expression inventory validation, added route-metadata sync
validation, and extended progress reports with condition/metadata guardrail
summaries. The accepted Round 13 coordinator pass promoted `vote_menu`,
`callvote_main`, `callvote_ruleset`, `callvote_timelimit`,
`callvote_scorelimit`, `callvote_unlagged`, `callvote_random`, and
`callvote_map_flags` to `controller_stub`, added event/action inventory
validation, added accessibility/localization inventory validation, added a
legacy-removal inventory/checker, and extended progress reports with
event/a11y guardrail summaries. The accepted Round 14 coordinator pass
promoted `multiplayer`, `dm_welcome`, `dm_join`, `join`, `dm_hostinfo`, and
`dm_matchinfo` to `controller_stub`, added document/body identity inventory,
added route entrypoint inventory, added route metadata shape validation, and
extended progress reports with legacy-removal gate summaries. The
accepted Round 15 coordinator pass pinned upstream RmlUi `6.2` in
`subprojects/rmlui.wrap`, added the default-disabled optional `rmlui` Meson
feature gate, exposed dependency-free runtime availability/file/runtime-hook
boundaries, added dependency-integration validation, and revalidated package
staging. The accepted Round 16 coordinator pass promoted the final `12`
central starter routes to static `controller_stub`, added the strict
controller-stub completion checker, and reconciled feature metadata so all
`57` tracked routes now have static controller-contract coverage. The accepted
migration phase baseline is now `starter=0`, `controller_stub=54`, and
`runtime_stub=3`, with `57` advanced routes (`100.0%`) and `149`
controller-contract references. Round 17 added a guarded compiled RmlUi Core
adapter, RmlUi wrap provide aliases, explicit CMake fallback options, a
`renderer_unavailable` availability state, and runtime-adapter validation; an
enabled scratch build linked `rmlui_core.dll` and `worr_engine_x86_64.dll` in
`.tmp/rmlui/round17-rmlui-enabled3`. Round 18 installed WORR-backed RmlUi Core
system/file interfaces before `Rml::Initialise`, added the explicit
`ui_rml_runtime_probe` command, and validated that RmlUi file loads use
WORR filesystem APIs. Round 19 added the first native renderer bridge
contract: explicit OpenGL, Vulkan, and RTX/vkpt renderer-family lanes, an
opaque native render-interface hook, renderer registration/query helpers, and
validation that route availability remains gated by a native renderer without
Vulkan-to-OpenGL redirection. Round 20 added the first renderer-family
implementation scaffold: an OpenGL-owned `Rml::RenderInterface` object exported
from the OpenGL renderer DLL, client renderer lifecycle registration, adapter
installation through `Rml::SetRenderInterface`, and validation that the
scaffold remained guarded until it could draw. Round 21 turned that OpenGL
scaffold into a primitive bridge with geometry caching, tessellator drawing,
generated and loaded texture handling, scissor state, and `CanRender=true` for
the OpenGL renderer. Round 22 adds the first guarded runtime context path:
`core.runtime_smoke` can be opened with `ui_rml_runtime_open`, the compiled
runtime creates the `worr_ui` RmlUi context, loads and shows one document,
updates/resizes/renders it from `UI_Draw`, and closes it with Escape or
`ui_rml_runtime_close`. Round 23 adds guarded key/text/mouse delivery into the
sample RmlUi context, status counters, and `ui_rml_runtime_capture` as the
manual evidence path for the next OpenGL screenshot pass. Round 24 converts
that path into an automated guarded OpenGL TGA capture harness, adds a
local `r_screenshot_dir` evidence override, installs a temporary layout-only
RmlUi font engine, and styles `core.runtime_smoke` for visible nonblank
geometry. Round 25 replaces that layout-only adapter with a guarded smoke
bitmap font engine that emits RmlUi glyph quads, tightens the capture harness
to require the glyph-generation marker, and refreshes the smoke RCSS so the
captured route is legible. Round 26 adds TGA visual layout assertions to the
guarded capture harness, checking smoke-route color counts, bounding boxes,
and panel/text/button relationships. Round 27 adds a guarded synthetic
input/back-close pass to the same capture harness, requiring pointer, text,
mouse-wheel, mouse-button, close-request, close-counter, and inactive-status
evidence after the visual screenshot is written. Round 28 broadens the same
capture path into a two-viewport OpenGL matrix, setting explicit
`r_geometry` values for `960x720` and `1280x960`, validating exact screenshot
dimensions, and writing aggregate per-viewport evidence. Round 29 promotes the
three guarded `runtime_stub` menu entrypoints (`main`, `game`, and
`download_status`) into the same opt-in OpenGL runtime path, adds
`ui_rml_runtime_capture_menu`, and accepts a route matrix proving each route
opens through `UI_OpenMenu`, renders text, receives synthetic input, closes,
and reports inactive status at `960x720`. Round 30 adds a dedicated
renderer-family matrix guardrail that records OpenGL as the only current
guarded native RmlUi lane, keeps Vulkan and RTX/vkpt explicitly blocked until
native bridges exist, and fails Vulkan/RTX-to-OpenGL shortcut wiring. Round 31
connects that guardrail to the guarded OpenGL route-capture harness through
`--renderer-matrix`, producing one aggregate manifest with the OpenGL
`main`/`game`/`download_status` route evidence plus the blocked Vulkan and
RTX/vkpt lane facts. Round 32 adds a static Vulkan/RTX-vkpt bridge-readiness
audit that inventories the native renderer UI/image/draw foundations and keeps
both non-OpenGL lanes blocked until renderer-owned RmlUi bridges, family
exports, runtime dependencies, and non-null native interfaces exist. Round 33
connects that bridge-readiness audit to the `--renderer-matrix` aggregate
manifest, so the reviewed renderer evidence now includes OpenGL route
captures, renderer-family guardrails, and Vulkan/RTX foundation-plus-missing-
bridge facts together. Round 34 adds structured native bridge activation
requirements to those bridge-readiness and aggregate renderer manifests,
recording `8` required Vulkan/RTX activation items, `0` satisfied items, and
`8` pending items while keeping both non-OpenGL lanes blocked. Round 35 adds
activation status and next-blocker reporting for those lanes, recording
`activation_complete_lanes=0`, `partial_activation_lanes=0`, and
`inactive_activation_lanes=2`. Round 36 adds native bridge source-set
activation requirements, recording `10` required Vulkan/RTX activation items,
`0` satisfied items, and `10` pending items so class text cannot count unless
the target renderer DLL also compiles the bridge source. Round 37 wires the
shared bridge source into the Vulkan and RTX/vkpt renderer source sets in
inactive mode, recording `10` required activation items, `2` satisfied items,
and `8` pending items while keeping `native_bridge_lanes=0`. Round 38 adds
inactive Vulkan and RTX/vkpt `Rml::RenderInterface` class stubs, recording
`10` required activation items, `4` satisfied items, and `6` pending items
while keeping `native_bridge_lanes=0` and moving the next blocker to
`native_family_export_present`. Round 39 adds inactive non-OpenGL renderer
family exports for Vulkan and RTX/vkpt, recording `10` required activation
items, `6` satisfied items, and `4` pending items while keeping
`native_bridge_lanes=0` and moving the next blocker to
`runtime_dependency_enabled`. Round 40 wires the RmlUi runtime dependency into
the Vulkan and RTX/vkpt renderer DLL lanes in inactive mode, recording `10`
required activation items, `8` satisfied items, and `2` pending items while keeping
`native_bridge_lanes=0` and moving the next blocker to
`native_interface_export_present`. Round 41 fixes installed menu-route
document lifetime during runtime opens, refreshes `.install/`, and validates
that all `57` registered RmlUi routes open from the staged client without a
fresh crash dump or RmlUi parser/fallback/error log hits. Round 42 aligns the
OpenGL RmlUi context, mouse input, scissor conversion, mode-change updates, and
software cursor drawing with the renderer virtual UI canvas so window resizing
and fullscreen-style mode changes update active RmlUi menus correctly.
Round 43 adds SDL3_ttf-backed RmlUi font textures, refines shared menu layout
and visible fallback copy, wraps long utility keybind lists within the active
canvas, refreshes `.install/`, and validates final staged all-route OpenGL
loading plus a `960x720` keybind screenshot with TTF text. Round 44 pins the
RmlUi display, UI, and monospace TTF faces to Quake II Rerelease font assets
from the normal filesystem search path, adds static and runtime guardrails for
the rerelease font-source marker, refines dense Options/Admin/Start Server/
Deathmatch Flags layouts for the active `960x720` canvas, refreshes
`.install/`, and validates final staged all-route OpenGL loading with Quake II
Rerelease text-source evidence. Round 45 broadens the visual pass to `30`
representative routes, then bounds long settings forms, save/load slot lists,
and the in-game menu action list so Back/Close actions remain visible at
`960x720`; the final staged all-route OpenGL sweep still opens `58` unique
routes with Quake II Rerelease text-source evidence and no parser/error hits.
Round 46 follows that broad pass with targeted Single Player hub and generic
Session List refinements: selector/action widths are explicit, the `ui_list`
toolbar/list/footer stack is bounded at `960x720`, focused screenshots confirm
visible footer actions, and the final staged all-route OpenGL sweep still opens
`58` unique routes with no parser/error hits.
Round 47 refreshes the `30`-route representative screenshot pass and contains
the remaining long session/keybind surfaces: `admin_commands`,
`callvote_main`, `dm_join`, and `keys` now have bounded scroll/content regions
with visible footer actions at `960x720`, while the final staged all-route
OpenGL sweep still opens `58` unique routes with Quake II Rerelease
font-source evidence and no parser/error hits.
Round 48 refines settings/local-host forms under the same staged OpenGL path:
binary settings now render as compact square toggles, `performance`, `sound`,
`downloads`, and `startserver` focused screenshots end on complete rows with
visible Back/Close actions, and the final staged all-route OpenGL sweep still
opens `58` unique routes with Quake II Rerelease font-source evidence and no
parser/error hits.
Round 49 adds conservative RmlUi-native color/border transitions, header/footer
framing, and hover/focus treatments across the shared shell, settings,
session, and utility surfaces; focused screenshots validate representative
shell/settings/session/utility routes, and the final staged all-route OpenGL
sweep still opens `58` unique routes with Quake II Rerelease font-source
evidence and no parser/transition/error hits.
Round 50 fixes RmlUi viewport anchoring and scale coordination for windowed,
widescreen, tall, and fullscreen-style sizes: the runtime now uses a `960x720`
reference canvas, the OpenGL draw scale is set before RmlUi rendering and
cursor drawing, `body`/`.screen` fill the RmlUi viewport, the main-menu action
column has a stable width, and the final staged all-route OpenGL sweep still
opens `58` unique routes with Quake II Rerelease font-source evidence and no
parser/transition/error hits.
Round 51 refines typed settings widgets and non-main menu layout: shared
settings rows now use stable label/control/value columns, toggles, ranges,
selects, combo boxes, text/numeric fields, image-value selectors, and progress
rows get control-specific widths, utility text fields no longer collapse when
empty, `download_status` imports the settings control contract, and the final
staged all-route OpenGL sweep still opens `58` unique routes with Quake II
Rerelease font-source evidence and no parser/transition/error hits.
Round 52 refines command navigation and settings density: shell command
controls now use real buttons, Options/Game/Single Player/save-load/
multiplayer/session menus use deterministic two-column command grids on a
`604px` slab, shared settings rows are narrowed to the same contract, and the
final staged all-route OpenGL sweep still opens `58` unique routes with Quake
II Rerelease font-source evidence and no parser/transition/error hits.
Round 53 polishes those command grids into spaced tiles with slimmer heights,
rounded frames, dark fills, and hover/focus left accents, while shared settings
rows get denser rounded row frames; representative `960x720` captures and the
final staged all-route OpenGL sweep still open `58` unique routes with Quake
II Rerelease font-source evidence and no parser/transition/error hits.
Round 54 tightens action intent and widget semantics: remaining pseudo-button
commands become real buttons, primary/destructive actions now have consistent
green/red filled treatments, high-specificity shell/session grids preserve
those intent states, and utility/player/quit confirmation surfaces are cleaner
without changing the accepted `604px` layout contract; focused captures and the
final staged all-route OpenGL sweep still open `58` unique routes with Quake II
Rerelease font-source evidence and no parser/transition/error hits.
Round 55 adds a first-class RmlUi popup route command for confirmation menus,
routes Quit/Forfeit/Leave Match/Replay confirmations through compact popup
documents, maps RmlUi menu feedback sounds to the legacy menu samples, and
reworks Sound Settings into a two-column typed-widget page with menu music
controls; focused captures, popup-command validation, and the final staged
all-route OpenGL sweep still open `58` unique routes with Quake II Rerelease
font-source evidence and no parser/transition/error hits.
Round 56 consumes `data-menu-music="menu"` in the compiled runtime through the
existing OGG playback path, adds music intent to high-level hub routes, and
routes the in-game Game menu Quit action through the same popup confirmation
as Main Quit; focused captures, popup/music validation, and the final staged
all-route OpenGL sweep still open `58` unique routes with Quake II Rerelease
font-source evidence and no parser/transition/error hits.
Round 57 consumes open-sound metadata and focus/change menu feedback in the
compiled runtime, while Round 58 bridges both client and cgame `pushmenu`
producers into deterministic RmlUi route opens and popup route opens; focused
staged OpenGL probes now prove `pushmenu options` opens the RmlUi Options
route, and Quit/Forfeit/Leave Match/Tournament Replay confirmations use the
RmlUi popup path with alert open sounds and menu music cues.
Round 59 refines the RmlUi Multiplayer hub against the original pre-RmlUi menu:
q2servers.com browsing, address-book browsing, demo browsing, Start Server
setup defaults, Player Setup, and Options now use legacy command strings from
the staged shell layout, with the dead `multiplayer.connect_address` command
removed and `pushmenu multiplayer` validated through RmlUi at `960x720`.
Round 60 refines Video Setup against the original pre-RmlUi settings menu:
the RmlUi page now restores the three-state borderless mode, Multi-Monitor
action, anti-aliasing, hardware gamma, anisotropic filtering, texture
saturation/intensity, lightmap saturation/brightness, and renderer backend
controls using typed widgets in a compact three-column layout that fits the
`960x720` canvas with TTF font, open-sound, and menu-music evidence.
Round 61 normalizes the settings family so all settings routes request menu
music and open-sound cues, converts Screen/Crosshair and Effects/Railgun Trail
navigation into typed action rows, and moves Screen Setup plus Effects Setup to
compact two-column layouts with final `960x720` captures proving footer
containment.
Round 62 applies the same audio contract to the single-player/local-session
routes, adds confirm/open cues to decisive Skill Select and Start Server
actions, and reshapes Start Server into a three-column static-fallback layout
that keeps Server, Match Setup, Rules, and footer controls visible at
`960x720`.
Round 63 normalizes the utility route family with menu music/open-sound
metadata, intent-specific action sounds, capture-action semantics for keybind
surfaces, and bounded Address Book, Key Bindings, and Weapon Bindings layouts;
all eight utility routes now have staged `pushmenu` OpenGL evidence with
Quake II Rerelease TTF markers at `960x720`.
Round 64 brings session/match routes into the shared menu audio contract,
preserves dynamic `worr_*` command publication before RmlUi routing, and keeps
representative session/lobby/callvote surfaces bounded at `960x720`.
Round 65 groups Options, Game, and Multiplayer into modern hub sections,
normalizes explicit action sounds across authored route buttons, and keeps
Quit confirmation on the popup route path. Round 66 refines the shared popup
visual treatment, loosens fixed panel minimums, converts additional fixed
menu containers from hidden overflow to contained scroll overflow, and extends
explicit menu audio intent into reusable RmlUi component templates. Round 67
adds a shared compiled-runtime bridge for `data-cvar` form controls,
`data-bind-cvar`, `data-label-cvar`, and `data-bind="cvars.*"` text, plus
`data-visible-if`/`data-enable-if` condition evaluation; settings widgets now
show cvar-backed value badges and typed control accents, and DM Join proves
cvar-driven labels/visibility with a bounded session command grid. Round 68
adds cvar-driven meter/value badges for range and progress-style controls,
converts Video, Sound, Screen, Crosshair, Rail Trail, and Download Status
meter surfaces, and lays out Crosshair as two bounded columns so its
pre-RmlUi Crosshair and Hit Feedback controls remain visible above the footer.
Round 69 adds first-party SVG UX icon assets, OpenGL renderer-side SVG subset
rasterization, shared icon-button styling, and icon integration across Main,
Options, Game, Multiplayer, Single Player, and Quit confirmation popup
surfaces; full RmlUi SVG plugin/LunaSVG support and native Vulkan/RTX-vkpt SVG
texture upload remain pending. Round 70 supersedes the visible command-icon
treatment by removing high-level menu pictograms, deleting the
`common/icons/ux` source asset set, adding a widget-only SVG asset library,
and integrating `130` compact widget markers across settings and utility
fields while keeping Main menu commands plain again. Round 71 adds a
state-aware SVG skin library for real widget surfaces, wiring button,
primary/destructive button, text box, combo/drop-down, checkbox, arrow box,
range, progress, scrollbar, and popup-frame assets into the shared RmlUi
themes while preserving color/border fallbacks.
Native Vulkan/RTX-vkpt renderer
implementations, live data controllers, localization/text shaping parity,
broad input/navigation parity, automated runtime navigation, full theme/layout
parity, parity proof, and legacy-removal cutover remain incomplete.

Strategic parent:
`docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`.

Related current-system docs:
- `docs-dev/cgame-ui-json.md`
- `docs-dev/cgame-ui-port.md`
- `docs-dev/menu-migration-legacy-to-cgame.md`

## Task Status and Progression

Status legend:
- `Planned`: scoped here, but no accepted implementation evidence is recorded
  in this roadmap yet.
- `Active`: implementation or validation work is underway.
- `Blocked`: waiting on a named dependency or gate.
- `Ready for validation`: implementation exists, but gate evidence is not
  accepted yet.
- `Done`: completion evidence is recorded and the canonical roadmap can be
  updated.

| Task | Owner | Current status | Progression path | Completion evidence |
|---|---|---|---|---|
| `FR-09-T01` Runtime ownership, inventory, asset layout, and cutover policy | S0, Agent 1 | `Active`: route and ownership manifests seeded; route-ownership metadata, full-route probe coverage, and selected route progression through three guarded menu-entrypoint `runtime_stub` routes accepted; S0 not closed | S0 inventory -> route/data/command contracts -> ownership metadata reconciliation -> full-route probe/route progression evidence -> guarded entrypoint evidence -> Gate G0 | Ownership decision, asset layout, staging plan, coexistence policy, and full menu ownership manifest. |
| `FR-09-T02` RmlUi dependency, Meson/build wiring, and staging | Agent 1 | `Active`: loose `ui/rml` package staging implemented, client scaffold source wired, full-route runtime document probe, registry drift, runtime asset/import, JSON runtime-asset reporting, detailed runtime asset manifest output, staged loose validation, proposed dependency decision/audit record, RmlUi `6.2` source wrap, default-disabled optional `rmlui` Meson feature gate, dependency-integration checker state `optional`, wrap provide aliases, explicit CMake fallback options, enabled scratch compile/link proof, RmlUi runtime DLL staging, and refreshed `.install` route-load proof accepted; supported build matrix pending | Gate G0 -> full-route runtime document probe accepted -> runtime registry/import validation -> runtime asset JSON/staging/manifest evidence -> dependency decision/audit -> dependency integration -> optional build gate -> runtime-switch/build wiring -> install refresh -> Gate G1 | RmlUi dependency resolves, links in the supported build matrix, and `.install/basew/ui/rml/` refreshes with current RmlUi assets. |
| `FR-09-T03` Runtime bootstrap and native renderer integration | Agents 1 and 2 | `Active`: runtime smoke document, native renderer guardrails, guarded `ui_rml_enable` switch scaffold, filesystem-backed probe coverage for 57 route documents plus `core.runtime_smoke`, guarded menu-open probes for `main`, `game`, and `download_status`, runtime availability reporting, dependency-free file-interface boundary, runtime-hook boundary, compiled RmlUi Core adapter registration, WORR-backed RmlUi system/file interfaces, explicit `ui_rml_runtime_probe`, native renderer bridge contract/family lanes, OpenGL render-interface scaffold export/registration, OpenGL geometry/texture/scissor primitive bridge with `CanRender=true`, guarded `core.runtime_smoke` context open/update/render path, `ui_rml_runtime_open`/`ui_rml_runtime_close`, UI draw interception, guarded key/text/mouse delivery, runtime counters, `ui_rml_runtime_capture`, smoke bitmap glyph font path, styled smoke RCSS, local screenshot-dir override, automated guarded OpenGL TGA capture with glyph marker and layout assertions, guarded synthetic input/back-close capture evidence, two-viewport OpenGL matrix evidence, guarded `main`/`game`/`download_status` menu-route OpenGL matrix evidence, explicit renderer-family matrix guardrails, aggregate renderer-matrix capture manifests, native Vulkan/RTX bridge-readiness audit, bridge-readiness aggregate renderer manifests, native bridge activation checklist manifests, native bridge activation status reporting, native bridge source-set activation requirements, inactive Vulkan/RTX bridge source wiring, inactive Vulkan/RTX bridge class stubs, inactive Vulkan/RTX family exports, inactive Vulkan/RTX runtime dependencies, installed OpenGL full-route menu-load proof accepted, focused OpenGL navigation-layout proof accepted, deterministic client/cgame `pushmenu` to RmlUi route/popup proof accepted, focused `pushmenu multiplayer` OpenGL proof accepted, and focused SVG texture generation proof accepted; Vulkan/RTX-vkpt implementations, full font/text services, full input services, runtime navigation, responsive widescreen parity, theme/layout parity, and parity proof pending | Gate G0 -> runtime-switch scaffold -> full-route document probe -> guarded menu-entrypoint runtime stubs -> runtime/file interface boundary -> compiled core adapter -> system/file bridge -> native renderer contract -> OpenGL render-interface scaffold -> OpenGL render primitives -> guarded sample route/context draw proof -> guarded sample input/capture proof -> guarded sample screenshot/glyph/layout proof -> guarded synthetic input/back-close proof -> guarded viewport matrix proof -> guarded menu route proof -> renderer-family guardrail -> renderer-matrix capture manifest -> bridge-readiness audit -> bridge-readiness renderer manifest -> native bridge activation checklist -> native bridge activation status -> native bridge source-set activation -> inactive non-OpenGL bridge source wiring -> inactive non-OpenGL bridge class stubs -> inactive non-OpenGL family exports -> inactive non-OpenGL runtime dependencies -> installed route-load proof -> normal pushmenu bridge proof -> multiplayer hub route proof -> SVG texture proof -> native renderer matrix proof -> Gate G1 | Sample `.rml` opens from normal menu entry points in OpenGL, Vulkan, and RTX/vkpt without Vulkan-to-OpenGL fallback. |
| `FR-09-T04` Fonts, localization, theme, cursor/audio, and accessibility | Agent 2, Agent 4 consumer | `Active`: base, utility, session, and accessibility themes seeded; linked shell/settings/single-player/session/accessibility themes now avoid RmlUi-rejected browser CSS in installed route-load validation; all low-risk settings routes have `controller_stub` metadata; a11y/localization inventory reports 8 static refs and 6 localization keys with 0 malformed hooks; guarded smoke bitmap glyph path validates first text geometry; OpenGL path draws a software cursor; SDL3_ttf-backed RmlUi text textures now validate on staged menu routes; shared settings/navigation surfaces now use the `604px` typed-widget/menu contract; menu music, open-sound, and focus/change feedback plumbing validates through staged RmlUi routes; first-party SVG widget marker styling validates across settings and utility forms after command-menu pictograms were removed; live localization/accessibility services and final shaping policy pending | Gate G1 -> theme/font/input services -> a11y/localization inventory -> content consumption -> widget asset styling -> live localization/a11y services -> Gate G2 | Stable theme/font/input/accessibility services plus at least one migrated page using them. |
| `FR-09-T05` Reusable data-model and controller bridges | Agent 3 | `Active`: mock contracts, route-contract audit, shared components, controller fixtures, 54 accepted `controller_stub` routes, 3 guarded `runtime_stub` routes, 149 controller-contract references across all 57 advanced routes, command/cvar/data-model/condition/event inventory validation, metadata-sync validation, phase-consistency validation, controller fixture validation, controller-stub coverage, route metadata shape validation, runtime-stub eligibility validation, and controller-stub completion validation landed; live C++ controllers pending | mock contracts -> controller fixture reconciliation -> selected `controller_stub` routes accepted -> guarded `runtime_stub` eligibility -> utility/list/single-player/local-session/session-vote/lobby/final session controller-stub metadata -> cvar/command/condition/keybind/list/save-load/session bridges -> controller-stub completion gate -> live C++ controllers -> Gate G2 | One cvar control, one command button, one conditional element, one list/table, and one preview component validated through RmlUi. |
| `FR-09-T06` Shell/settings/single-player menu translation | Agent 4 | `Active`: all 23 Agent 4 source-route starter documents landed; all Agent 4-owned shell/settings/single-player routes now have either `controller_stub` metadata (`20`) or guarded `runtime_stub` metadata (`3`); runtime/parity pending | Agent 4 documents -> smoke manifest -> controller-stub batches -> runtime-stub entrypoint batch -> single-player controller-stub batch -> remaining local-session controller-stub batch -> parity checks -> Gate G3 | Agent 4-owned Wave A and single-player Wave B routes run through RmlUi with settings persistence and back/escape parity. |
| `FR-09-T07` Browser, player-config, save/load, keybind, and utility surfaces | Agents 3, 4, and 5 | `Active`: all tracked rich utility and save/load starter documents landed; `addressbook`, `keys`, `legacykeys`, `weapons`, `servers`, `demos`, `players`, `ui_list`, `loadgame`, `savegame`, and `downloads` have `controller_stub` metadata; utility/keybind visible copy and long-list layout now have staged TTF screenshot proof; live controllers/parity pending | shared rich components -> utility documents -> utility controller-stub metadata -> utility/list controller-stub metadata -> save/load controller-stub metadata -> download-options controller-stub metadata -> long-list layout proof -> smoke/parity checks -> Gate G3 | Servers, demos, players, ui_list, keybind, addressbook, weapons, loadgame, and savegame routes pass parity checks. |
| `FR-09-T08` Multiplayer/session/match menu translation | Agent 5 | `Active`: all tracked Wave C source-route starter documents landed; multiplayer/session route metadata covers the multiplayer hub plus all 25 session/match routes; all non-runtime multiplayer/session/vote/tournament/MyMap/map selector/match-stats routes now have static `controller_stub` metadata; visible fallback copy has been refined away from migration/debug wording; the Multiplayer hub now mirrors original q2servers/address-book/demos/start-server/player/options commands in a staged shell grid; live session behavior/parity pending | session data contract -> Wave C documents -> session route metadata -> vote/callvote controller-stub metadata -> multiplayer/lobby controller-stub metadata -> tournament/MyMap/map-selector/match-stats controller-stub metadata -> player-facing fallback copy -> multiplayer hub parity proof -> live match-state smoke -> Gate G3 | Vote, tournament, MyMap, forfeit, replay, map selector, match info, and match stats flows run through RmlUi. |
| `FR-09-T09` Migration-specific validation | Agent 5 plus all agents | `Active`: smoke, route-contract, command inventory, cvar inventory, data-model inventory, condition inventory, event inventory, a11y/localization inventory, document-id inventory, entrypoint inventory, route-metadata-shape validation, legacy-removal inventory/reporting, metadata sync, phase-consistency, dependency-decision, dependency-integration, runtime-adapter/system-file/renderer-contract/OpenGL-scaffold/OpenGL-primitive/context/input-capture/glyph-font validation, runtime capture harness glyph-marker, TGA layout, synthetic input/back-close, viewport-matrix validation, menu-route-matrix validation, renderer-family matrix guardrail validation, aggregate renderer-matrix capture validation, Vulkan/RTX bridge-readiness validation, bridge-readiness aggregate manifest validation, native bridge activation checklist validation, native bridge activation status validation, native bridge source-set activation validation, inactive non-OpenGL bridge source wiring validation, and inactive non-OpenGL bridge class-stub validation, inactive non-OpenGL family-export validation, inactive non-OpenGL runtime-dependency validation, installed 57-route load validation, focused navigation-layout capture evidence, deterministic `pushmenu` route/popup validation, focused SVG widget marker validation, controller fixture, controller-stub coverage, controller-stub completion, runtime-stub eligibility, menu-entrypoint, static RML semantics, navigation graph, runtime registry, import-aware runtime asset JSON/manifest, parity checklist/summary, and progress-report tools validate 57/57 source routes with `starter=0`, `controller_stub=54`, and `runtime_stub=3`; runtime navigation/broader input parity coverage pending | smoke manifest -> migration-phase metadata -> static semantics checks -> runtime registry/import checks -> menu-entrypoint/runtime-stub checks -> navigation graph/fixture/parity checks -> command/cvar/data-model/condition/event/a11y/document-id/entrypoint inventories -> metadata/shape/phase/dependency/dependency-integration/legacy guardrails -> controller-stub completion gate -> runtime-adapter/system-file/renderer-contract/OpenGL-scaffold/OpenGL-primitive/context/input-capture/glyph-font checks -> guarded screenshot/glyph/layout/input/viewport/menu-route harness -> renderer-family guardrail -> renderer-matrix capture manifest -> bridge-readiness audit -> bridge-readiness renderer manifest -> native bridge activation checklist -> native bridge activation status -> native bridge source-set activation -> inactive non-OpenGL bridge source wiring -> inactive non-OpenGL bridge class stubs -> inactive non-OpenGL family exports -> inactive non-OpenGL runtime dependencies -> progress reports -> document-load checks -> normal pushmenu probes -> SVG widget probes -> renderer/layout/input/session evidence -> Gate G4 | Automated smoke coverage and manual parity checklist cover all migrated routes and selected renderer/layout matrix. |
| `FR-09-T10` Legacy JSON removal and final docs/staging cleanup | Agent 5 plus all agents | `Blocked`: waits on Gate G3/G4; legacy-removal inventory/checker tracks 6 items with 4 blocked, 2 pending, 0 ready, and 0 complete; progress reports surface the closed parity gate; controller-bindings parity is complete, but navigation, renderer, broad screenshot/input/back, and legacy-fallback evidence remain pending; no legacy removal attempted | legacy inventory -> progress gate summaries -> parity-ready evidence -> delete/archive -> docs/staging updates -> Gate G4 | Legacy JSON loader/widgets and dead assets are removed or intentionally archived with a documented reason. |
| `FR-03-T08` Complete engine-side/cgame-side UI ownership split | S0, Agents 1 and 3 | `Active`: client-owned presentation/data-provider contract seeded; route ownership metadata, full-route probe registry, 54 selected `controller_stub` routes, 3 guarded menu-entrypoint `runtime_stub` routes, and all 57 central routes matched to feature metadata validated; ownership audit pending | ownership audit -> route ownership metadata -> selected route progression accepted -> guarded entrypoint route progression -> all static controller-stub progression -> data bridge contract -> Gate G0/G2 | Client runtime ownership and cgame/sgame data-provider boundaries are explicit and documented. |
| `DV-03-T07` UI automation harness | Agent 5 | `Active`: manifest, route-contract, command inventory, cvar inventory, data-model inventory, condition inventory, event inventory, a11y/localization inventory, document-id inventory, entrypoint inventory, route-metadata-shape validation, legacy-removal inventory/reporting, metadata sync, phase-consistency, dependency-decision, dependency-integration, runtime-adapter/system-file/renderer-contract/OpenGL-scaffold/OpenGL-primitive/context/input-capture/glyph-font checks, guarded runtime capture harness with glyph marker, layout assertions, synthetic input/back-close counters, exact geometry validation, viewport-matrix manifest output, menu-route-matrix manifest output, renderer-family matrix guardrail output, aggregate renderer-matrix manifest output, Vulkan/RTX bridge-readiness audit output, bridge-readiness renderer-manifest output, native bridge activation checklist output, native bridge activation status output, native bridge source-set activation output, inactive non-OpenGL bridge source wiring output, and inactive non-OpenGL bridge class-stub output, inactive non-OpenGL family-export output, inactive non-OpenGL runtime-dependency output, installed 57-route load output, controller fixture, controller-stub coverage, controller-stub completion, runtime-stub eligibility, menu-entrypoint, navigation graph, parity manifest/summary, import, phase, package, static semantics, runtime registry/asset text/JSON/manifest, and text/markdown/JSON progress-report checks landed; runtime navigation/broader input parity harness pending | manifest -> smoke-transition metadata -> static semantics checks -> runtime registry/import checks -> menu-entrypoint/runtime-stub checks -> navigation graph/fixture/parity checks -> command/cvar/data-model/condition/event/a11y/document-id/entrypoint inventories -> dependency/source/build/runtime-adapter/system-file/renderer-contract/OpenGL-scaffold/OpenGL-primitive/context/input-capture/glyph-font guardrails -> guarded screenshot/glyph/layout/input/viewport/menu-route smoke -> renderer-family guardrail -> renderer-matrix capture manifest -> bridge-readiness audit -> bridge-readiness renderer manifest -> native bridge activation checklist -> native bridge activation status -> native bridge source-set activation -> inactive non-OpenGL bridge source wiring -> inactive non-OpenGL bridge class stubs -> inactive non-OpenGL family exports -> inactive non-OpenGL runtime dependencies -> metadata/progress reports -> controller-stub completion -> load/navigation smoke -> broader screenshot/layout capture -> Gate G4 | Harness can prove document load, route navigation, renderer smoke, and session transition coverage. |
| `DV-04-T02` Reduce mixed ownership and refactor risk | S0, Agent 3 | `Active`: mock bridge boundaries, guarded client runtime switch, full-route probe registry, 54 selected `controller_stub` routes, 3 guarded menu-entrypoint `runtime_stub` ownership progression, all 57 central routes matched to feature metadata, and route metadata shape guardrails accepted; live bridge simplification pending | ownership contract -> runtime-switch/controller contracts -> selected `controller_stub` routes accepted -> guarded `runtime_stub` routes accepted -> all static controller-stub progression -> metadata-sync/shape coverage -> bridge simplification -> parity validation | New UI path uses narrow data/command bridges instead of recreating legacy ownership tangles. |
| `DV-07-T02` Visual/readability modernization support | Agent 2 | `Active`: readable base, utility, session, and accessibility theme hooks landed; utility/session fallback copy, keybind long-list layout, and first-party SVG widget markers are refined with staged screenshot evidence | theme tokens -> readable defaults -> long-list layout proof -> widget asset proof -> high-visibility checks -> Gate G2/G4 | Theme and readability policy are shared by all migrated documents and validated against long strings. |
| `DV-07-T04` Regression/parity hardening support | Agent 2 and Agent 5 | `Active`: full source-route manifest, route shape, asset import, loose staging, phase metadata, command/cvar/data-model/condition/event/a11y/document-id/entrypoint inventory, legacy-removal guardrails, metadata-sync/shape guardrails, phase-consistency guardrails, dependency-decision validation, dependency-integration validation, runtime-adapter/system-file/renderer-contract/OpenGL-scaffold/OpenGL-primitive/context/input-capture/glyph-font validation, guarded runtime screenshot/glyph/layout/input-back/viewport-matrix/menu-route evidence, renderer-family matrix guardrails, aggregate renderer-matrix evidence, Vulkan/RTX bridge-readiness evidence, bridge-readiness aggregate manifest evidence, native bridge activation checklist evidence, native bridge activation status evidence, native bridge source-set activation evidence, inactive non-OpenGL bridge source wiring evidence, inactive non-OpenGL bridge class-stub evidence, inactive non-OpenGL family-export evidence, inactive non-OpenGL runtime-dependency evidence, installed 57-route load evidence, focused SVG widget generation evidence, controller-contract/fixture/coverage/completion, runtime-stub eligibility, menu-entrypoint, navigation graph, parity checklist/summary, static semantics, runtime registry/asset JSON/manifest, and structured progress-report checks landed; broader parity evidence pending | renderer/layout checks -> migration metadata evidence -> static semantics/runtime registry/import/menu-entrypoint/navigation/parity/command/cvar/data-model/condition/event/a11y/document-id/entrypoint/dependency/legacy/metadata/progress reports -> controller-stub completion -> runtime-adapter/system-file/renderer-contract/OpenGL-scaffold/OpenGL-primitive/context/input-capture/glyph-font guardrails -> guarded screenshot/glyph/layout/input/viewport/menu-route evidence -> SVG widget evidence -> renderer-family guardrail -> renderer-matrix capture manifest -> bridge-readiness audit -> bridge-readiness renderer manifest -> native bridge activation checklist -> native bridge activation status -> native bridge source-set activation -> inactive non-OpenGL bridge source wiring -> inactive non-OpenGL bridge class stubs -> inactive non-OpenGL family exports -> inactive non-OpenGL runtime dependencies -> migration regression checklist -> Gate G4 | Manual and automated parity evidence covers renderer, layout, accessibility, and content stress cases. |
| `DV-06-T01` Dependency baseline audit | Agent 1 | `Active`: proposed RmlUi dependency decision/audit record, validation checker, upstream RmlUi `6.2` wrap URL/hash, license/provenance notes, wrap provide aliases, explicit CMake fallback options, enabled scratch compile/link proof, WORR-backed RmlUi file-interface proof, renderer-contract dependency boundary, OpenGL-scoped renderer scaffold/primitive/context/input-capture/glyph-font/layout/input-back/viewport/menu-route dependency wiring, renderer-family matrix dependency guardrails, aggregate renderer-matrix dependency evidence, native Vulkan/RTX bridge-readiness dependency boundary, aggregate bridge-readiness renderer-manifest evidence, native bridge activation checklist dependency boundary, native bridge activation status dependency boundary, native bridge source-set activation dependency boundary, inactive non-OpenGL bridge source wiring, inactive non-OpenGL bridge class stubs, inactive non-OpenGL family exports, and inactive non-OpenGL runtime dependencies accepted; final notice/update/local-patch/supported-matrix policy and full font service pending | RmlUi dependency review -> proposed decision record -> proposed/not-implemented guardrail -> accepted source/version/license audit -> optional Meson build gate -> vendoring/build-link decision -> system/file bridge -> renderer contract -> OpenGL-scoped renderer scaffold -> OpenGL primitive bridge -> guarded sample context -> guarded input/capture -> guarded screenshot/glyph/layout/input bootstrap -> viewport matrix -> guarded menu route matrix -> renderer-family guardrail -> renderer-matrix capture manifest -> bridge-readiness audit -> bridge-readiness renderer manifest -> native bridge activation checklist -> native bridge activation status -> native bridge source-set activation -> inactive non-OpenGL bridge source wiring -> inactive non-OpenGL bridge class stubs -> inactive non-OpenGL family exports -> inactive non-OpenGL runtime dependencies -> Gate G1 | Dependency choice is documented and accepted before first-class build integration lands. |
| `FR-07-T01` Map vote, MyMap, and nextmap validation scenarios | Agent 5 | `Active`: MyMap, map selector, and match stats now have static `controller_stub` hooks/metadata; live validation pending | session contract -> RmlUi match flows -> static MyMap/map-selector/match-stats controller-stub metadata -> session transition smoke -> Gate G3/G4 | RmlUi session documents preserve map vote, MyMap queue, and nextmap transition behavior. |
| `FR-07-T02` Tournament veto/replay flow hardening | Agent 5 | `Active`: tournament/veto/replay documents now have static `controller_stub` hooks/metadata; live validation pending | session contract -> tournament/replay documents -> static tournament/veto/replay controller-stub metadata -> match-state smoke -> Gate G3/G4 | RmlUi tournament veto and replay reset flows preserve current error handling and state reset behavior. |

### Round 1 Evidence (2026-07-02)

- Five parallel subagent lanes landed first-round source assets and logs:
  `docs-dev/rmlui-agent1-platform-runtime-round1-2026-07-02.md`,
  `docs-dev/rmlui-agent2-render-theme-accessibility-round1-2026-07-02.md`,
  `docs-dev/rmlui-agent3-data-components-round1-2026-07-02.md`,
  `docs-dev/rmlui-agent4-shell-settings-round1-2026-07-02.md`, and
  `docs-dev/rmlui-agent5-utility-session-smoke-round1-2026-07-02.md`.
- Source assets now exist under `assets/ui/rml/` for the core runtime smoke
  route, shared theme/components, mock data contracts, five shell/settings/
  single-player starter documents, five rich utility/multiplayer/session
  starter documents, and the first smoke manifest.
- `tools/ui_smoke/check_rmlui_manifest.py` validates `57` tracked migration
  surfaces: the `56` named JSON-menu baseline plus the code-driven/shared
  `ui_list` utility surface tracked for migration completeness.
- Latest first-round validation: `10/10` `required_now` documents present,
  `47` pending documents, all asset JSON parsed, all RML starter/import files
  parsed as XML-ish markup, and all local `href` imports resolved.

### Round 2 Evidence (2026-07-02)

- Five parallel subagent lanes landed second-round source assets and logs:
  `docs-dev/rmlui-agent1-packaging-staging-round2-2026-07-02.md`,
  `docs-dev/rmlui-agent2-theme-accessibility-round2-2026-07-02.md`,
  `docs-dev/rmlui-agent3-components-contracts-round2-2026-07-02.md`,
  `docs-dev/rmlui-agent4-shell-settings-round2-2026-07-02.md`, and
  `docs-dev/rmlui-agent5-utility-session-round2-2026-07-02.md`.
- `tools/package_assets.py` now mirrors `assets/ui/rml/` loose into
  `.install/<base-game>/ui/rml/` by default and validates archive/loose hashes
  for authored RmlUi assets. Focused package tests pass.
- Additional shared contracts now cover utility/session/accessibility theme
  hooks, keybind capture, image-grid selectors, and a first route-contract
  schema.
- Additional starter documents now cover the next Agent 4 shell/settings batch
  plus Agent 5 utility/session routes including address book, keybinds, weapon
  bindings, `ui_list`, callvote, MyMap, and leave-match confirmation.
- Latest round-two validation: `tools/ui_smoke/check_rmlui_manifest.py`
  reports `57` tracked migration surfaces, `30/30` `required_now` documents
  present, and `27` pending documents. JSON/RML/import validation passes, and
  packaging the current authored assets into `.tmp/rmlui/round2-package-validation`
  reports `67` validated RmlUi archive/loose file(s).

### Round 3 Evidence (2026-07-02)

- Five parallel subagent lanes landed third-round source assets and logs:
  `docs-dev/rmlui-agent1-smoke-harness-round3-2026-07-02.md`,
  `docs-dev/rmlui-agent2-route-contracts-round3-2026-07-02.md`,
  `docs-dev/rmlui-agent3-shell-singleplayer-round3-2026-07-02.md`,
  `docs-dev/rmlui-agent4-session-vote-admin-round3-2026-07-02.md`, and
  `docs-dev/rmlui-agent5-tournament-mapstats-round3-2026-07-02.md`.
- The central smoke manifest now marks all `57` tracked Wave A/B/C migration
  surfaces as `required_now`; every route has a starter `.rml` document.
- `tools/ui_smoke/check_rmlui_manifest.py` now validates present document XML,
  local `href` imports, duplicate route IDs, `.rml` document paths, and strict
  `required_now` booleans while still allowing non-required pending routes for
  future manifests.
- `tools/ui_smoke/check_rmlui_route_contracts.py` now audits route metadata
  across `assets/ui/rml/core/routes.json`, `assets/ui/rml/shell/routes.json`,
  and the central smoke manifest.
- Latest round-three validation: the smoke checker reports `57` total routes,
  `57` required routes, `57` present documents, `0` pending documents, `151`
  parsed RML/import files, and `213` local `href` imports checked. The route
  contract audit reports `57/57` central smoke documents present and `23/23`
  Agent 4 route documents present.

### Round 4 Evidence (2026-07-02)

- Five parallel subagent lanes landed fourth-round scaffold work and logs:
  `docs-dev/rmlui-agent1-runtime-switch-round4-2026-07-02.md`,
  `docs-dev/rmlui-agent2-smoke-transition-round4-2026-07-02.md`,
  `docs-dev/rmlui-agent3-controller-contracts-round4-2026-07-02.md`,
  `docs-dev/rmlui-agent4-route-ownership-round4-2026-07-02.md`, and
  `docs-dev/rmlui-agent5-docs-status-round4-2026-07-02.md`.
- `src/client/ui_rml/` now contains a dependency-free, guarded client runtime
  switch scaffold. It registers `ui_rml_enable` and `ui_rml_debug`, maps normal
  menu entry points to RmlUi route IDs, and falls back to the current cgame UI
  until a real RmlUi runtime is integrated. The coordinator also wired this
  scaffold through the active `src/client/ui_bridge.cpp` path.
- The smoke manifest now requires `migration_phase` metadata and marks all
  `57` tracked routes as `starter`; no route is promoted beyond `starter`
  until runtime/controller/parity evidence exists.
- Controller fixture contracts now cover cvar binding, command action,
  conditional state, route navigation/back behavior, and list/table data
  providers.
- Core and shell route manifests now carry route-ownership metadata for
  legacy/current surface names, source owners, task IDs, controller scope, and
  shared migration phases.
- `tools/ui_smoke/check_rmlui_route_contracts.py` now validates and reports the
  shared migration phases alongside owner/status/document checks.
- Coordinator integration log:
  `docs-dev/rmlui-parallel-round4-integration-2026-07-02.md`.
- Latest round-four validation: the smoke checker reports `57` total routes,
  `57` required routes, `57` present documents, `0` pending documents, and
  `Migration phases: starter=57`. The route-contract audit reports
  `starter=1` for core routes, `starter=23` for shell routes, and `starter=57`
  for the central smoke manifest. Focused package/manifest/route-contract tests
  pass, package staging validates `100` RmlUi archive/loose files, and the
  touched `ui_rml` and `ui_bridge` client objects compile. A broader Windows
  Meson target remains blocked by the existing `llvm-ar` regular-archive to
  thin-archive failure in static third-party/q2proto archive targets.

### Round 5 Evidence (2026-07-02)

Round 5 is accepted as another scaffold/validation round. It promotes only
source metadata and dependency-free runtime probing, not real RmlUi ownership,
rendering, live controller behavior, screenshots, parity, or legacy removal.

- Worker logs accepted:
  `docs-dev/rmlui-agent1-runtime-probe-round5-2026-07-02.md`,
  `docs-dev/rmlui-agent2-route-progression-round5-2026-07-02.md`,
  `docs-dev/rmlui-agent3-rml-semantics-round5-2026-07-02.md`,
  `docs-dev/rmlui-agent4-progress-report-round5-2026-07-02.md`, and
  `docs-dev/rmlui-agent5-docs-status-round5-2026-07-02.md`.
- Worker 1 added a dependency-free runtime document probe in
  `src/client/ui_rml/`. The scaffold now registers `ui_rml_asset_root` and
  `ui_rml_probe [route_id]`, resolves registered route documents under the
  runtime `ui/rml` asset root, probes them through `FS_LoadFileEx`, and keeps
  `UI_Rml_OpenMenu` returning `false` so the legacy UI remains authoritative.
- Worker 2 promoted exactly five shell/settings routes from `starter` to
  `controller_stub`: `main`, `game`, `options`, `video`, and
  `download_status`. All other tracked routes remain `starter`.
- Worker 3 added `tools/ui_smoke/check_rmlui_semantics.py` and focused tests.
  The checker validates static route targets, command element IDs, direct cvar
  references, and conservative cvar tokens inside supported condition
  attributes.
- Worker 4 added `tools/ui_smoke/report_rmlui_progress.py` and focused tests.
  The report summarizes total routes, document presence, required coverage,
  waves, owners, statuses, and migration phases, including `--format markdown`
  output for docs/status notes.
- Coordinator integration log:
  `docs-dev/rmlui-parallel-round5-integration-2026-07-02.md`.
- Coordinator validation accepted:
  - `python tools\ui_smoke\check_rmlui_manifest.py`: passed with `57` total
    routes, `57` required, `57` present, `0` pending, waves `A=21`, `B=11`,
    `C=25`, and migration phases `starter=52`, `controller_stub=5`.
  - `python tools\ui_smoke\check_rmlui_route_contracts.py`: passed with core
    `starter=1`, shell `starter=18`/`controller_stub=5`, and smoke
    `starter=52`/`controller_stub=5`.
  - `python tools\ui_smoke\check_rmlui_semantics.py`: passed with `57` routes
    known, `57` documents checked, `52` route targets checked, `289` command
    elements checked, and `452` cvar references checked.
  - `python tools\ui_smoke\report_rmlui_progress.py`: passed with `57/57`
    documents present and migration phases `starter=52`,
    `controller_stub=5`.
  - `python -m pytest tools/test_package_assets.py tools/ui_smoke/test_check_rmlui_manifest.py tools/ui_smoke/test_check_rmlui_route_contracts.py tools/ui_smoke/test_check_rmlui_semantics.py tools/ui_smoke/test_report_rmlui_progress.py`:
    passed with `34` tests.
  - `python tools\package_assets.py --assets-dir assets --install-dir .tmp\rmlui\round5-package-validation --base-game basew --archive-name pak0.pkz`:
    passed, packed `194` files from `assets`, and validated `31` botfile
    package/loose files plus `100` RmlUi package/loose files.
  - `ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj -v`:
    passed. The broader Windows Meson target remains subject to the existing
    `llvm-ar` regular-archive to thin-archive blocker recorded in Round 4.

### Round 6 Evidence (2026-07-02)

Round 6 is accepted as another scaffold/validation round. It expands route
probe and automation coverage, and it promotes a second conservative
shell/settings route batch to `controller_stub`; it still does not claim real
RmlUi runtime ownership, rendering, live controller behavior, screenshots,
parity, or legacy removal.

- Worker logs accepted:
  `docs-dev/rmlui-agent1-runtime-route-coverage-round6-2026-07-02.md`,
  `docs-dev/rmlui-agent2-controller-contract-validation-round6-2026-07-02.md`,
  `docs-dev/rmlui-agent3-runtime-assets-round6-2026-07-02.md`,
  `docs-dev/rmlui-agent4-route-progression-round6-2026-07-02.md`, and
  `docs-dev/rmlui-agent5-progress-json-round6-2026-07-02.md`.
- Worker 1 expanded the static client probe registry to `58` entries: all
  `57` smoke-manifest route IDs plus `core.runtime_smoke`. The route registry
  remains dependency-free and stores paths relative to `ui_rml_asset_root`.
- Worker 2 added controller contract reference validation to
  `tools/ui_smoke/check_rmlui_route_contracts.py`. The checker now validates
  optional `controller_contracts` metadata and reports reference counts.
- Worker 3 added `tools/ui_smoke/check_rmlui_runtime_assets.py`, which maps
  source route documents under `assets/ui/rml/` to runtime paths under
  `ui/rml/` and can validate staged loose files under an install root.
- Worker 4 promoted exactly five additional routes to `controller_stub`:
  `performance`, `accessibility`, `sound`, `screen`, and `input`. Together
  with the Round 5 batch, the accepted phase baseline is now `starter=47` and
  `controller_stub=10`.
- Worker 5 added `--format json` to
  `tools/ui_smoke/report_rmlui_progress.py`, while preserving text and
  markdown output.
- Coordinator integration log:
  `docs-dev/rmlui-parallel-round6-integration-2026-07-02.md`.
- Coordinator validation accepted:
  - Manifest/registry comparison: passed with `57` manifest routes, `58`
    registered routes, no missing manifest IDs, no unexpected extra IDs, and
    `core.runtime_smoke` present.
  - `python tools\ui_smoke\check_rmlui_manifest.py`: passed with `57` total
    routes, `57` required, `57` present, `0` pending, waves `A=21`, `B=11`,
    `C=25`, and migration phases `starter=47`, `controller_stub=10`.
  - `python tools\ui_smoke\check_rmlui_route_contracts.py`: passed with core
    `0` controller references/`starter=1`, shell `28` controller references
    with `controller_stub=10`/`starter=13`, and smoke
    `controller_stub=10`/`starter=47`.
  - `python tools\ui_smoke\check_rmlui_semantics.py`: passed with `57` routes
    known, `57` documents checked, `52` route targets checked, `289` command
    elements checked, and `452` cvar references checked.
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py`: passed with `57`
    routes checked, `57` source documents present, and `57` runtime paths
    derived.
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --install-dir .tmp\rmlui\round6-package-validation --base-game basew`:
    passed with `57` staged loose route document files present.
  - `python tools\ui_smoke\report_rmlui_progress.py`,
    `python tools\ui_smoke\report_rmlui_progress.py --format markdown`, and
    `python tools\ui_smoke\report_rmlui_progress.py --format json`: passed
    with `57/57` documents present and migration phases `starter=47`,
    `controller_stub=10`.
  - `python -m pytest tools/test_package_assets.py tools/ui_smoke/test_check_rmlui_manifest.py tools/ui_smoke/test_check_rmlui_route_contracts.py tools/ui_smoke/test_check_rmlui_semantics.py tools/ui_smoke/test_check_rmlui_runtime_assets.py tools/ui_smoke/test_report_rmlui_progress.py`:
    passed with `43` tests.
  - `python tools\package_assets.py --assets-dir assets --install-dir .tmp\rmlui\round6-package-validation --base-game basew --archive-name pak0.pkz`:
    passed, packed `194` files from `assets`, and validated `31` botfile
    package/loose files plus `100` RmlUi package/loose files.
  - `ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj -v`:
    passed. The broader Windows Meson target remains subject to the existing
    `llvm-ar` regular-archive to thin-archive blocker recorded in Round 4.

### Round 7 Evidence (2026-07-02)

Round 7 is accepted as another scaffold/validation round. It promotes the
remaining low-risk settings routes to `controller_stub`, adds registry and
controller-stub coverage checks, extends runtime asset checks to imported
RML/RCSS files, and exposes controller-contract facts in progress reports. It
still does not claim real RmlUi runtime ownership, rendering, live controller
behavior, screenshots, parity, or legacy removal.

- Worker logs accepted:
  `docs-dev/rmlui-agent1-runtime-registry-check-round7-2026-07-02.md`,
  `docs-dev/rmlui-agent2-controller-stub-coverage-round7-2026-07-02.md`,
  `docs-dev/rmlui-agent3-route-progression-round7-2026-07-02.md`,
  `docs-dev/rmlui-agent4-runtime-import-assets-round7-2026-07-02.md`, and
  `docs-dev/rmlui-agent5-progress-contracts-round7-2026-07-02.md`.
- Worker 1 added `tools/ui_smoke/check_rmlui_runtime_registry.py`, making the
  manifest-to-`ui_rml_routes` drift check a first-class smoke tool.
- Worker 2 added `tools/ui_smoke/check_rmlui_controller_stub_coverage.py`,
  which validates that `controller_stub` routes have matching shell metadata
  and contract categories for static navigation, command, cvar, and condition
  hooks.
- Worker 3 promoted exactly five additional settings routes to
  `controller_stub`: `multimonitor`, `railtrail`, `effects`, `crosshair`, and
  `language`. Together with the Round 5 and Round 6 batches, the accepted
  phase baseline is now `starter=42` and `controller_stub=15`.
- Worker 4 added `--include-imports` to
  `tools/ui_smoke/check_rmlui_runtime_assets.py`, enabling validation of local
  `.rml` and `.rcss` assets imported by route documents.
- Worker 5 added controller-contract summary facts to
  `tools/ui_smoke/report_rmlui_progress.py` text, markdown, and JSON output.
- Coordinator integration log:
  `docs-dev/rmlui-parallel-round7-integration-2026-07-02.md`.
- Coordinator validation accepted:
  - `python tools\ui_smoke\check_rmlui_manifest.py`: passed with `57` total
    routes, `57` required, `57` present, `0` pending, waves `A=21`, `B=11`,
    `C=25`, and migration phases `starter=42`, `controller_stub=15`.
  - `python tools\ui_smoke\check_rmlui_route_contracts.py`: passed with core
    `0` controller references/`starter=1`, shell `44` controller references
    with `controller_stub=15`/`starter=8`, and smoke
    `controller_stub=15`/`starter=42`.
  - `python tools\ui_smoke\check_rmlui_semantics.py`: passed with `57` routes
    known, `57` documents checked, `52` route targets checked, `289` command
    elements checked, and `452` cvar references checked.
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports`:
    passed with `57` route documents present, `16` imported assets present,
    and `73` runtime paths derived.
  - `python tools\ui_smoke\check_rmlui_runtime_registry.py`: passed with `57`
    manifest routes, `58` registered routes, `0` missing, `0` unexpected,
    `0` duplicates, and `57` matched runtime paths.
  - `python tools\ui_smoke\check_rmlui_controller_stub_coverage.py`: passed
    with `15` `controller_stub` routes checked and no missing inferred
    contract categories.
  - `python tools\ui_smoke\report_rmlui_progress.py`,
    `python tools\ui_smoke\report_rmlui_progress.py --format markdown`, and
    `python tools\ui_smoke\report_rmlui_progress.py --format json`: passed
    with `57/57` documents present, migration phases `starter=42`,
    `controller_stub=15`, and controller-contract facts showing `44`
    references across `15` routes.
  - `python -m pytest tools/test_package_assets.py tools/ui_smoke/test_check_rmlui_manifest.py tools/ui_smoke/test_check_rmlui_route_contracts.py tools/ui_smoke/test_check_rmlui_semantics.py tools/ui_smoke/test_check_rmlui_runtime_assets.py tools/ui_smoke/test_check_rmlui_runtime_registry.py tools/ui_smoke/test_check_rmlui_controller_stub_coverage.py tools/ui_smoke/test_report_rmlui_progress.py`:
    passed with `58` tests.
  - `python tools\package_assets.py --assets-dir assets --install-dir .tmp\rmlui\round7-package-validation --base-game basew --archive-name pak0.pkz`:
    passed, packed `194` files from `assets`, and validated `31` botfile
    package/loose files plus `100` RmlUi package/loose files.
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round7-package-validation --base-game basew`:
    passed with `73` staged loose route/import assets present and `0` missing.
  - `ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj -v`:
    passed. The broader Windows Meson target remains subject to the existing
    `llvm-ar` regular-archive to thin-archive blocker recorded in Round 4.

### Round 8 Evidence (2026-07-02)

Round 8 is accepted as another scaffold/validation round. It promotes only the
guarded menu-entrypoint document-probe surface for `main`, `game`, and
`download_status` to `runtime_stub`; it still does not claim first-class RmlUi
dependency integration, native renderer output, live controller behavior,
screenshots, parity, or legacy removal.

- Worker logs accepted:
  `docs-dev/rmlui-agent1-menu-entrypoints-round8-2026-07-02.md`,
  `docs-dev/rmlui-agent2-runtime-stub-eligibility-round8-2026-07-02.md`,
  `docs-dev/rmlui-agent3-runtime-stub-progression-round8-2026-07-02.md`,
  `docs-dev/rmlui-agent4-runtime-assets-json-round8-2026-07-02.md`, and
  `docs-dev/rmlui-agent5-progress-runtime-stub-round8-2026-07-02.md`.
- Worker 1 added `tools/ui_smoke/check_rmlui_menu_entrypoints.py`, making the
  `UI_Rml_RouteForMenu` mapping from normal menu entry points to route IDs a
  first-class smoke check.
- Worker 2 added `tools/ui_smoke/check_rmlui_runtime_stub_eligibility.py`,
  which verifies that every `runtime_stub` route is menu-mapped, present in the
  manifest and runtime registry, backed by shell route metadata, and covered by
  controller contracts while `UI_Rml_OpenMenu` still preserves legacy fallback.
- Worker 3 promoted exactly `main`, `game`, and `download_status` from
  `controller_stub` to `runtime_stub` in the central manifest and shell route
  metadata. The accepted phase baseline is now `starter=42`,
  `controller_stub=12`, and `runtime_stub=3`.
- Worker 4 added `--format text|json` to
  `tools/ui_smoke/check_rmlui_runtime_assets.py`, including structured success
  and error payloads for source documents, imported assets, runtime paths, and
  staged loose files.
- Worker 5 added phase-progression and `routes_by_phase` facts to
  `tools/ui_smoke/report_rmlui_progress.py` text, markdown, and JSON output.
- Coordinator integration log:
  `docs-dev/rmlui-parallel-round8-integration-2026-07-02.md`.
- Coordinator validation accepted:
  - `python tools\ui_smoke\check_rmlui_manifest.py`: passed with `57` total
    routes, `57` required, `57` present, `0` pending, waves `A=21`, `B=11`,
    `C=25`, and migration phases `starter=42`, `controller_stub=12`,
    `runtime_stub=3`.
  - `python tools\ui_smoke\check_rmlui_route_contracts.py`: passed with core
    `0` controller references/`starter=1`, shell `44` controller references
    with `starter=8`/`controller_stub=12`/`runtime_stub=3`, and smoke
    `starter=42`/`controller_stub=12`/`runtime_stub=3`.
  - `python tools\ui_smoke\check_rmlui_semantics.py`: passed with `57`
    documents checked, `52` route targets checked, `289` command elements
    checked, and `452` cvar references checked.
  - `python tools\ui_smoke\check_rmlui_menu_entrypoints.py`: passed with `5`
    menu cases checked, `4` mapped route cases, `3` unique mapped route IDs,
    and `3` manifest/registry matches.
  - `python tools\ui_smoke\check_rmlui_runtime_stub_eligibility.py`: passed
    with `3` `runtime_stub` routes checked, all `3` menu-mapped, all `3`
    matched in the runtime registry, and all `3` covered by controller
    contracts.
  - `python tools\ui_smoke\check_rmlui_controller_stub_coverage.py`: passed
    with `12` `controller_stub` routes checked and no missing inferred
    contract categories.
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports`
    and `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --format json`:
    passed with `57` source route documents present, `16` imported assets
    present, and `73` runtime paths derived.
  - `python tools\ui_smoke\check_rmlui_runtime_registry.py`: passed with `57`
    manifest routes, `58` registered routes, `0` missing, `0` unexpected,
    `0` duplicates, and `57` matched runtime paths.
  - `python tools\ui_smoke\report_rmlui_progress.py`,
    `python tools\ui_smoke\report_rmlui_progress.py --format markdown`, and
    `python tools\ui_smoke\report_rmlui_progress.py --format json`: passed
    with `57/57` documents present, migration phases `starter=42`,
    `controller_stub=12`, `runtime_stub=3`, and phase-progression facts
    showing `15` advanced routes (`26.3%`).
  - `python -m pytest tools/test_package_assets.py tools/ui_smoke/test_check_rmlui_manifest.py tools/ui_smoke/test_check_rmlui_route_contracts.py tools/ui_smoke/test_check_rmlui_semantics.py tools/ui_smoke/test_check_rmlui_runtime_assets.py tools/ui_smoke/test_check_rmlui_runtime_registry.py tools/ui_smoke/test_check_rmlui_controller_stub_coverage.py tools/ui_smoke/test_check_rmlui_menu_entrypoints.py tools/ui_smoke/test_check_rmlui_runtime_stub_eligibility.py tools/ui_smoke/test_report_rmlui_progress.py`:
    passed with `73` tests.
  - `python tools\package_assets.py --assets-dir assets --install-dir .tmp\rmlui\round8-package-validation --base-game basew --archive-name pak0.pkz`:
    passed, packed `194` files from `assets`, and validated `31` botfile
    package/loose files plus `100` RmlUi package/loose files.
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round8-package-validation --base-game basew`
    and the same command with `--format json`: passed with `73` staged loose
    route/import assets present, including all `16` imported assets, and `0`
    missing.
  - `ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj -v`:
    passed. The broader Windows Meson target remains subject to the existing
    `llvm-ar` regular-archive to thin-archive blocker recorded in Round 4.

### Round 9 Evidence (2026-07-02)

Round 9 is accepted as another scaffold/validation round. It promotes only
static utility-route controller metadata for `addressbook`, `keys`,
`legacykeys`, and `weapons`; it still does not claim first-class RmlUi
dependency integration, native renderer output, live controller behavior,
runtime navigation, screenshots, parity, or legacy removal.

- Worker logs accepted:
  `docs-dev/rmlui-agent1-navigation-graph-round9-2026-07-02.md`,
  `docs-dev/rmlui-agent2-controller-fixtures-round9-2026-07-02.md`,
  `docs-dev/rmlui-agent3-utility-controller-stubs-round9-2026-07-02.md`,
  `docs-dev/rmlui-agent4-runtime-asset-manifest-round9-2026-07-02.md`, and
  `docs-dev/rmlui-agent5-parity-manifest-round9-2026-07-02.md`.
- Worker 1 added `tools/ui_smoke/check_rmlui_navigation_graph.py`, which
  scans static `data-route-target` references and reports route graph facts.
  The accepted baseline has `57` routes, `52` route-target references, `50`
  unique directed edges, `0` unknown targets, `44` dead-end routes, and `27`
  routes unreachable from the guarded roots `main`, `game`, and
  `download_status`.
- Worker 2 added `tools/ui_smoke/check_rmlui_controller_fixtures.py`, which
  validates route `controller_contracts` references against mock fixture JSON
  files. The accepted baseline has `3` route metadata files, `28` metadata
  routes checked, `19` routes with controller contracts, `54` controller
  contract references, `5` unique fixtures referenced/present, and no malformed
  fixtures or refs.
- Worker 3 added `assets/ui/rml/utility/routes.json`, promoted exactly
  `addressbook`, `keys`, `legacykeys`, and `weapons` to `controller_stub`, and
  extended route-contract/controller-stub validation to discovered
  `assets/ui/rml/*/routes.json` metadata. The accepted phase baseline is now
  `starter=38`, `controller_stub=16`, and `runtime_stub=3`.
- Worker 4 added `--write-manifest <path>` to
  `tools/ui_smoke/check_rmlui_runtime_assets.py`, producing deterministic JSON
  with route/import source paths, runtime paths, source presence, and staged
  loose presence.
- Worker 5 added `tools/ui_smoke/rmlui_parity_manifest.json` and
  `tools/ui_smoke/check_rmlui_parity_manifest.py`. The checklist covers all
  `57` routes and `9` categories; it records `57` complete document-load
  entries, `19` complete controller-binding entries, `3` complete guarded
  legacy-fallback entries, and all real renderer/screenshot/input/runtime
  navigation categories as pending.
- Coordinator integration log:
  `docs-dev/rmlui-parallel-round9-integration-2026-07-02.md`.
- Coordinator validation accepted:
  - `python tools\ui_smoke\check_rmlui_manifest.py`: passed with `57` total
    routes, `57` required, `57` present, `0` pending, waves `A=21`, `B=11`,
    `C=25`, and migration phases `starter=38`, `controller_stub=16`,
    `runtime_stub=3`.
  - `python tools\ui_smoke\check_rmlui_route_contracts.py`: passed with core,
    shell, smoke, and utility route metadata checked; utility metadata reports
    `4` routes and `10` controller-contract references.
  - `python tools\ui_smoke\check_rmlui_controller_stub_coverage.py`: passed
    with `3` route metadata files checked, `16` `controller_stub` routes, and
    no missing inferred contract categories.
  - `python tools\ui_smoke\check_rmlui_controller_fixtures.py`: passed with
    `54` controller-contract refs across `19` routes and `5` fixtures present.
  - `python tools\ui_smoke\check_rmlui_navigation_graph.py`: passed with `0`
    unknown static route targets.
  - `python tools\ui_smoke\check_rmlui_parity_manifest.py`: passed with `57`
    route checklists, `9` categories, and `0` `parity_ready` routes.
  - `python tools\ui_smoke\check_rmlui_semantics.py`: passed with `57`
    documents checked, `52` route targets checked, `289` command elements
    checked, and `452` cvar references checked.
  - `python tools\ui_smoke\check_rmlui_menu_entrypoints.py`: passed with `5`
    menu cases checked, `4` mapped route cases, and `3` unique mapped route
    IDs.
  - `python tools\ui_smoke\check_rmlui_runtime_stub_eligibility.py`: passed
    with `3` `runtime_stub` routes checked, all menu-mapped, registry-matched,
    and controller-contract-matched.
  - `python tools\ui_smoke\check_rmlui_runtime_registry.py`: passed with `57`
    manifest routes, `58` registered routes, `0` missing, `0` unexpected,
    `0` duplicates, and `57` matched runtime paths.
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --write-manifest .tmp\rmlui\round9-runtime-assets.json`:
    passed with `57` source route documents present, `16` imported assets
    present, and `73` runtime paths derived.
  - `python tools\ui_smoke\report_rmlui_progress.py`,
    `python tools\ui_smoke\report_rmlui_progress.py --format markdown`, and
    `python tools\ui_smoke\report_rmlui_progress.py --format json`: passed
    with `57/57` documents present, migration phases `starter=38`,
    `controller_stub=16`, `runtime_stub=3`, `19` advanced routes (`33.3%`),
    and `54` controller-contract references across `19` routes.
  - `python -m pytest tools/test_package_assets.py tools/ui_smoke/test_check_rmlui_manifest.py tools/ui_smoke/test_check_rmlui_route_contracts.py tools/ui_smoke/test_check_rmlui_semantics.py tools/ui_smoke/test_check_rmlui_runtime_assets.py tools/ui_smoke/test_check_rmlui_runtime_registry.py tools/ui_smoke/test_check_rmlui_controller_stub_coverage.py tools/ui_smoke/test_check_rmlui_menu_entrypoints.py tools/ui_smoke/test_check_rmlui_runtime_stub_eligibility.py tools/ui_smoke/test_check_rmlui_navigation_graph.py tools/ui_smoke/test_check_rmlui_controller_fixtures.py tools/ui_smoke/test_check_rmlui_parity_manifest.py tools/ui_smoke/test_report_rmlui_progress.py`:
    passed with `94` tests.
  - `python tools\package_assets.py --assets-dir assets --install-dir .tmp\rmlui\round9-package-validation --base-game basew --archive-name pak0.pkz`:
    passed, packed `195` files from `assets`, and validated `31` botfile
    package/loose files plus `101` RmlUi package/loose files.
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round9-package-validation --base-game basew`
    and the same command with `--format json`: passed with `73` staged loose
    route/import assets present, including all `16` imported assets, and `0`
    missing.
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round9-package-validation --base-game basew --write-manifest .tmp\rmlui\round9-runtime-assets-staged.json`:
    passed and wrote staged manifest entries against
    `.tmp/rmlui/round9-package-validation`.
  - `ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj -v`:
    passed. The broader Windows Meson target remains subject to the existing
    `llvm-ar` regular-archive to thin-archive blocker recorded in Round 4.

### Round 10 Evidence (2026-07-02)

Round 10 is accepted as another scaffold/validation round. It promotes only
static utility/list controller metadata for `servers`, `demos`, `players`, and
`ui_list`; it also adds command/cvar inventory reporting, progress inventory
and parity summary output, and dependency decision planning. It still does not claim
first-class RmlUi dependency integration, native renderer output, live
controller behavior, runtime navigation, screenshots, parity, or legacy
removal.

- Worker logs accepted:
  `docs-dev/rmlui-agent1-command-inventory-round10-2026-07-02.md`,
  `docs-dev/rmlui-agent2-cvar-inventory-round10-2026-07-02.md`,
  `docs-dev/rmlui-agent3-utility-list-controller-stubs-round10-2026-07-02.md`,
  `docs-dev/rmlui-agent4-progress-inventory-round10-2026-07-02.md`,
  `docs-dev/rmlui-agent4-progress-parity-round10-2026-07-02.md`, and
  `docs-dev/rmlui-agent5-dependency-decision-round10-2026-07-02.md`.
- Worker 1 added `tools/ui_smoke/check_rmlui_command_inventory.py`, which
  inventories static `data-command` and `data-command-cvar` hooks across the
  authored route documents. The accepted baseline has `289` direct command
  references, `15` cvar-command references, `70` unique command tokens, `15`
  unique command-cvar references, all `57` routes with command hooks, and `0`
  malformed command attributes.
- Worker 2 added `tools/ui_smoke/check_rmlui_cvar_inventory.py`, which
  inventories `data-cvar`, `data-bind-cvar`, `data-label-cvar`,
  `data-command-cvar`, and condition-expression cvar references. The accepted
  baseline has `233` direct cvar refs, `54` label refs, `15` command refs,
  `150` condition refs, `452` total refs, `272` unique cvars, `37` routes with
  cvar hooks, and `0` unknown/bad tokens.
- Worker 3 expanded `assets/ui/rml/utility/routes.json`, promoted exactly
  `servers`, `demos`, `players`, and `ui_list` to `controller_stub`, and added
  utility/list controller contracts for list providers, command actions,
  cvar bindings, preview ownership, and condition state. The accepted phase
  baseline is now `starter=34`, `controller_stub=20`, and `runtime_stub=3`.
- Worker 4 extended `tools/ui_smoke/report_rmlui_progress.py` so text,
  markdown, and JSON reports include parity checklist summary counts when
  `tools/ui_smoke/rmlui_parity_manifest.json` is present, while keeping
  missing parity manifests non-fatal. The integration pass also added
  command/cvar inventory summaries to the same report, documented in
  `docs-dev/rmlui-agent4-progress-inventory-round10-2026-07-02.md`.
- Worker 5 added
  `docs-dev/rmlui-dependency-decision-record-2026-07-02.md`, a planning-only
  dependency decision record for future first-class RmlUi integration. The
  record proposes a Meson subproject/wrap preference with a reviewed vendored
  fallback, but does not add a dependency or change build files.
- Coordinator integration log:
  `docs-dev/rmlui-parallel-round10-integration-2026-07-02.md`.
- Coordinator validation accepted:
  - `python tools\ui_smoke\check_rmlui_manifest.py`: passed with `57` total
    routes, `57` required, `57` present, `0` pending, waves `A=21`, `B=11`,
    `C=25`, and migration phases `starter=34`, `controller_stub=20`,
    `runtime_stub=3`.
  - `python tools\ui_smoke\check_rmlui_route_contracts.py`: passed with core,
    shell, smoke, and utility route metadata checked; utility metadata reports
    `8` routes and `21` controller-contract references.
  - `python tools\ui_smoke\check_rmlui_controller_stub_coverage.py`: passed
    with `3` route metadata files checked, `20` `controller_stub` routes, and
    no missing inferred contract categories.
  - `python tools\ui_smoke\check_rmlui_controller_fixtures.py`: passed with
    `65` controller-contract refs across `23` routes and `7` fixtures present.
  - `python tools\ui_smoke\check_rmlui_command_inventory.py` and
    `python tools\ui_smoke\check_rmlui_command_inventory.py --format json`:
    passed with `289` direct command refs and `0` malformed command attributes.
  - `python tools\ui_smoke\check_rmlui_cvar_inventory.py` and
    `python tools\ui_smoke\check_rmlui_cvar_inventory.py --format json`:
    passed with `452` total cvar refs, `272` unique cvars, and `0`
    unknown/bad tokens.
  - `python tools\ui_smoke\check_rmlui_navigation_graph.py`: passed with `0`
    unknown static route targets.
  - `python tools\ui_smoke\check_rmlui_parity_manifest.py`: passed with `57`
    route checklists, `9` categories, `0` `parity_ready` routes, `23`
    complete controller-binding entries, and all real renderer/screenshot/input
    categories still pending.
  - `python tools\ui_smoke\check_rmlui_semantics.py`: passed with `57`
    documents checked, `52` route targets checked, `289` command elements
    checked, and `452` cvar references checked.
  - `python tools\ui_smoke\check_rmlui_menu_entrypoints.py`: passed with `5`
    menu cases checked, `4` mapped route cases, and `3` unique mapped route
    IDs.
  - `python tools\ui_smoke\check_rmlui_runtime_stub_eligibility.py`: passed
    with `3` `runtime_stub` routes checked, all menu-mapped, registry-matched,
    and controller-contract-matched.
  - `python tools\ui_smoke\check_rmlui_runtime_registry.py`: passed with `57`
    manifest routes, `58` registered routes, `0` missing, `0` unexpected,
    `0` duplicates, and `57` matched runtime paths.
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports`:
    passed with `57` source route documents present, `16` imported assets
    present, and `73` runtime paths derived.
  - `python tools\ui_smoke\report_rmlui_progress.py`,
    `python tools\ui_smoke\report_rmlui_progress.py --format markdown`, and
    `python tools\ui_smoke\report_rmlui_progress.py --format json`: passed
    with `57/57` documents present, migration phases `starter=34`,
    `controller_stub=20`, `runtime_stub=3`, `23` advanced routes (`40.4%`),
    `65` controller-contract references across `23` routes, and a parity
    checklist summary showing `0` `parity_ready` routes. Text, markdown, and
    JSON output also include command/cvar inventory summaries.
  - `python -m pytest tools/test_package_assets.py tools/ui_smoke/test_check_rmlui_manifest.py tools/ui_smoke/test_check_rmlui_route_contracts.py tools/ui_smoke/test_check_rmlui_semantics.py tools/ui_smoke/test_check_rmlui_runtime_assets.py tools/ui_smoke/test_check_rmlui_runtime_registry.py tools/ui_smoke/test_check_rmlui_controller_stub_coverage.py tools/ui_smoke/test_check_rmlui_menu_entrypoints.py tools/ui_smoke/test_check_rmlui_runtime_stub_eligibility.py tools/ui_smoke/test_check_rmlui_navigation_graph.py tools/ui_smoke/test_check_rmlui_controller_fixtures.py tools/ui_smoke/test_check_rmlui_parity_manifest.py tools/ui_smoke/test_check_rmlui_command_inventory.py tools/ui_smoke/test_check_rmlui_cvar_inventory.py tools/ui_smoke/test_report_rmlui_progress.py`:
    passed with `108` tests.
  - `python tools\package_assets.py --assets-dir assets --install-dir .tmp\rmlui\round10-package-validation --base-game basew --archive-name pak0.pkz`:
    passed, packed `195` files from `assets`, and validated `31` botfile
    package/loose files plus `101` RmlUi package/loose files.
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round10-package-validation --base-game basew`
    and the same command with `--format json`: passed with `73` staged loose
    route/import assets present, including all `16` imported assets, and `0`
    missing.
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round10-package-validation --base-game basew --write-manifest .tmp\rmlui\round10-runtime-assets.json`:
    passed and wrote staged manifest entries against
    `.tmp/rmlui/round10-package-validation`.
  - `ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj -v`:
    passed. The broader Windows Meson target remains subject to the existing
    `llvm-ar` regular-archive to thin-archive blocker recorded in Round 4.

### Round 11 Evidence (2026-07-02)

Round 11 is accepted as another scaffold/validation round. It promotes only
static single-player/save-load controller metadata for `singleplayer`,
`skill_select`, `loadgame`, and `savegame`; it also adds data-model inventory,
phase-consistency, dependency-decision, and progress-report status guardrails.
It still does not claim first-class RmlUi dependency integration, native
renderer output, live controller behavior, runtime navigation, screenshots,
parity, or legacy removal.

- Worker logs accepted:
  `docs-dev/rmlui-agent1-data-model-inventory-round11-2026-07-02.md`,
  `docs-dev/rmlui-agent2-singleplayer-controller-stubs-round11-2026-07-02.md`,
  `docs-dev/rmlui-agent3-phase-consistency-round11-2026-07-02.md`,
  `docs-dev/rmlui-agent4-dependency-decision-check-round11-2026-07-02.md`,
  and `docs-dev/rmlui-agent5-progress-data-model-round11-2026-07-02.md`.
- Worker 1 added `tools/ui_smoke/check_rmlui_data_model_inventory.py`, which
  inventories static data-model/data-binding hooks across authored route
  documents. The accepted baseline has `190` total model/data-binding refs,
  `187` unique model tokens, `30` component refs, `13` controller refs, `33`
  action-type refs, `31` slot refs, `38` routes with data-model hooks, and `0`
  malformed tokens.
- Worker 2 promoted exactly `singleplayer`, `skill_select`, `loadgame`, and
  `savegame` to `controller_stub`, adding single-player and save/load contract
  metadata while preserving legacy fallback status. The accepted phase
  baseline is now `starter=30`, `controller_stub=24`, and `runtime_stub=3`.
- Worker 3 added `tools/ui_smoke/check_rmlui_phase_consistency.py`, which
  verifies advanced route phases have route metadata/controller-contract
  evidence, runtime stubs remain mapped to guarded menu entry points, and
  `parity_ready` is not overclaimed without complete parity checklist evidence.
- Worker 4 added `tools/ui_smoke/check_rmlui_dependency_decision.py`, which
  validates the proposed/not-implemented RmlUi dependency decision record, its
  no-go wording, native OpenGL/Vulkan/RTX-vkpt obligations, Gate G1 interface
  requirements, and validation/staging evidence.
- Worker 5 extended `tools/ui_smoke/report_rmlui_progress.py` so text,
  markdown, and JSON reports include data-model inventory counts when the new
  checker is available, with graceful unavailable/error reporting.
- Coordinator integration log:
  `docs-dev/rmlui-parallel-round11-integration-2026-07-02.md`.
- Coordinator validation accepted:
  - `python tools\ui_smoke\check_rmlui_manifest.py`: passed with `57` total
    routes, `57` required, `57` present, `0` pending, waves `A=21`, `B=11`,
    `C=25`, and migration phases `starter=30`, `controller_stub=24`,
    `runtime_stub=3`.
  - `python tools\ui_smoke\check_rmlui_route_contracts.py`: passed with core,
    shell, smoke, and utility route metadata checked; shell metadata reports
    `54` controller-contract references and utility metadata reports `21`.
  - `python tools\ui_smoke\check_rmlui_controller_stub_coverage.py`: passed
    with `3` route metadata files checked, `24` `controller_stub` routes, and
    no missing inferred contract categories.
  - `python tools\ui_smoke\check_rmlui_controller_fixtures.py`: passed with
    `75` controller-contract refs across `27` routes and `8` fixtures present.
  - `python tools\ui_smoke\check_rmlui_data_model_inventory.py` and
    `python tools\ui_smoke\check_rmlui_data_model_inventory.py --format json`:
    passed with `190` total model/data-binding refs, `187` unique model
    tokens, `38` routes with hooks, and `0` malformed tokens.
  - `python tools\ui_smoke\check_rmlui_phase_consistency.py` and
    `python tools\ui_smoke\check_rmlui_phase_consistency.py --format json`:
    passed with `27` metadata-backed advanced routes, `3` runtime-stub routes,
    `3` runtime menu-mapped routes, `0` `parity_ready` routes, and `0` errors.
  - `python tools\ui_smoke\check_rmlui_dependency_decision.py` and
    `python tools\ui_smoke\check_rmlui_dependency_decision.py --format json`:
    passed with `5/5` required task IDs present, `0` status overclaims, `4/4`
    no-go items, `4/4` native renderer obligations, `5/5` Gate G1 interface
    areas, and `3/3` validation evidence groups.
  - Existing command/cvar/navigation/parity/runtime checks passed with stable
    Round 10 inventory counts: `289` direct command refs, `15` cvar-command
    refs, `452` total cvar refs, `272` unique cvars, `57` parity checklist
    routes, `9` parity categories, and `0` `parity_ready` routes.
  - `python tools\ui_smoke\report_rmlui_progress.py`,
    `python tools\ui_smoke\report_rmlui_progress.py --format markdown`, and
    `python tools\ui_smoke\report_rmlui_progress.py --format json`: passed
    with `57/57` documents present, migration phases `starter=30`,
    `controller_stub=24`, `runtime_stub=3`, `27` advanced routes (`47.4%`),
    `75` controller-contract references across `27` routes, parity summary
    showing `0` `parity_ready` routes, and command/cvar/data-model inventory
    summaries.
  - `python -m pytest tools/test_package_assets.py tools/ui_smoke/test_check_rmlui_manifest.py tools/ui_smoke/test_check_rmlui_route_contracts.py tools/ui_smoke/test_check_rmlui_semantics.py tools/ui_smoke/test_check_rmlui_runtime_assets.py tools/ui_smoke/test_check_rmlui_runtime_registry.py tools/ui_smoke/test_check_rmlui_controller_stub_coverage.py tools/ui_smoke/test_check_rmlui_menu_entrypoints.py tools/ui_smoke/test_check_rmlui_runtime_stub_eligibility.py tools/ui_smoke/test_check_rmlui_navigation_graph.py tools/ui_smoke/test_check_rmlui_controller_fixtures.py tools/ui_smoke/test_check_rmlui_parity_manifest.py tools/ui_smoke/test_check_rmlui_command_inventory.py tools/ui_smoke/test_check_rmlui_cvar_inventory.py tools/ui_smoke/test_check_rmlui_data_model_inventory.py tools/ui_smoke/test_check_rmlui_phase_consistency.py tools/ui_smoke/test_check_rmlui_dependency_decision.py tools/ui_smoke/test_report_rmlui_progress.py`:
    passed with `127` tests.
  - `python tools\package_assets.py --assets-dir assets --install-dir .tmp\rmlui\round11-package-validation --base-game basew --archive-name pak0.pkz`:
    passed, packed `195` files from `assets`, and validated `31` botfile
    package/loose files plus `101` RmlUi package/loose files.
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round11-package-validation --base-game basew`
    and the same command with `--format json`: passed with `73` staged loose
    route/import assets present, including all `16` imported assets, and `0`
    missing.
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round11-package-validation --base-game basew --write-manifest .tmp\rmlui\round11-runtime-assets-staged.json`:
    passed and wrote staged manifest entries against
    `.tmp/rmlui/round11-package-validation`.
  - `ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj -v`:
    passed. Ninja still emitted the known `premature end of file; recovering`
    warning, and the broader Windows Meson target remains subject to the
    existing `llvm-ar` regular-archive to thin-archive blocker recorded in
    Round 4.

### Round 12 Evidence (2026-07-02)

Round 12 is accepted as another scaffold/validation round. It promotes only
static local-session controller metadata for `downloads`, `quit_confirm`,
`gameflags`, and `startserver`; it also adds starter route metadata for the
multiplayer hub plus all tracked session/match routes, condition inventory,
metadata-sync validation, and progress-report guardrail summaries. It still
does not claim first-class RmlUi dependency integration, native renderer
output, live controller behavior, runtime navigation, screenshots, parity, or
legacy removal.

- Worker logs accepted:
  `docs-dev/rmlui-agent1-local-session-controller-stubs-round12-2026-07-02.md`,
  `docs-dev/rmlui-agent2-session-route-metadata-round12-2026-07-02.md`,
  `docs-dev/rmlui-agent3-condition-inventory-round12-2026-07-02.md`,
  `docs-dev/rmlui-agent4-metadata-sync-round12-2026-07-02.md`, and
  `docs-dev/rmlui-agent5-progress-guardrails-round12-2026-07-02.md`.
- Worker 1 promoted exactly `downloads`, `quit_confirm`, `gameflags`, and
  `startserver` to `controller_stub`, adding static mock cvar, command,
  navigation, and condition-state metadata while preserving legacy fallback
  status. The accepted phase baseline is now `starter=26`,
  `controller_stub=28`, and `runtime_stub=3`.
- Worker 2 added `assets/ui/rml/multiplayer/routes.json` with `1` starter
  route and `assets/ui/rml/session/routes.json` with `25` starter session
  routes. These files add ownership, source/current surface, entrypoint, and
  data-model metadata only; no central phase promotion or live session
  behavior is claimed.
- Worker 3 added `tools/ui_smoke/check_rmlui_condition_inventory.py`, which
  inventories static condition-expression hooks. The accepted baseline has
  `141` condition refs across `22` routes, `114` unique expressions, `111`
  unique tokens/cvars, and `0` malformed condition attributes.
- Worker 4 added `tools/ui_smoke/check_rmlui_metadata_sync.py`, which compares
  discovered feature route metadata with the central smoke manifest. The
  accepted baseline has `5` metadata files, `58` metadata routes, `57` matched
  central migration routes, `1` explicit support metadata route
  (`core.runtime_smoke`), `0` central routes without metadata, `0` advanced
  missing metadata routes, `0` phase/document mismatches, and `0` duplicates.
- Worker 5 extended `tools/ui_smoke/report_rmlui_progress.py` so text,
  markdown, and JSON reports include condition-inventory and metadata-sync
  summaries when the new checkers are available, with graceful unavailable/error
  reporting.
- Coordinator integration log:
  `docs-dev/rmlui-parallel-round12-integration-2026-07-02.md`.
- Coordinator validation accepted:
  - `python tools\ui_smoke\check_rmlui_manifest.py`: passed with `57` total
    routes, `57` required, `57` present, `0` pending, waves `A=21`, `B=11`,
    `C=25`, and migration phases `starter=26`, `controller_stub=28`,
    `runtime_stub=3`.
  - `python tools\ui_smoke\check_rmlui_route_contracts.py`: passed with core,
    shell, smoke, multiplayer, session, and utility route metadata checked;
    shell metadata reports `66` controller-contract references and utility
    metadata reports `21`.
  - `python tools\ui_smoke\check_rmlui_controller_stub_coverage.py`: passed
    with `5` route metadata files checked, `28` `controller_stub` routes, and
    no missing inferred contract categories.
  - `python tools\ui_smoke\check_rmlui_controller_fixtures.py`: passed with
    `87` controller-contract refs across `31` routes and `8` fixtures present.
  - `python tools\ui_smoke\check_rmlui_condition_inventory.py` and
    `python tools\ui_smoke\check_rmlui_condition_inventory.py --format json`:
    passed with `141` total condition refs, `22` routes with hooks, `114`
    unique condition expressions, `111` unique condition tokens/cvars, and `0`
    malformed condition attributes.
  - `python tools\ui_smoke\check_rmlui_metadata_sync.py` and
    `python tools\ui_smoke\check_rmlui_metadata_sync.py --format json`:
    passed with `5` metadata files, `58` metadata routes, `57` matched routes,
    `1` support metadata route, and `0` errors.
  - `python tools\ui_smoke\check_rmlui_phase_consistency.py`: passed with
    `31` metadata-backed advanced routes, `3` runtime-stub routes, `3`
    runtime menu-mapped routes, `0` `parity_ready` routes, and `0` errors.
  - Existing command/cvar/data-model/navigation/parity/runtime checks passed
    with stable inventory counts: `289` direct command refs, `15`
    cvar-command refs, `452` total cvar refs, `272` unique cvars, `190`
    data-model/data-binding refs, `57` parity checklist routes, `9` parity
    categories, and `0` `parity_ready` routes.
  - `python tools\ui_smoke\report_rmlui_progress.py`,
    `python tools\ui_smoke\report_rmlui_progress.py --format markdown`, and
    `python tools\ui_smoke\report_rmlui_progress.py --format json`: passed
    with `57/57` documents present, migration phases `starter=26`,
    `controller_stub=28`, `runtime_stub=3`, `31` advanced routes (`54.4%`),
    `87` controller-contract references across `31` routes, parity summary
    showing `0` `parity_ready` routes, and command/cvar/data-model/condition/
    metadata summaries.
  - `python -m pytest tools/test_package_assets.py tools/ui_smoke/test_check_rmlui_manifest.py tools/ui_smoke/test_check_rmlui_route_contracts.py tools/ui_smoke/test_check_rmlui_semantics.py tools/ui_smoke/test_check_rmlui_runtime_assets.py tools/ui_smoke/test_check_rmlui_runtime_registry.py tools/ui_smoke/test_check_rmlui_controller_stub_coverage.py tools/ui_smoke/test_check_rmlui_menu_entrypoints.py tools/ui_smoke/test_check_rmlui_runtime_stub_eligibility.py tools/ui_smoke/test_check_rmlui_navigation_graph.py tools/ui_smoke/test_check_rmlui_controller_fixtures.py tools/ui_smoke/test_check_rmlui_parity_manifest.py tools/ui_smoke/test_check_rmlui_command_inventory.py tools/ui_smoke/test_check_rmlui_cvar_inventory.py tools/ui_smoke/test_check_rmlui_data_model_inventory.py tools/ui_smoke/test_check_rmlui_condition_inventory.py tools/ui_smoke/test_check_rmlui_metadata_sync.py tools/ui_smoke/test_check_rmlui_phase_consistency.py tools/ui_smoke/test_check_rmlui_dependency_decision.py tools/ui_smoke/test_report_rmlui_progress.py`:
    passed with `144` tests.
  - `python tools\package_assets.py --assets-dir assets --install-dir .tmp\rmlui\round12-package-validation --base-game basew --archive-name pak0.pkz`:
    passed, packed `197` files from `assets`, and validated `31` botfile
    package/loose files plus `103` RmlUi package/loose files.
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round12-package-validation --base-game basew`
    and the same command with `--format json`: passed with `73` staged loose
    route/import assets present, including all `16` imported assets, and `0`
    missing.
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round12-package-validation --base-game basew --write-manifest .tmp\rmlui\round12-runtime-assets-staged.json`:
    passed and wrote staged manifest entries against
    `.tmp/rmlui/round12-package-validation`.
  - `ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj -v`:
    passed. Ninja still emitted the known `premature end of file; recovering`
    warning, and the broader Windows Meson target remains subject to the
    existing `llvm-ar` regular-archive to thin-archive blocker recorded in
    Round 4.

### Round 13 Evidence (2026-07-02)

Round 13 is accepted as another scaffold/validation round. It promotes only
static vote/callvote session controller metadata for `vote_menu`,
`callvote_main`, `callvote_ruleset`, `callvote_timelimit`,
`callvote_scorelimit`, `callvote_unlagged`, `callvote_random`, and
`callvote_map_flags`; it also adds event/action inventory,
accessibility/localization inventory, a legacy-removal guardrail inventory, and
progress-report event/a11y summaries. It still does not claim first-class
RmlUi dependency integration, native renderer output, live controller
behavior, runtime navigation, screenshots, parity, or legacy removal.

- Worker logs accepted:
  `docs-dev/rmlui-agent1-session-vote-controller-stubs-round13-2026-07-02.md`,
  `docs-dev/rmlui-agent2-event-inventory-round13-2026-07-02.md`,
  `docs-dev/rmlui-agent3-a11y-inventory-round13-2026-07-02.md`,
  `docs-dev/rmlui-agent4-progress-events-a11y-round13-2026-07-02.md`, and
  `docs-dev/rmlui-agent5-legacy-removal-inventory-round13-2026-07-02.md`.
- Worker 1 promoted exactly `vote_menu`, `callvote_main`,
  `callvote_ruleset`, `callvote_timelimit`, `callvote_scorelimit`,
  `callvote_unlagged`, `callvote_random`, and `callvote_map_flags` to
  `controller_stub`, adding `14` static controller-contract references across
  the promoted session/vote routes while preserving legacy fallback status.
  The accepted phase baseline is now `starter=18`, `controller_stub=36`, and
  `runtime_stub=3`.
- Worker 2 added `tools/ui_smoke/check_rmlui_event_inventory.py`, which
  inventories static event/action hooks. The accepted baseline has `465`
  event/action refs across all `57` routes, including `289` `data-command`
  refs, `52` `data-route-target` refs, `33` `data-action-type` refs, `38`
  `data-event-click` refs, `38` `data-bind-command` refs, `15`
  `data-command-cvar` refs, `70` unique command tokens, and `0` malformed
  hooks.
- Worker 3 added `tools/ui_smoke/check_rmlui_a11y_inventory.py`, which
  inventories static accessibility/localization hooks. The accepted baseline
  has `8` a11y/localization refs across `3` routes, all via `data-loc-key`,
  with `6` unique localization keys, `0` role refs, and `0` malformed hooks.
- Worker 4 extended `tools/ui_smoke/report_rmlui_progress.py` so text,
  markdown, and JSON reports include event-inventory and a11y-inventory
  summaries when the new checkers are available, with graceful unavailable/error
  reporting.
- Worker 5 added `tools/ui_smoke/rmlui_legacy_removal_manifest.json` and
  `tools/ui_smoke/check_rmlui_legacy_removal.py`. The initial guardrail
  baseline tracks `6` removal items with `4` blocked, `2` pending, `0` ready,
  and `0` complete; all required categories and task IDs are represented, and
  no legacy removal is allowed while `parity_ready` remains `0`.
- Coordinator integration log:
  `docs-dev/rmlui-parallel-round13-integration-2026-07-02.md`.
- Coordinator validation accepted:
  - `python tools\ui_smoke\check_rmlui_manifest.py`: passed with `57` total
    routes, `57` required, `57` present, `0` pending, waves `A=21`, `B=11`,
    `C=25`, and migration phases `starter=18`, `controller_stub=36`,
    `runtime_stub=3`.
  - `python tools\ui_smoke\check_rmlui_route_contracts.py`: passed with core,
    shell, smoke, multiplayer, session, and utility route metadata checked;
    session metadata reports `25` routes with `14` controller-contract refs.
  - `python tools\ui_smoke\check_rmlui_controller_stub_coverage.py`: passed
    with `5` route metadata files checked, `36` `controller_stub` routes, and
    no missing inferred contract categories.
  - `python tools\ui_smoke\check_rmlui_controller_fixtures.py`: passed with
    `101` controller-contract refs across `39` routes and `9` fixtures
    present.
  - `python tools\ui_smoke\check_rmlui_event_inventory.py` and
    `python tools\ui_smoke\check_rmlui_event_inventory.py --format json`:
    passed with `465` total event/action refs, `57` routes with hooks, `70`
    unique command tokens, and `0` malformed hooks.
  - `python tools\ui_smoke\check_rmlui_a11y_inventory.py` and
    `python tools\ui_smoke\check_rmlui_a11y_inventory.py --format json`:
    passed with `8` a11y/localization refs, `3` routes with hooks, `6` unique
    localization keys, and `0` malformed hooks.
  - `python tools\ui_smoke\check_rmlui_legacy_removal.py` and
    `python tools\ui_smoke\check_rmlui_legacy_removal.py --format json`:
    passed with `6` tracked removal items, statuses `blocked=4`,
    `pending=2`, `ready=0`, `complete=0`, all required categories/task IDs
    represented, and parity-removal gates closed.
  - Existing command/cvar/data-model/condition/metadata/navigation/parity/
    runtime checks passed with stable inventory counts: `289` direct command
    refs, `15` cvar-command refs, `452` total cvar refs, `272` unique cvars,
    `190` data-model/data-binding refs, `141` condition refs, `57` parity
    checklist routes, `9` parity categories, and `0` `parity_ready` routes.
  - `python tools\ui_smoke\report_rmlui_progress.py`,
    `python tools\ui_smoke\report_rmlui_progress.py --format markdown`, and
    `python tools\ui_smoke\report_rmlui_progress.py --format json`: passed
    with `57/57` documents present, migration phases `starter=18`,
    `controller_stub=36`, `runtime_stub=3`, `39` advanced routes (`68.4%`),
    `101` controller-contract references across `39` routes, parity summary
    showing `0` `parity_ready` routes, and command/cvar/data-model/condition/
    metadata/event/a11y summaries.
  - `python -m pytest tools/test_package_assets.py tools/ui_smoke/test_check_rmlui_manifest.py tools/ui_smoke/test_check_rmlui_route_contracts.py tools/ui_smoke/test_check_rmlui_semantics.py tools/ui_smoke/test_check_rmlui_runtime_assets.py tools/ui_smoke/test_check_rmlui_runtime_registry.py tools/ui_smoke/test_check_rmlui_controller_stub_coverage.py tools/ui_smoke/test_check_rmlui_menu_entrypoints.py tools/ui_smoke/test_check_rmlui_runtime_stub_eligibility.py tools/ui_smoke/test_check_rmlui_navigation_graph.py tools/ui_smoke/test_check_rmlui_controller_fixtures.py tools/ui_smoke/test_check_rmlui_parity_manifest.py tools/ui_smoke/test_check_rmlui_command_inventory.py tools/ui_smoke/test_check_rmlui_cvar_inventory.py tools/ui_smoke/test_check_rmlui_data_model_inventory.py tools/ui_smoke/test_check_rmlui_condition_inventory.py tools/ui_smoke/test_check_rmlui_event_inventory.py tools/ui_smoke/test_check_rmlui_a11y_inventory.py tools/ui_smoke/test_check_rmlui_metadata_sync.py tools/ui_smoke/test_check_rmlui_phase_consistency.py tools/ui_smoke/test_check_rmlui_dependency_decision.py tools/ui_smoke/test_check_rmlui_legacy_removal.py tools/ui_smoke/test_report_rmlui_progress.py`:
    passed with `163` tests.
  - `python tools\package_assets.py --assets-dir assets --install-dir .tmp\rmlui\round13-package-validation --base-game basew --archive-name pak0.pkz`:
    passed, packed `197` files from `assets`, and validated `31` botfile
    package/loose files plus `103` RmlUi package/loose files.
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round13-package-validation --base-game basew`
    and the same command with `--format json`: passed with `73` staged loose
    route/import assets present, including all `16` imported assets, and `0`
    missing.
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round13-package-validation --base-game basew --write-manifest .tmp\rmlui\round13-runtime-assets-staged.json`:
    passed and wrote staged manifest entries against
    `.tmp/rmlui/round13-package-validation`.
  - `ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj -v`:
    passed. Ninja still emitted the known `premature end of file; recovering`
    warning, and the broader Windows Meson target remains subject to the
    existing `llvm-ar` regular-archive to thin-archive blocker recorded in
    Round 4.

### Round 14 Evidence (2026-07-02)

Round 14 is accepted as another scaffold/validation round. It promotes only
static multiplayer/lobby/session-info controller metadata for `multiplayer`,
`dm_welcome`, `dm_join`, `join`, `dm_hostinfo`, and `dm_matchinfo`; it also
adds document/body identity inventory, route entrypoint inventory, stricter
route metadata shape validation, and progress-report legacy-removal gate
summaries. It still does not claim first-class RmlUi dependency integration,
native renderer output, live controller behavior, runtime navigation,
screenshots, parity, or legacy removal.

- Worker logs accepted:
  `docs-dev/rmlui-agent1-session-lobby-controller-stubs-round14-2026-07-02.md`,
  `docs-dev/rmlui-agent2-document-id-inventory-round14-2026-07-02.md`,
  `docs-dev/rmlui-agent3-entrypoint-inventory-round14-2026-07-02.md`,
  `docs-dev/rmlui-agent4-progress-legacy-removal-round14-2026-07-02.md`, and
  `docs-dev/rmlui-agent5-route-metadata-shape-round14-2026-07-02.md`.
- Worker 1 promoted exactly `multiplayer`, `dm_welcome`, `dm_join`, `join`,
  `dm_hostinfo`, and `dm_matchinfo` to `controller_stub`, adding `16` static
  controller-contract references across the promoted multiplayer/lobby/info
  routes while preserving legacy fallback status. The accepted central phase
  baseline is now `starter=12`, `controller_stub=42`, and `runtime_stub=3`.
- Worker 2 added `tools/ui_smoke/check_rmlui_document_id_inventory.py`, which
  verifies body/document identity across central routes and feature metadata.
  The accepted baseline has `57` route documents checked, `57` body IDs, `57`
  unique body IDs, `57` matched metadata/body document IDs, `0` duplicate body
  IDs, `0` route-ID mismatches, and `0` malformed documents.
- Worker 3 added `tools/ui_smoke/check_rmlui_entrypoint_inventory.py`, which
  inventories route metadata entry points. The accepted baseline has `5`
  metadata files, `58` metadata routes, `58` routes with entry points, `72`
  total entrypoint refs, `72` unique entrypoint strings, `1` support metadata
  route, and `0` malformed or duplicate per-route entry points.
- Worker 4 extended `tools/ui_smoke/report_rmlui_progress.py` so text,
  markdown, and JSON reports include legacy-removal summaries and the
  parity-gate closed/open state when the legacy-removal checker is available.
- Worker 5 added `tools/ui_smoke/check_rmlui_route_metadata_shape.py`, which
  validates discovered `assets/ui/rml/*/routes.json` metadata shape. The
  accepted baseline has `5` metadata files, `58` metadata routes,
  metadata-phase counts `starter=13`, `controller_stub=42`, `runtime_stub=3`,
  `45` routes with controller contracts, `117` controller-contract refs, and
  `0` malformed routes. The extra starter route is the support-only
  `core.runtime_smoke` metadata route.
- Coordinator integration log:
  `docs-dev/rmlui-parallel-round14-integration-2026-07-02.md`.
- Coordinator validation accepted:
  - `python tools\ui_smoke\check_rmlui_manifest.py`: passed with `57` total
    routes, `57` required, `57` present, `0` pending, waves `A=21`, `B=11`,
    `C=25`, and migration phases `starter=12`, `controller_stub=42`,
    `runtime_stub=3`.
  - `python tools\ui_smoke\check_rmlui_route_contracts.py`: passed with core,
    shell, smoke, multiplayer, session, and utility route metadata checked;
    controller contracts total `117` references across `45` advanced routes.
  - `python tools\ui_smoke\check_rmlui_controller_stub_coverage.py`: passed
    with `42` `controller_stub` routes and no missing inferred contract
    categories.
  - `python tools\ui_smoke\check_rmlui_controller_fixtures.py`: passed with
    `117` controller-contract refs across `45` routes and fixture coverage
    intact.
  - `python tools\ui_smoke\check_rmlui_document_id_inventory.py` and
    `python tools\ui_smoke\check_rmlui_document_id_inventory.py --format json`:
    passed with `57` checked body IDs, `57` unique body IDs, and `0`
    mismatches or malformed documents.
  - `python tools\ui_smoke\check_rmlui_entrypoint_inventory.py` and
    `python tools\ui_smoke\check_rmlui_entrypoint_inventory.py --format json`:
    passed with `72` total/unique entrypoint refs and `0` malformed
    entrypoints.
  - `python tools\ui_smoke\check_rmlui_route_metadata_shape.py` and
    `python tools\ui_smoke\check_rmlui_route_metadata_shape.py --format json`:
    passed with `58` metadata routes, `45` routes with controller contracts,
    `117` controller-contract refs, and `0` malformed routes.
  - `python tools\ui_smoke\report_rmlui_progress.py`,
    `python tools\ui_smoke\report_rmlui_progress.py --format markdown`, and
    `python tools\ui_smoke\report_rmlui_progress.py --format json`: passed
    with `57/57` documents present, migration phases `starter=12`,
    `controller_stub=42`, `runtime_stub=3`, `45` advanced routes (`78.9%`),
    `117` controller-contract references across `45` routes, parity summary
    showing `0` `parity_ready` routes, and command/cvar/data-model/condition/
    metadata/event/a11y/legacy-removal summaries.
  - Legacy-removal progress reporting confirms `6` items, `5` categories,
    statuses `blocked=4`, `pending=2`, `ready=0`, `complete=0`, `0` missing
    required task IDs, and a closed parity gate with `0` `parity_ready`
    routes.
  - Existing command/cvar/data-model/condition/event/a11y/metadata/navigation/
    parity/runtime checks passed with stable inventory counts except for the
    expected controller-binding progression: parity checklist
    `controller_bindings` now has `45` complete and `12` pending.
  - Focused pytest coverage for the updated RmlUi smoke suite passed with the
    new Round 14 tests included: `183 passed`.
  - Packaging and staged runtime asset validation passed with `197` packaged
    assets, `103` RmlUi package/loose assets, `73` staged runtime paths, and
    `16` staged imported assets.
  - The touched `ui_rml` client object compile passed for the round. This was
    the last accepted RmlUi round before the stale Windows builddir archive
    state was cleared and revalidated during the Round 15 dependency/build
    gate pass.

### Round 15 Evidence (2026-07-02)

Round 15 is accepted as the first dependency-source/build-gate slice. It
records a real upstream source acquisition path and a guarded Meson option, but
it still does not claim a compiled RmlUi runtime, native renderer output, live
controller behavior, runtime navigation, screenshots, parity, or legacy
removal.

- Worker logs accepted:
  `docs-dev/rmlui-agent1-dependency-source-round15-2026-07-02.md`,
  `docs-dev/rmlui-agent2-build-option-round15-2026-07-02.md`,
  `docs-dev/rmlui-agent3-runtime-interface-round15-2026-07-02.md`,
  `docs-dev/rmlui-agent4-dependency-integration-check-round15-2026-07-02.md`,
  and `docs-dev/rmlui-agent5-install-gate-round15-2026-07-02.md`.
- Worker 1 added `subprojects/rmlui.wrap`, pinning upstream RmlUi `6.2` at
  `https://github.com/mikke89/RmlUi/archive/refs/tags/6.2.tar.gz` with
  SHA-256
  `814c3ff7b9666280338d8f0dda85979f5daf028d01c85fc8975431d1e2fd8e8b`.
  The wrap uses CMake method source acquisition only; it intentionally has no
  Meson patch overlay, no `[provide]` section, and no default runtime enable.
- Worker 2 added the default-disabled Meson feature option `rmlui`. The
  default build does not probe or link RmlUi. `-Drmlui=auto` probes optional
  `RmlUi` CMake target `RmlUi::RmlUi` and then optional `rmlui` pkg-config
  style dependency; `-Drmlui=enabled` is reserved for a future hard failure if
  no real dependency resolves. `UI_RML_HAS_RUNTIME=1` is emitted only when a
  dependency is found.
- Worker 3 refactored `src/client/ui_rml/` to expose runtime availability,
  availability strings, a dependency-free file-interface boundary, and a
  future runtime hook interface while preserving the guarded legacy fallback.
- Worker 4 added `tools/ui_smoke/check_rmlui_dependency_integration.py`. The
  accepted baseline reports state `optional`, integration components `4/4`,
  wrap files `1`, source dirs `0`, Meson dependency declarations `2`, Meson
  options `1`, compile defines `1`, runtime compiled `no`, and scaffold
  status `compiled-stub`.
- Worker 5 revalidated package/install guardrails: `197` packaged assets,
  `103` RmlUi package/loose assets, `73` staged runtime paths, `16` staged
  imported assets, and `0` missing staged runtime assets.
- Coordinator integration log:
  `docs-dev/rmlui-parallel-round15-integration-2026-07-02.md`.
- Coordinator validation accepted:
  - `python tools\ui_smoke\check_rmlui_dependency_integration.py` and
    `python tools\ui_smoke\check_rmlui_dependency_integration.py --format json`:
    passed with dependency state `optional` and `4/4` components present.
  - `python tools\ui_smoke\check_rmlui_dependency_decision.py`: passed with no
    status overclaims and the Round 15 source/build gate reflected in the
    decision record.
  - `python tools\ui_smoke\check_rmlui_manifest.py`,
    `python tools\ui_smoke\check_rmlui_phase_consistency.py`, and
    `python tools\ui_smoke\report_rmlui_progress.py --format json`: passed
    with the unchanged route phase baseline `starter=12`,
    `controller_stub=42`, `runtime_stub=3`, `45/57` advanced routes, and
    `0` `parity_ready` routes.
  - `meson setup builddir-win --reconfigure`,
    `meson setup builddir-win --reconfigure -Drmlui=auto`, and
    `meson setup builddir-win --reconfigure -Drmlui=disabled`: passed; the
    active builddir was restored to the default-disabled RmlUi option.
  - `ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj -v`
    passed after the stale Windows builddir archive/log state was cleared.
    `meson compile -C builddir-win` then completed cleanly, and the final
    `ninja -C builddir-win -n` reported no work to do.
  - Focused pytest coverage for the updated RmlUi smoke/package suite passed
    with the new Round 15 dependency-integration tests included: `191 passed`.
  - Packaging and staged runtime asset validation passed under
    `.tmp/rmlui/round15-package-validation`, and staged evidence was written to
    `.tmp/rmlui/round15-runtime-assets-staged.json`.

### Round 16 Evidence (2026-07-02)

Round 16 is accepted as the static controller-stub completion slice. It
promotes the final `12` central starter routes to `controller_stub`, hardens
the remaining admin, MyMap, map selector, match stats, and tournament RML
hooks, reconciles feature route metadata, and adds a strict completion checker.
It still does not claim a compiled RmlUi runtime, native renderer output, live
controller behavior, runtime navigation, screenshots, parity, or legacy
removal.

- Worker logs accepted:
  `docs-dev/rmlui-agent1-admin-confirm-controller-stubs-round16-2026-07-02.md`,
  `docs-dev/rmlui-agent2-mymap-mapstats-controller-stubs-round16-2026-07-02.md`,
  `docs-dev/rmlui-agent3-tournament-controller-stubs-round16-2026-07-02.md`,
  `docs-dev/rmlui-agent4-controller-stub-completion-round16-2026-07-02.md`,
  and `docs-dev/rmlui-agent5-round16-install-docs-gate-2026-07-02.md`.
- Worker 1 added missing admin/confirmation route-target coverage, including
  the static `admin_menu` edge to `tourney_replay_confirm`.
- Worker 2 added direct RML hooks for MyMap, map selector, and match stats:
  cvar bindings, condition gates, and fixed-list providers now match the
  declared controller contracts.
- Worker 3 added explicit tournament command/cvar/list/visibility hooks for
  tournament info, map choices, veto, and replay-confirm flows.
- Worker 4 added
  `tools/ui_smoke/check_rmlui_controller_stub_completion.py`, which reports
  route phase completion in text/JSON and fails when strict completion is
  required while central starter routes remain.
- Worker 5 rechecked the install/package/parity gates and confirmed the
  completion target remained static controller metadata, not runtime activation.
- Coordinator integration log:
  `docs-dev/rmlui-parallel-round16-integration-2026-07-02.md`.
- Coordinator validation accepted:
  - `python tools\ui_smoke\check_rmlui_manifest.py`: passed with `57` total
    routes, `57` required, `57` present, `0` pending, waves `A=21`, `B=11`,
    `C=25`, and migration phases `starter=0`, `controller_stub=54`,
    `runtime_stub=3`.
  - `python tools\ui_smoke\check_rmlui_route_contracts.py`: passed with core,
    shell, smoke, multiplayer, session, and utility route metadata checked.
  - `python tools\ui_smoke\check_rmlui_semantics.py`: passed with `57`
    documents checked, `53` route targets, `290` command elements, and `494`
    cvar references.
  - `python tools\ui_smoke\check_rmlui_controller_stub_coverage.py`: passed
    with `54` `controller_stub` routes and no missing inferred contract
    categories.
  - `python tools\ui_smoke\check_rmlui_controller_stub_completion.py --require-complete-controller-stubs --format json`:
    passed with `0` starter routes, `54` controller-stub routes, `3`
    runtime-stub routes, and `57/57` advanced routes.
  - `python tools\ui_smoke\report_rmlui_progress.py --format json`: passed
    with `149` controller-contract references across all `57` routes. Contract
    categories are `command_action=57`, `condition_state=22`,
    `cvar_binding=30`, `keybind=3`, `list_provider=8`, `navigation=26`,
    `preview=1`, and `save_load=2`.
  - `python tools\ui_smoke\check_rmlui_parity_manifest.py --format json`:
    passed with `controller_bindings=57` complete and `0` pending, while
    `navigation`, renderer, screenshot/layout, input/back, and
    non-runtime legacy-fallback evidence remain pending.
  - `python tools\ui_smoke\check_rmlui_route_metadata_shape.py --format json`:
    passed with `58` metadata routes, including the support-only
    `core.runtime_smoke` starter metadata route, `57` routes with controller
    contracts, `149` controller-contract refs, and `0` malformed routes.
  - Legacy-removal validation still reports `6` items, `blocked=4`,
    `pending=2`, `ready=0`, `complete=0`, and a closed parity gate with `0`
    `parity_ready` routes.
  - Command/cvar/condition inventory validation passed with `290` direct
    command refs, `15` cvar-command refs, `494` total cvar refs, `282` unique
    cvars, `144` condition refs, and `0` malformed command/cvar/condition
    hooks.
  - Focused pytest coverage for the updated RmlUi smoke/package suite passed
    with the new Round 16 controller-stub completion tests included:
    `196 passed`.
  - Packaging and staged runtime asset validation passed under
    `.tmp/rmlui/round16-package-validation` with `197` packaged assets, `103`
    RmlUi package/loose assets, `73` staged runtime paths, and `16` staged
    imported assets.
  - `meson setup builddir-win --reconfigure`,
    `meson setup builddir-win --reconfigure -Drmlui=auto`, and
    `meson setup builddir-win --reconfigure -Drmlui=disabled`: passed; the
    optional `auto` probe remained non-fatal with no RmlUi dependency resolved,
    and the active builddir was restored to `rmlui=disabled`.
  - `meson compile -C builddir-win` and `ninja -C builddir-win -n`: passed
    with no work to do. Build option introspection reported `rmlui disabled`.

### Round 17 Evidence (2026-07-03)

Round 17 is accepted as the compiled-runtime adapter slice. It proves that the
default-disabled RmlUi build path can resolve the pinned upstream `6.2` source,
compile RmlUi Core, compile a guarded WORR runtime adapter, and link the engine
target in a scratch enabled build. It still does not claim native renderer
integration, route rendering, live controllers, runtime navigation,
screenshot/layout evidence, parity readiness, or legacy JSON removal.

- Implementation log:
  `docs-dev/rmlui-round17-compiled-runtime-adapter-2026-07-03.md`.
- `src/client/ui_rml/ui_rml_runtime.cpp` is compiled only when
  `UI_RML_HAS_RUNTIME` is enabled, includes RmlUi Core behind that guard,
  references `Rml::GetVersion`, `Rml::Initialise`, and `Rml::Shutdown`, and
  registers a runtime interface through `UI_Rml_RegisterCompiledRuntime`.
- `src/client/ui_rml/ui_rml.cpp` now reports
  `renderer_unavailable` when a compiled RmlUi Core runtime is present but no
  route-opening renderer bridge is registered.
- `meson.build` now supports an external `RmlUi::Core` CMake package, an
  external `rmlui` pkg-config dependency, or the pinned `subprojects/rmlui.wrap`
  CMake fallback. The fallback sets `RMLUI_FONT_ENGINE=none` and disables RmlUi
  samples/tests for this compile-only Core adapter proof.
- `subprojects/rmlui.wrap` now provides dependency aliases for `RmlUi` and
  `rmlui`; the Meson CMake fallback uses the exposed `rmlui_core` target.
- `tools/ui_smoke/check_rmlui_runtime_adapter.py` validates the guarded adapter,
  conservative route-open guard, renderer-unavailable state, Meson fallback
  options, and wrap provide aliases.
- Coordinator validation accepted:
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`:
    passed with the adapter listed in Meson, RmlUi Core include guarded, macro
    collision guards present, all three RmlUi Core symbols referenced, the
    conservative `CanOpenRoutes` false guard present, and no errors.
  - `python tools\ui_smoke\check_rmlui_dependency_integration.py --format json`:
    passed with dependency/build state `optional`, optional Meson probes,
    `subproject('rmlui')`, fallback `dependency('rmlui_core')`, the pinned
    wrap/source directory detected, and no errors.
  - `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py tools/ui_smoke/test_check_rmlui_dependency_integration.py`:
    passed with `15 passed`.
  - `meson setup builddir-win --reconfigure -Drmlui=disabled`: passed and
    preserved the default-disabled active builddir.
  - `ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml_runtime.cpp.obj`:
    passed for the default-disabled scaffold and adapter translation units.
  - `meson setup .tmp\rmlui\round17-rmlui-enabled3 -Drmlui=enabled`: passed
    after the fallback was pinned to RmlUi Core with `RMLUI_FONT_ENGINE=none`.
  - `ninja -C .tmp\rmlui\round17-rmlui-enabled3 subprojects/RmlUi-6.2/rmlui_core.dll`:
    passed and linked the RmlUi Core library.
  - `ninja -C .tmp\rmlui\round17-rmlui-enabled3 worr_engine_x86_64.dll`:
    passed and linked the WORR engine target against the compiled Core adapter.

### Round 18 Evidence (2026-07-03)

Round 18 is accepted as the RmlUi Core system/file bridge slice. It installs
WORR-backed RmlUi `SystemInterface` and `FileInterface` implementations before
`Rml::Initialise`, routes RmlUi file opens through WORR filesystem APIs, and
adds an explicit runtime-facing file probe command. It still does not claim
native renderer integration, route rendering, live controllers, runtime
navigation, screenshot/layout evidence, parity readiness, or legacy JSON
removal.

- Implementation log:
  `docs-dev/rmlui-round18-core-system-file-bridge-2026-07-03.md`.
- `src/client/ui_rml/ui_rml_runtime.cpp` now installs a RmlUi system interface
  backed by `Sys_Milliseconds`, `Com_EPrintf`, `Com_WPrintf`, and
  `Com_Printf`.
- The compiled adapter now installs a RmlUi file interface backed by
  `FS_OpenFile`, `FS_CloseFile`, `FS_Read`, `FS_Seek`, `FS_Tell`, and
  `FS_Length`.
- `ui_rml_runtime_probe [route_id]` starts the compiled runtime for an explicit
  developer probe, loads the resolved route document through
  `Rml::GetFileInterface()->LoadFile`, and stops the runtime again when the
  probe was the only reason it started.
- `CanOpenRoutes=false` and `renderer_unavailable` remain the expected route
  ownership state until a native renderer bridge exists.
- Coordinator validation accepted:
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`:
    passed with RmlUi Core/system/file includes guarded, RmlUi Core symbols
    present, RmlUi interface symbols present, WORR filesystem symbols present,
    WORR system/log symbols present, runtime probe hook present, and interface
    installation before `Rml::Initialise` confirmed.
  - `python tools\ui_smoke\check_rmlui_dependency_integration.py --format json`:
    passed with dependency/build state still `optional`.
  - `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py`:
    passed with `7 passed`.
  - `ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml_runtime.cpp.obj`:
    passed for the default-disabled scaffold and adapter translation units.
  - `ninja -C .tmp\rmlui\round17-rmlui-enabled3 worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml_runtime.cpp.obj`:
    passed for the enabled scratch build.
  - `ninja -C .tmp\rmlui\round17-rmlui-enabled3 worr_engine_x86_64.dll`:
    passed and linked the enabled scratch engine target against the updated
    RmlUi Core adapter.

### Round 19 Evidence (2026-07-03)

Round 19 is accepted as the native renderer bridge contract slice. It adds the
renderer-family registration boundary needed by future native OpenGL, Vulkan,
and RTX/vkpt render-interface implementations while keeping RmlUi route
ownership guarded. It does not implement a renderer backend, create a RmlUi
context, draw a route, claim parity, or redirect Vulkan paths to OpenGL.

- Implementation log:
  `docs-dev/rmlui-round19-native-renderer-bridge-contract-2026-07-03.md`.
- `src/client/ui_rml/ui_rml.h` now declares `ui_rml_renderer_family_t` with
  explicit `opengl`, `vulkan`, and `rtx_vkpt` family lanes plus
  `ui_rml_renderer_interface_t` with `RendererName`, `CanRender`, and an
  opaque `NativeRenderInterface` hook.
- `src/client/ui_rml/ui_rml.cpp` now owns renderer registration/query helpers,
  reports renderer name/family in runtime diagnostics, and requires a native
  render-interface pointer before renderer availability can become true.
- `UI_Rml_RuntimeCanOpenRoutes` now gates route availability on
  `UI_Rml_RendererIsAvailable` before consulting the runtime route-open hook.
- `src/client/ui_rml/ui_rml_runtime.cpp` still returns `CanOpenRoutes=false`;
  renderer registration alone is not enough to claim menu ownership.
- Coordinator validation accepted:
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`:
    passed with renderer contract declarations, OpenGL/Vulkan/RTX-vkpt family
    coverage, renderer scaffold coverage, route gate coverage, native
    render-interface requirements, Vulkan no-redirect guard, and all prior
    runtime/system/file boundary checks confirmed.
  - `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py`:
    passed with `7 passed`.
  - `ninja -C builddir-win worr_engine_x86_64.dll`: passed and linked the
    default-disabled engine target.
  - `ninja -C .tmp\rmlui\round17-rmlui-enabled3 worr_engine_x86_64.dll`:
    passed and linked the enabled scratch engine target against the updated
    RmlUi Core adapter.

### Round 20 Evidence (2026-07-03)

Round 20 is accepted as the first renderer-family implementation scaffold. It
adds an OpenGL-owned RmlUi `RenderInterface` object and wires it through the
renderer export/client registration/runtime adapter path while keeping route
ownership guarded. It does not implement visible drawing, create a RmlUi
context, draw a route, claim parity, or redirect Vulkan/RTX-vkpt paths to
OpenGL.

- Implementation log:
  `docs-dev/rmlui-round20-opengl-render-interface-scaffold-2026-07-03.md`.
- `inc/renderer/renderer.h` now exposes renderer-side RmlUi family values and
  export slots for renderer name, renderer readiness, and the opaque native
  render-interface pointer.
- `src/renderer/rmlui_bridge.cpp` is compiled into the OpenGL renderer. In
  RmlUi-enabled builds, it creates an OpenGL `Rml::RenderInterface` scaffold
  with the required geometry, texture, and scissor methods.
- `R_RmlUiCanRender()` intentionally returns `false`, so the OpenGL scaffold
  cannot make `UI_Rml_RendererIsAvailable()` true until it actually draws.
- `src/renderer/renderer_api.c` exports the concrete bridge only for
  `USE_REF == REF_GL`; non-OpenGL renderer DLLs export no RmlUi renderer
  family and no native interface.
- `src/client/renderer.cpp` registers the native renderer bridge after
  renderer initialization and clears it during renderer shutdown.
- `src/client/ui_rml/ui_rml_runtime.cpp` installs the native
  `Rml::RenderInterface` through `Rml::SetRenderInterface` before
  `Rml::Initialise` when a renderer supplies one.
- Meson now supplies generated config/q2proto platform defines to external
  renderer C++ sources, and the RmlUi renderer dependency/compile define stays
  scoped to the OpenGL scaffold.
- Coordinator validation accepted:
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`:
    passed with renderer API exports, OpenGL-only bridge scoping, client
    registration/clear lifecycle, `Rml::SetRenderInterface`, OpenGL
    `CanRender=false`, and no Vulkan-to-OpenGL redirection confirmed.
  - `python tools\ui_smoke\check_rmlui_dependency_integration.py --format json`:
    passed with dependency/build state `optional`; guarded client/OpenGL
    renderer `UI_RML_HAS_RUNTIME` defines remain optional.
  - `python tools\ui_smoke\check_rmlui_dependency_decision.py --format json`:
    passed with Round 20 active status and remaining runtime/renderer/parity
    guardrails intact.
  - `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py tools/ui_smoke/test_check_rmlui_dependency_integration.py tools/ui_smoke/test_check_rmlui_dependency_decision.py`:
    passed with `25 passed`.
  - `ninja -C builddir-win worr_engine_x86_64.dll worr_opengl_x86_64.dll`:
    passed and linked the default-disabled engine and OpenGL renderer targets.
  - `ninja -C .tmp\rmlui\round17-rmlui-enabled3 worr_engine_x86_64.dll worr_opengl_x86_64.dll`:
    passed and linked the enabled scratch engine and OpenGL renderer targets
    against the compiled RmlUi Core path.
  - `git diff --check`: passed with only existing LF-to-CRLF normalization
    warnings.

### Round 21 Evidence (2026-07-04)

Round 21 is accepted as the first OpenGL-native RmlUi primitive bridge. It
keeps the renderer-family boundary from Round 19 and the OpenGL-only export
scope from Round 20, but replaces the OpenGL no-op scaffold with renderer-owned
geometry, texture, draw, and scissor behavior. It does not create a RmlUi
context, draw a menu route, claim parity, or redirect Vulkan/RTX-vkpt paths to
OpenGL.

- Implementation log:
  `docs-dev/rmlui-round21-opengl-render-primitives-2026-07-04.md`.
- `src/renderer/rmlui_bridge.cpp` now compiles RmlUi geometry into
  `glVertexDesc2D_t`/`glIndex_t` caches, converts premultiplied RmlUi colors
  back to straight alpha for WORR's current OpenGL blend state, and releases
  compiled geometry through the RmlUi handle lifetime.
- The OpenGL bridge renders compiled geometry through the existing `tess` 2D
  path, applies RmlUi translations, resolves RmlUi texture handles to OpenGL
  texture ids, and flushes the batch for immediate primitive output.
- Generated RmlUi textures upload through OpenGL-owned textures with linear
  filtering and clamp-to-edge when available. Loaded textures use `IMG_Find`
  and remain owned by the renderer image manager rather than deleted by RmlUi.
- RmlUi scissor enable/disable and rectangle updates now map to
  `GL_SCISSOR_TEST`, `qglScissor`, and the renderer's `draw.scissor` tracking.
- `R_RmlUiCanRender()` now returns `true` for RmlUi-enabled OpenGL builds
  because the required render-interface primitives are implemented. The
  compiled runtime still returns `CanOpenRoutes=false`, so normal menu entry
  points keep legacy fallback until a context/draw-loop route proof exists.
- Runtime-adapter validation now checks OpenGL geometry caching, tessellator
  drawing, generated texture upload, loaded/generated texture lifetime,
  scissor state, OpenGL-scoped dependency wiring, `CanRender=true`, and no
  Vulkan-to-OpenGL redirection.
- Coordinator validation accepted:
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`:
    passed with `errors=0` and the new OpenGL primitive bridge checks.
  - `python tools\ui_smoke\check_rmlui_dependency_integration.py --format json`:
    passed with dependency/build state `optional`.
  - `python tools\ui_smoke\check_rmlui_dependency_decision.py --format json`:
    passed with route-rendering, Vulkan, and legacy-removal guardrails intact.
  - `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py tools/ui_smoke/test_check_rmlui_dependency_integration.py tools/ui_smoke/test_check_rmlui_dependency_decision.py`:
    passed with the focused RmlUi dependency/runtime suite.
  - `ninja -C builddir-win worr_engine_x86_64.dll worr_opengl_x86_64.dll`:
    passed and linked the default-disabled engine and OpenGL renderer targets.
  - `ninja -C .tmp\rmlui\round17-rmlui-enabled3 worr_engine_x86_64.dll worr_opengl_x86_64.dll`:
    passed and linked the enabled scratch engine and OpenGL renderer targets
    against the compiled RmlUi Core path.
  - `git diff --check`: passed with only existing LF-to-CRLF normalization
    warnings.

### Round 22 Evidence (2026-07-04)

Round 22 is accepted as the first guarded RmlUi context/draw path. It keeps the
Round 21 OpenGL primitive bridge and opens only the sample
`core.runtime_smoke` route through a new explicit command path. Normal menu
entry points (`main`, `game`, `download_status`, and the rest of the route
table) still fall back to the legacy UI until route-level input, font/text,
controller, and parity evidence exists. Vulkan and RTX/vkpt remain native
pending and are not redirected to OpenGL.

- Implementation log:
  `docs-dev/rmlui-round22-guarded-context-route-2026-07-04.md`.
- `ui_rml_runtime_interface_t` now includes `CloseRoute`, `Update`, and
  `Render` hooks so the dependency-free scaffold can own route lifecycle
  without exposing RmlUi types outside the compiled adapter.
- `src/client/ui_rml/ui_rml_runtime.cpp` creates a `worr_ui` RmlUi context,
  loads one `ElementDocument`, shows it, resizes the context from renderer
  dimensions, updates it before rendering, renders through the native OpenGL
  bridge, and removes the context during shutdown.
- `src/client/ui_rml/ui_rml.cpp` keeps a scaffold-side active-route guard,
  allow-lists only `core.runtime_smoke`, exposes
  `ui_rml_runtime_open [route_id]` and `ui_rml_runtime_close`, and keeps normal
  menu routes on the legacy fallback path.
- `src/client/ui_bridge.cpp` now lets an active RmlUi route draw before the
  legacy cgame UI draw callback, suppresses legacy input/frame callbacks while
  the sample route is active, and closes the sample route on Escape.
- Runtime-adapter validation now checks context lifecycle hooks, adapter
  `CreateContext`/`LoadDocument`/`Update`/`Render`/`RemoveContext` behavior,
  sample-route guardrails, runtime open/close commands, UI bridge draw order,
  and the existing no Vulkan-to-OpenGL redirection guard.
- Coordinator validation accepted:
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`:
    passed with `errors=0` and the new context lifecycle checks.
  - `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py`:
    passed with the focused runtime-adapter suite.
  - `ninja -C builddir-win worr_engine_x86_64.dll worr_opengl_x86_64.dll`:
    passed and linked the default-disabled engine and OpenGL renderer targets.

### Round 23 Evidence (2026-07-04)

Round 23 is accepted as the guarded sample input/capture proof. It keeps the
Round 22 `core.runtime_smoke` ownership guard and adds key, text, mouse button,
mouse wheel, and pointer movement delivery into the RmlUi context. It also adds
runtime status counters and `ui_rml_runtime_capture` as the repeatable manual
path for collecting OpenGL visual evidence in the next slice. Normal menu
routes still fall back to legacy UI; Vulkan/RTX-vkpt bridges, live
controllers, automated screenshots, full font/input services, and parity remain
pending.

- Implementation log:
  `docs-dev/rmlui-round23-input-capture-2026-07-04.md`.
- `ui_rml_runtime_interface_t` now includes `KeyEvent`, `CharEvent`, and
  `MouseEvent` hooks, preserving the dependency-free public scaffold boundary.
- `src/client/ui_bridge.cpp` now routes guarded RmlUi key, char, draw, and
  mouse callbacks before legacy cgame UI callbacks while preserving
  console/chat mouse notification before pointer delivery.
- `src/client/ui_rml/ui_rml_runtime.cpp` maps WORR keys/modifiers into
  `Rml::Input`, dispatches key/text/mouse button/mouse wheel/pointer events to
  the active RmlUi context, and keeps the OpenGL-only sample route guard.
- `src/client/ui_rml/ui_rml.cpp` tracks guarded route frame/input counters,
  closes the active sample route on Escape or mouse button 2, and exposes
  `ui_rml_runtime_status` plus `ui_rml_runtime_capture`.
- Runtime-adapter validation now checks input hook declarations, adapter
  event-delivery calls, UI bridge input ordering, close/back tokens, and
  status/capture command coverage.
- Coordinator validation accepted:
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`:
    passed with `errors=0` and the new input/capture checks.
  - `python tools\ui_smoke\check_rmlui_dependency_integration.py --format json`:
    passed with `errors=0`.
  - `python tools\ui_smoke\check_rmlui_dependency_decision.py --format json`:
    passed with `errors=0`.
  - `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py tools/ui_smoke/test_check_rmlui_dependency_integration.py tools/ui_smoke/test_check_rmlui_dependency_decision.py`:
    passed with the focused smoke checker suite.
  - `ninja -C builddir-win worr_engine_x86_64.dll worr_opengl_x86_64.dll`:
    passed and linked the default-disabled engine and OpenGL renderer targets.
  - `ninja -C .tmp\rmlui\round17-rmlui-enabled3 worr_engine_x86_64.dll worr_opengl_x86_64.dll`:
    passed and linked the enabled scratch engine and OpenGL renderer targets.
  - `git diff --check`: passed with only existing LF-to-CRLF normalization
    warnings.

### Round 24 Evidence (2026-07-04)

Round 24 is accepted as the guarded runtime capture harness proof. It keeps
the `core.runtime_smoke` ownership guard, uses the Round 23 capture/status
commands, installs a temporary layout-only RmlUi font engine so RmlUi can
initialize under the current `RMLUI_FONT_ENGINE=none` build, and records a
fresh nonblank OpenGL TGA screenshot with matching runtime counters. The
capture shows styled RmlUi geometry but no glyph rendering yet. Normal menu
routes still fall back to legacy UI; Vulkan/RTX-vkpt bridges, real font/text
services, live controllers, runtime navigation, and parity remain pending.

- Implementation log:
  `docs-dev/rmlui-round24-runtime-capture-harness-2026-07-04.md`.
- `tools/ui_smoke/check_rmlui_runtime_capture.py` launches the engine when
  requested, opens `core.runtime_smoke`, captures a TGA screenshot by default,
  validates log markers/status counters/frame counters/screenshot dimensions,
  checks the TGA payload is nonblank, copies evidence to `.tmp/rmlui/`, and
  writes a JSON manifest.
- `r_screenshot_dir` is now an empty-by-default, non-archived renderer cvar in
  the OpenGL and RTX screenshot implementations so automation can keep
  evidence in `.install/basew/screenshots` without changing normal screenshot
  behavior.
- `src/client/ui_rml/ui_rml_runtime.cpp` installs
  `UI_Rml_NullFontEngineInterface` before `Rml::Initialise`. This is only a
  bootstrap/layout adapter: it reports metrics and widths, but emits no glyph
  mesh.
- `assets/ui/rml/core/runtime_smoke.rml` now links
  `runtime_smoke.rcss`, giving the guarded capture route visible background,
  panel, border, and button geometry.
- Accepted live evidence:
  - `.install/basew/screenshots/rmlui_runtime_smoke_round24.tga`
  - `.install/basew/logs/rmlui_runtime_smoke_round24.log`
  - `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round24.tga`
  - `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round24.log`
  - `.tmp/rmlui/runtime-capture/manifest.json`
- Final manifest facts included `ok=true`, `errors=[]`, `updates=24`,
  `renders=24`, OpenGL guarded route status, screenshot format `tga`,
  screenshot dimensions `960x720`, size `2073618`, and nonblank payload.
- Coordinator validation accepted:
  - `python tools\ui_smoke\check_rmlui_runtime_capture.py --run --engine-exe .tmp\rmlui\round17-rmlui-enabled3\worr_x86_64.exe --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\manifest.json --format json --timeout 120`:
    passed with `errors=0` and copied log/screenshot evidence.
  - `python tools\ui_smoke\check_rmlui_runtime_capture.py --engine-exe .tmp\rmlui\round17-rmlui-enabled3\worr_x86_64.exe --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\manifest.json --format json`:
    passed against the freshly captured evidence.
  - `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_capture.py`:
    passed with the focused capture-harness suite.
  - `ninja -C builddir-win worr_x86_64.exe worr_engine_x86_64.dll worr_opengl_x86_64.dll worr_rtx_x86_64.dll`:
    passed and linked the default-disabled engine, OpenGL, and RTX targets.
  - `ninja -C .tmp\rmlui\round17-rmlui-enabled3 worr_x86_64.exe worr_engine_x86_64.dll worr_opengl_x86_64.dll`:
    passed and linked the enabled scratch engine and OpenGL renderer targets.
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`:
    passed and validated the staged payload, including `104` RmlUi
    package/loose assets.

### Round 25 Evidence (2026-07-04)

Round 25 is accepted as the guarded smoke glyph proof. It replaces the Round 24
layout-only font bootstrap with a minimal `UI_Rml_SmokeFontEngineInterface`
that emits RmlUi mesh quads for ASCII smoke-route text. The route still does
not claim final font ownership, localization coverage, text shaping, or menu
parity, but the OpenGL runtime capture now proves actual text geometry rather
than only styled boxes.

- Implementation log:
  `docs-dev/rmlui-round25-smoke-font-glyph-path-2026-07-04.md`.
- `src/client/ui_rml/ui_rml_runtime.cpp` now emits untextured colored 5x7
  bitmap glyph quads through `Rml::TexturedMeshList`, returns matching smoke
  string widths, and logs
  `RmlUi smoke font engine generated glyph geometry` once glyph geometry is
  generated.
- `tools/ui_smoke/check_rmlui_runtime_adapter.py` now requires the
  glyph-generating smoke font path, and
  `tools/ui_smoke/check_rmlui_runtime_capture.py` now requires the glyph marker
  in the flushed log/condump evidence.
- `assets/ui/rml/core/runtime_smoke.rcss` now gives smoke text elements
  explicit block layout and fixed widths so the captured route is readable
  while the full font/layout service remains pending.
- Accepted live evidence:
  - `.install/basew/screenshots/rmlui_runtime_smoke_round25.tga`
  - `.install/basew/logs/rmlui_runtime_smoke_round25.log`
  - `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round25.tga`
  - `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round25.log`
  - `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round25.preview.png`
  - `.tmp/rmlui/runtime-capture/manifest.json`
- Final manifest facts included `ok=true`, `errors=[]`, `font_geometry_marker_seen=true`,
  `updates=24`, `renders=24`, OpenGL guarded route status, screenshot format
  `tga`, screenshot dimensions `960x720`, size `2073618`, fresh screenshot,
  and nonblank payload.
- Coordinator validation accepted:
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`:
    passed with `runtime_font_engine_adapter_present=true`.
  - `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py tools/ui_smoke/test_check_rmlui_runtime_capture.py`:
    passed with the focused adapter/capture suites.
  - `ninja -C builddir-win worr_x86_64.exe worr_engine_x86_64.dll worr_opengl_x86_64.dll worr_rtx_x86_64.dll`:
    passed for the default-disabled build targets.
  - `ninja -C .tmp\rmlui\round17-rmlui-enabled3 worr_engine_x86_64.dll worr_x86_64.exe worr_opengl_x86_64.dll`:
    passed and relinked the enabled scratch engine.
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`:
    passed and validated the staged payload, including `104` RmlUi
    package/loose assets.
  - `python tools\ui_smoke\check_rmlui_runtime_capture.py --run --engine-exe .tmp\rmlui\round17-rmlui-enabled3\worr_x86_64.exe --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\manifest.json --format json --timeout 120`:
    passed with `font_geometry_marker_seen=true` and copied log/screenshot
    evidence.

### Round 26 Evidence (2026-07-04)

Round 26 is accepted as the guarded capture layout proof. It keeps the Round
25 smoke glyph path and adds TGA pixel/layout assertions to the runtime
capture harness, so the accepted screenshot must contain the expected smoke
route colors and region relationships rather than merely being nonblank.
Normal menu routes still fall back to legacy UI; Vulkan/RTX-vkpt bridges,
full font/text services, synthetic input/back automation, live controllers,
runtime navigation, and parity remain pending.

- Implementation log:
  `docs-dev/rmlui-round26-capture-layout-assertions-2026-07-04.md`.
- `tools/ui_smoke/check_rmlui_runtime_capture.py` now parses uncompressed
  true-color TGA screenshots, counts smoke-route colors, records bounding
  boxes, and validates that the contract panel, panel border, text, accent
  text, and action buttons occupy the expected route structure.
- The JSON manifest now includes `layout_checked`, `layout_ok`,
  `layout_color_counts`, `layout_bounding_boxes`,
  `layout_button_fill_below_panel_count`, and `layout_assertions`.
- `tools/ui_smoke/test_check_rmlui_runtime_capture.py` now paints valid
  synthetic layout evidence and verifies that a nonblank but wrong-layout TGA
  fails the harness.
- Accepted live evidence:
  - `.install/basew/screenshots/rmlui_runtime_smoke_round26.tga`
  - `.install/basew/logs/rmlui_runtime_smoke_round26.log`
  - `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round26.tga`
  - `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round26.log`
  - `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round26.preview.png`
  - `.tmp/rmlui/runtime-capture/manifest.json`
- Final manifest facts included `ok=true`, `errors=[]`,
  `font_geometry_marker_seen=true`, `layout_checked=true`, `layout_ok=true`,
  `updates=24`, `renders=24`, OpenGL guarded route status, screenshot format
  `tga`, screenshot dimensions `960x720`, size `2073618`, fresh screenshot,
  and nonblank payload.
- Accepted layout facts included panel background bbox `[66, 221, 865, 424]`,
  panel border bbox `[64, 219, 867, 426]`, button fill bbox
  `[66, 247, 433, 498]`, body text bbox `[64, 130, 775, 390]`,
  `layout_button_fill_below_panel_count=15640`, and all `12` layout
  assertions set to `true`.
- Coordinator validation accepted:
  - `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_capture.py`:
    passed with the focused capture-layout suite.
  - `python tools\ui_smoke\check_rmlui_runtime_capture.py --run --engine-exe .tmp\rmlui\round17-rmlui-enabled3\worr_x86_64.exe --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\manifest.json --format json --timeout 120`:
    passed with `layout_ok=true` and copied log/screenshot evidence.

### Round 27 Evidence (2026-07-04)

Round 27 is accepted as the guarded synthetic input/back-close proof. It keeps
the Round 26 glyph and layout checks, then runs a developer-only synthetic
input command after the screenshot has been written. The accepted evidence must
show pointer, text, mouse-wheel, mouse-button, close-request, close-counter,
and inactive final status facts for `core.runtime_smoke`. Normal menu routes
still fall back to legacy UI; Vulkan/RTX-vkpt bridges, full font/text
services, broad input/navigation parity, live controllers, runtime navigation,
and parity remain pending.

- Implementation log:
  `docs-dev/rmlui-round27-synthetic-input-capture-2026-07-04.md`.
- `src/client/ui_rml/ui_rml.cpp` now exposes
  `ui_rml_runtime_synthetic_input`, route open/close/request/synthetic
  counters, and `RmlUi runtime route counters` status output.
- `tools/ui_smoke/check_rmlui_runtime_capture.py` now inserts
  `ui_rml_runtime_synthetic_input` into the live command sequence and requires
  `synthetic_input_marker_seen`, `inactive_status_seen`, positive input
  counters, and positive route close counters.
- `tools/ui_smoke/check_rmlui_runtime_adapter.py` now treats the synthetic
  input command and route counter status line as part of the guarded adapter
  boundary.
- Accepted live evidence:
  - `.install/basew/screenshots/rmlui_runtime_smoke_round27.tga`
  - `.install/basew/logs/rmlui_runtime_smoke_round27.log`
  - `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round27.tga`
  - `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round27.log`
  - `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round27.preview.png`
  - `.tmp/rmlui/runtime-capture/manifest.json`
- Final manifest facts included `ok=true`, `errors=[]`, `ran_engine=true`,
  `exit_code=0`, `font_geometry_marker_seen=true`, `layout_checked=true`,
  `layout_ok=true`, `synthetic_input_marker_seen=true`,
  `inactive_status_seen=true`, `updates=24`, `renders=24`,
  `input_keys=2`, `input_chars=1`, `input_mouse_moves=1`,
  `input_mouse_buttons=1`, `input_mouse_wheels=1`, `route_opens=1`,
  `route_closes=1`, `route_close_requests=1`, `route_synthetic_inputs=1`,
  screenshot format `tga`, screenshot dimensions `960x720`, size `2073618`,
  fresh screenshot, and nonblank payload.
- The Round 27 visual facts retained the Round 26 layout contract: panel
  background bbox `[66, 221, 865, 424]`, panel border bbox
  `[64, 219, 867, 426]`, button fill bbox `[66, 247, 433, 498]`, body text
  bbox `[64, 130, 775, 390]`, `layout_button_fill_below_panel_count=15640`,
  and all `12` layout assertions set to `true`.
- Coordinator validation accepted:
  - `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_capture.py`:
    passed with the focused capture-input suite.
  - `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py`:
    passed with the static adapter-boundary fixture.
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`:
    passed with `runtime_status_capture_commands_present=true`.
  - `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py tools/ui_smoke/test_check_rmlui_runtime_capture.py tools/ui_smoke/test_check_rmlui_dependency_integration.py tools/ui_smoke/test_check_rmlui_dependency_decision.py tools/ui_smoke/test_check_rmlui_cvar_inventory.py tools/ui_smoke/test_check_rmlui_runtime_assets.py`:
    passed with `52` focused RmlUi tests.
  - `python tools\ui_smoke\check_rmlui_runtime_capture.py --run --engine-exe .tmp\rmlui\round17-rmlui-enabled3\worr_x86_64.exe --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\manifest.json --format json --timeout 120`:
    passed with `synthetic_input_marker_seen=true`,
    `inactive_status_seen=true`, and copied log/screenshot evidence.

### Round 28 Evidence (2026-07-04)

Round 28 is accepted as the first guarded viewport-matrix proof for the
OpenGL `core.runtime_smoke` route. It keeps the Round 27 glyph, layout,
synthetic input, back-close, route teardown, and inactive-status assertions,
then runs them across two explicit window geometries. Normal menu routes still
fall back to legacy UI; responsive widescreen parity, Vulkan/RTX-vkpt
bridges, full font/text services, live controllers, runtime navigation, and
parity remain pending.

- Implementation log:
  `docs-dev/rmlui-round28-viewport-matrix-2026-07-04.md`.
- `tools/ui_smoke/check_rmlui_runtime_capture.py` now accepts
  `--geometry WIDTHxHEIGHT`, validates exact screenshot dimensions for
  geometry-driven captures, and exposes `--matrix` for the default viewport
  matrix.
- The accepted matrix appends viewport names to the evidence ID stem and
  writes aggregate per-viewport results to
  `.tmp/rmlui/runtime-capture/manifest.json`.
- `tools/ui_smoke/test_check_rmlui_runtime_capture.py` now covers matrix
  dry-runs, successful two-viewport evidence, exact dimension mismatch
  failure, and the existing glyph/layout/input failure modes.
- Accepted live evidence:
  - `.install/basew/screenshots/rmlui_runtime_smoke_round28_default_960x720.tga`
  - `.install/basew/logs/rmlui_runtime_smoke_round28_default_960x720.log`
  - `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round28_default_960x720.tga`
  - `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round28_default_960x720.log`
  - `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round28_default_960x720.preview.png`
  - `.install/basew/screenshots/rmlui_runtime_smoke_round28_large_1280x960.tga`
  - `.install/basew/logs/rmlui_runtime_smoke_round28_large_1280x960.log`
  - `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round28_large_1280x960.tga`
  - `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round28_large_1280x960.log`
  - `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round28_large_1280x960.preview.png`
  - `.tmp/rmlui/runtime-capture/manifest.json`
- Final aggregate manifest facts included `ok=true`, `viewports=2`,
  `passed=2`, `failed=0`, and `errors=[]`.
- `default_960x720` facts included `ran_engine=true`, `exit_code=0`,
  `screenshot_fresh=true`, `screenshot_dimensions=[960, 720]`,
  `expected_dimensions=[960, 720]`, `layout_ok=true`,
  `font_geometry_marker_seen=true`, `synthetic_input_marker_seen=true`,
  `inactive_status_seen=true`, `route_closes=1`, and
  `route_close_requests=1`.
- `large_1280x960` facts included `ran_engine=true`, `exit_code=0`,
  `screenshot_fresh=true`, `screenshot_dimensions=[1280, 960]`,
  `expected_dimensions=[1280, 960]`, `layout_ok=true`,
  `font_geometry_marker_seen=true`, `synthetic_input_marker_seen=true`,
  `inactive_status_seen=true`, `route_closes=1`, and
  `route_close_requests=1`.
- Accepted layout facts retained all `12` layout assertions in both viewports.
  The larger viewport recorded panel background bbox `[132, 442, 1279, 849]`,
  panel border bbox `[128, 438, 1279, 853]`, button fill bbox
  `[132, 493, 867, 959]`, body text bbox `[128, 259, 1279, 780]`, and
  `layout_button_fill_below_panel_count=35940`.
- Coordinator validation accepted:
  - `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_capture.py`:
    passed with `10` focused capture tests.
  - `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py tools/ui_smoke/test_check_rmlui_runtime_capture.py tools/ui_smoke/test_check_rmlui_dependency_integration.py tools/ui_smoke/test_check_rmlui_dependency_decision.py tools/ui_smoke/test_check_rmlui_cvar_inventory.py tools/ui_smoke/test_check_rmlui_runtime_assets.py`:
    passed with `55` focused RmlUi tests.
  - `python tools\ui_smoke\check_rmlui_runtime_capture.py --run --matrix --engine-exe .tmp\rmlui\round17-rmlui-enabled3\worr_x86_64.exe --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\manifest.json --format json --timeout 120`:
    passed with `viewports=2`, `passed=2`, and copied per-viewport log/TGA
    evidence.

### Round 29 Evidence (2026-07-04)

Round 29 is accepted as the first guarded normal-menu-entrypoint OpenGL route
proof. It keeps the `ui_rml_enable` opt-in gate, widens the guarded runtime
allow-list to the three existing `runtime_stub` menu routes (`main`, `game`,
and `download_status`), and adds `ui_rml_runtime_capture_menu` so the capture
harness opens those routes through `UI_OpenMenu`. This does not claim final
theme/layout parity, route navigation, live controller behavior, Vulkan/RTX-vkpt
renderer support, final font/text services, or legacy JSON removal.

- Implementation log:
  `docs-dev/rmlui-round29-menu-route-capture-2026-07-04.md`.
- `src/client/ui_rml/ui_rml.cpp` now keeps a guarded menu route table mapping
  `main`, `game`, and `download_status` to `UIMENU_MAIN`, `UIMENU_GAME`, and
  `UIMENU_DOWNLOAD`, and `UI_Rml_RuntimeRouteIsAllowed` accepts those route IDs
  alongside `core.runtime_smoke`.
- `src/client/ui_rml/ui_rml_runtime.cpp` mirrors the same route allow-list in
  the compiled RmlUi adapter while preserving the OpenGL-only guarded context
  path.
- `tools/ui_smoke/check_rmlui_runtime_capture.py` now accepts
  `--route-matrix`, captures all three menu routes at `960x720`, and validates
  route-specific active OpenGL status, exact dimensions, glyph text evidence,
  synthetic input, close counters, and inactive final status.
- Menu-route captures set `layout_required=false`; the pixel/color layout
  contract remains scoped to `core.runtime_smoke`.
- Accepted live evidence:
  - `.install/basew/screenshots/rmlui_runtime_smoke_round29_main.tga`
  - `.install/basew/logs/rmlui_runtime_smoke_round29_main.log`
  - `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round29_main.tga`
  - `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round29_main.log`
  - `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round29_main.preview.png`
  - `.install/basew/screenshots/rmlui_runtime_smoke_round29_game.tga`
  - `.install/basew/logs/rmlui_runtime_smoke_round29_game.log`
  - `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round29_game.tga`
  - `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round29_game.log`
  - `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round29_game.preview.png`
  - `.install/basew/screenshots/rmlui_runtime_smoke_round29_download_status.tga`
  - `.install/basew/logs/rmlui_runtime_smoke_round29_download_status.log`
  - `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round29_download_status.tga`
  - `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round29_download_status.log`
  - `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round29_download_status.preview.png`
  - `.tmp/rmlui/runtime-capture/manifest.json`
- Final aggregate manifest facts included `ok=true`, `routes=3`, `passed=3`,
  `failed=0`, and `errors=0`.
- Each route recorded `ran_engine=true`, `exit_code=0`,
  `screenshot_fresh=true`, `screenshot_dimensions=[960, 720]`,
  `expected_dimensions=[960, 720]`, `route_document_exists=true`,
  `guarded_opengl_status_seen=true`, `font_geometry_marker_seen=true`,
  `synthetic_input_marker_seen=true`, `inactive_status_seen=true`,
  `route_opens=1`, `route_closes=1`, `route_close_requests=1`,
  `route_synthetic_inputs=1`, and `layout_required=false`.
- Coordinator validation accepted:
  - `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_capture.py`:
    passed with `12` focused capture tests.
  - `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py`:
    passed with `8` focused adapter tests.
  - `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py tools/ui_smoke/test_check_rmlui_runtime_capture.py tools/ui_smoke/test_check_rmlui_dependency_integration.py tools/ui_smoke/test_check_rmlui_dependency_decision.py tools/ui_smoke/test_check_rmlui_cvar_inventory.py tools/ui_smoke/test_check_rmlui_runtime_assets.py tools/ui_smoke/test_check_rmlui_menu_entrypoints.py tools/ui_smoke/test_check_rmlui_runtime_stub_eligibility.py`:
    passed with `69` focused RmlUi tests.
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`:
    passed with `route_open_guard_present=true` and `errors=0`.
  - `python tools\ui_smoke\check_rmlui_menu_entrypoints.py`:
    passed with `3` unique mapped routes.
  - `python tools\ui_smoke\check_rmlui_runtime_stub_eligibility.py`:
    passed with `3` `runtime_stub` routes checked.
  - `ninja -C builddir-win worr_x86_64.exe worr_engine_x86_64.dll worr_opengl_x86_64.dll worr_rtx_x86_64.dll`:
    passed.
  - `ninja -C .tmp\rmlui\round17-rmlui-enabled3 worr_engine_x86_64.dll worr_x86_64.exe worr_opengl_x86_64.dll`:
    passed.
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`:
    passed and refreshed `.install/`.
  - `python tools\ui_smoke\check_rmlui_runtime_capture.py --run --route-matrix --engine-exe .tmp\rmlui\round17-rmlui-enabled3\worr_x86_64.exe --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\manifest.json --format json --timeout 120`:
    passed with `routes=3`, `passed=3`, and copied per-route log/TGA
    evidence.

### Round 30 Evidence (2026-07-04)

Round 30 is accepted as renderer-family matrix guardrail evidence for the
guarded RmlUi runtime path. It does not add native Vulkan or RTX/vkpt drawing;
instead, it makes the current matrix explicit: OpenGL is the single guarded
native lane, Vulkan and RTX/vkpt remain blocked until native bridges exist,
and any Vulkan/RTX-to-OpenGL shortcut wiring fails validation.

- Implementation log:
  `docs-dev/rmlui-round30-renderer-matrix-guardrails-2026-07-04.md`.
- `tools/ui_smoke/check_rmlui_renderer_matrix.py` validates:
  - renderer-family declarations and renderer export hooks in
    `inc/renderer/renderer.h`;
  - OpenGL native hook exports from `src/renderer/renderer_api.c`;
  - non-OpenGL external renderer exports returning `family=NONE`,
    `CanRender=false`, and `NativeRenderInterface=NULL`;
  - OpenGL-scoped Meson runtime dependency wiring;
  - Vulkan/RTX runtime dependencies remaining disabled until native bridges
    exist;
  - client renderer family mappings staying distinct;
  - client renderer bridge cleanup when no native RmlUi family is registered;
  - runtime capture automation remaining explicitly `r_renderer=opengl`.
- `tools/ui_smoke/test_check_rmlui_renderer_matrix.py` covers the accepted
  matrix plus failures for OpenGL `CanRender=false`, Vulkan mapped to OpenGL,
  premature Vulkan runtime dependency enablement, and a non-OpenGL capture
  harness.
- Accepted checker facts include `native_guarded_lanes=1`, `blocked_lanes=2`,
  `errors=0`, `opengl.expected_status=native_guarded`,
  `vulkan.expected_status=blocked_until_native`,
  `rtx_vkpt.expected_status=blocked_until_native`, and
  `no_vulkan_or_rtx_to_opengl_redirect=true`.
- Coordinator validation accepted:
  - `python -m pytest tools\ui_smoke\test_check_rmlui_renderer_matrix.py`:
    passed with `6` focused renderer-matrix tests.
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_renderer_matrix.py tools\ui_smoke\test_check_rmlui_runtime_capture.py tools\ui_smoke\test_check_rmlui_dependency_integration.py tools\ui_smoke\test_check_rmlui_dependency_decision.py tools\ui_smoke\test_check_rmlui_cvar_inventory.py tools\ui_smoke\test_check_rmlui_runtime_assets.py tools\ui_smoke\test_check_rmlui_menu_entrypoints.py tools\ui_smoke\test_check_rmlui_runtime_stub_eligibility.py`:
    passed with `75` focused RmlUi tests.
  - `python tools\ui_smoke\check_rmlui_renderer_matrix.py --format json`:
    passed with the accepted lane counts and `errors=0`.
  - `python tools\ui_smoke\check_rmlui_dependency_decision.py --format json`:
    passed with `ok=true` and `errors=[]`.
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`:
    passed with `ok=true` and `errors=[]`.

### Round 31 Evidence (2026-07-05)

Round 31 is accepted as aggregate renderer-matrix capture-manifest evidence.
It does not implement native Vulkan or RTX/vkpt rendering. It adds a
`--renderer-matrix` mode to the guarded runtime capture harness so the current
OpenGL route-visible evidence and the blocked non-OpenGL lane guardrails are
reported together.

- Implementation log:
  `docs-dev/rmlui-round31-renderer-matrix-capture-manifest-2026-07-05.md`.
- `tools/ui_smoke/check_rmlui_runtime_capture.py --renderer-matrix` now
  builds the guarded `main`, `game`, and `download_status` OpenGL route
  reports and embeds the Round 30 renderer-family guardrail payload in the
  same JSON/text output.
- The aggregate report fails if OpenGL route evidence fails, if the static
  renderer-family guardrail fails, or if both fail.
- Dry-run output prints the OpenGL route-matrix commands and the current lane
  policy: `OpenGL=native_guarded`, `Vulkan=blocked_until_native`, and
  `RTX/vkpt=blocked_until_native`.
- Accepted local aggregate evidence:
  - `.tmp/rmlui/runtime-capture/renderer-matrix.json`
- Accepted aggregate manifest facts included `ok=true`, `routes=3`,
  `route_passed=3`, `route_failed=0`, `renderer_lanes=3`,
  `native_guarded_lanes=1`, `blocked_lanes=2`, and `errors=0`.
- The OpenGL route entries retained the accepted Round 29 evidence facts:
  route-specific active OpenGL status, exact `960x720` screenshots, glyph text
  evidence, synthetic input, close counters, inactive final status, and
  `layout_required=false`.
- Coordinator validation accepted:
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_capture.py`:
    passed with `15` focused capture tests.
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_renderer_matrix.py tools\ui_smoke\test_check_rmlui_runtime_capture.py tools\ui_smoke\test_check_rmlui_dependency_integration.py tools\ui_smoke\test_check_rmlui_dependency_decision.py tools\ui_smoke\test_check_rmlui_cvar_inventory.py tools\ui_smoke\test_check_rmlui_runtime_assets.py tools\ui_smoke\test_check_rmlui_menu_entrypoints.py tools\ui_smoke\test_check_rmlui_runtime_stub_eligibility.py`:
    passed with `78` focused RmlUi tests.
  - `python tools\ui_smoke\check_rmlui_runtime_capture.py --dry-run --renderer-matrix`:
    passed and printed the OpenGL route commands plus the blocked-lane
    summary.
  - `python tools\ui_smoke\check_rmlui_runtime_capture.py --renderer-matrix --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\renderer-matrix.json --format json`:
    passed with the accepted aggregate counts above.
  - `python tools\ui_smoke\check_rmlui_renderer_matrix.py --format json`:
    passed with `native_guarded_lanes=1`, `blocked_lanes=2`, and `errors=0`.
  - `python tools\ui_smoke\check_rmlui_dependency_decision.py --format json`:
    passed with `ok=true` and `errors=[]`.

### Round 32 Evidence (2026-07-05)

Round 32 is accepted as Vulkan/RTX bridge-readiness audit evidence. It does
not implement native Vulkan or RTX/vkpt RmlUi rendering. It adds a static
audit for the renderer-native foundations future bridges should use and keeps
both non-OpenGL lanes blocked until their native bridge requirements exist.

- Implementation log:
  `docs-dev/rmlui-round32-vulkan-rtx-bridge-readiness-2026-07-05.md`.
- `tools/ui_smoke/check_rmlui_vulkan_bridge_readiness.py` now validates:
  - Vulkan and RTX/vkpt family lanes remain distinct and do not map to
    OpenGL;
  - non-OpenGL renderer API exports still report `family=NONE`,
    `CanRender=false`, and `NativeRenderInterface=NULL`;
  - Vulkan/RTX Meson runtime dependencies remain disabled until native
    bridges exist;
  - Vulkan UI draw, frame-recording, texture, descriptor, and clip/scissor
    foundations remain present;
  - RTX/vkpt stretch-pic draw, submit, texture, descriptor, clip, and shader
    foundations remain present.
- Accepted checker facts included `ok=true`, `lanes=2`,
  `foundation_lanes=2`, `native_bridge_lanes=0`, `blocked_lanes=2`,
  `missing_bridge_requirements=8`, and `errors=0`.
- The missing requirements are intentional for this round: renderer-owned
  Vulkan and RTX/vkpt `Rml::RenderInterface` classes, family exports, runtime
  dependencies, and non-null native render-interface exports.
- Coordinator validation accepted:
  - `python -m pytest tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py`:
    passed with `6` focused bridge-readiness tests.
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_renderer_matrix.py tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py tools\ui_smoke\test_check_rmlui_dependency_integration.py tools\ui_smoke\test_check_rmlui_dependency_decision.py tools\ui_smoke\test_check_rmlui_cvar_inventory.py tools\ui_smoke\test_check_rmlui_runtime_assets.py tools\ui_smoke\test_check_rmlui_menu_entrypoints.py tools\ui_smoke\test_check_rmlui_runtime_stub_eligibility.py`:
    passed with `84` focused RmlUi tests.
  - `python tools\ui_smoke\check_rmlui_vulkan_bridge_readiness.py`:
    passed with `Malformed findings: 0`.
  - `python tools\ui_smoke\check_rmlui_vulkan_bridge_readiness.py --format json`:
    passed with the accepted counts above.
  - `python tools\ui_smoke\check_rmlui_renderer_matrix.py --format json`:
    passed with `native_guarded_lanes=1`, `blocked_lanes=2`, and `errors=0`.
  - `python tools\ui_smoke\check_rmlui_dependency_decision.py --format json`:
    passed with `ok=true` and `errors=[]`.
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`:
    passed with `ok=true` and `errors=[]`.

### Round 33 Evidence (2026-07-05)

Round 33 is accepted as bridge-readiness aggregate renderer-manifest evidence.
It does not implement native Vulkan or RTX/vkpt rendering. It connects the
Round 32 bridge-readiness audit to the existing `--renderer-matrix` runtime
capture manifest so OpenGL route evidence, renderer-family guardrails, and
Vulkan/RTX bridge-readiness facts are reviewed together.

- Implementation log:
  `docs-dev/rmlui-round33-bridge-readiness-renderer-manifest-2026-07-05.md`.
- `tools/ui_smoke/check_rmlui_runtime_capture.py --renderer-matrix` now
  embeds:
  - `opengl_route_matrix`;
  - `renderer_guardrail`;
  - `bridge_readiness`.
- The aggregate report fails if any of those three evidence groups fails.
- Dry-run output prints both renderer lane policy and bridge-readiness lane
  policy.
- Accepted local aggregate evidence:
  - `.tmp/rmlui/runtime-capture/renderer-matrix.json`
- Accepted aggregate manifest facts included `ok=true`, `routes=3`,
  `route_passed=3`, `route_failed=0`, `renderer_lanes=3`,
  `native_guarded_lanes=1`, `blocked_lanes=2`, `bridge_lanes=2`,
  `bridge_foundation_lanes=2`, `native_bridge_lanes=0`,
  `bridge_blocked_lanes=2`, `missing_bridge_requirements=8`, and `errors=0`.
- Coordinator validation accepted:
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_capture.py`:
    passed with `16` focused capture tests.
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_renderer_matrix.py tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py tools\ui_smoke\test_check_rmlui_dependency_integration.py tools\ui_smoke\test_check_rmlui_dependency_decision.py tools\ui_smoke\test_check_rmlui_cvar_inventory.py tools\ui_smoke\test_check_rmlui_runtime_assets.py tools\ui_smoke\test_check_rmlui_menu_entrypoints.py tools\ui_smoke\test_check_rmlui_runtime_stub_eligibility.py`:
    passed with `85` focused RmlUi tests.
  - `python tools\ui_smoke\check_rmlui_runtime_capture.py --dry-run --renderer-matrix`:
    passed and printed the OpenGL route commands, renderer lane policy, and
    bridge-readiness lane policy.
  - `python tools\ui_smoke\check_rmlui_runtime_capture.py --renderer-matrix --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\renderer-matrix.json --format json`:
    passed with the accepted aggregate counts above.
  - `python tools\ui_smoke\check_rmlui_vulkan_bridge_readiness.py --format json`:
    passed with `foundation_lanes=2`, `native_bridge_lanes=0`, and `errors=0`.
  - `python tools\ui_smoke\check_rmlui_dependency_decision.py --format json`:
    passed with `ok=true` and `errors=[]`.
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`:
    passed with `ok=true` and `errors=[]`.

### Round 34 Evidence (2026-07-05)

Round 34 is accepted as native bridge activation checklist evidence. It does
not implement native Vulkan or RTX/vkpt rendering. It adds named activation
requirements to the bridge-readiness audit and carries those counts into the
aggregate `--renderer-matrix` manifest.

- Implementation log:
  `docs-dev/rmlui-round34-native-bridge-activation-checklist-2026-07-05.md`.
- Each non-OpenGL bridge-readiness lane now reports:
  - `native_bridge_class_present`;
  - `native_family_export_present`;
  - `runtime_dependency_enabled`;
  - `native_interface_export_present`.
- Standalone bridge-readiness output now records `activation_requirements=8`,
  `satisfied_activation_requirements=0`,
  `pending_activation_requirements=8`, `missing_bridge_requirements=8`, and
  `errors=0`.
- Aggregate renderer manifests now record
  `bridge_activation_requirements=8`,
  `bridge_satisfied_activation_requirements=0`,
  `bridge_pending_activation_requirements=8`,
  `missing_bridge_requirements=8`, and `errors=0`.
- A new partial Vulkan bridge-class regression test records
  `satisfied_activation_requirements=1` and keeps the lane failed until the
  full native Vulkan bridge is wired.
- Accepted local aggregate evidence:
  - `.tmp/rmlui/runtime-capture/renderer-matrix.json`
- Coordinator validation accepted:
  - `python -m pytest tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py`:
    passed with `23` focused bridge/capture tests.
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_renderer_matrix.py tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py tools\ui_smoke\test_check_rmlui_dependency_integration.py tools\ui_smoke\test_check_rmlui_dependency_decision.py tools\ui_smoke\test_check_rmlui_cvar_inventory.py tools\ui_smoke\test_check_rmlui_runtime_assets.py tools\ui_smoke\test_check_rmlui_menu_entrypoints.py tools\ui_smoke\test_check_rmlui_runtime_stub_eligibility.py`:
    passed with `87` focused RmlUi tests.
  - `python tools\ui_smoke\check_rmlui_vulkan_bridge_readiness.py --format json`:
    passed with the accepted standalone activation counts above.
  - `python tools\ui_smoke\check_rmlui_runtime_capture.py --renderer-matrix --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\renderer-matrix.json --format json`:
    passed with the accepted aggregate activation counts above.
  - `python tools\ui_smoke\check_rmlui_dependency_decision.py --format json`:
    passed with `ok=true` and `errors=[]`.
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`:
    passed with `ok=true` and `errors=[]`.

### Round 35 Evidence (2026-07-05)

Round 35 is accepted as native bridge activation status evidence. It does not
implement native Vulkan or RTX/vkpt rendering. It adds activation-stage and
next-blocker reporting to the Round 34 checklist so future native bridge work
can be reviewed as no activation, partial blocked activation, or complete
activation.

- Implementation log:
  `docs-dev/rmlui-round35-native-bridge-activation-status-2026-07-05.md`.
- Each non-OpenGL bridge-readiness lane now reports:
  - `activation_status`;
  - `activation_complete`;
  - `satisfied_activation_requirement_ids`;
  - `pending_activation_requirement_ids`;
  - `next_activation_requirement`.
- Standalone bridge-readiness output now records
  `activation_complete_lanes=0`, `partial_activation_lanes=0`,
  `inactive_activation_lanes=2`, `activation_requirements=8`,
  `satisfied_activation_requirements=0`,
  `pending_activation_requirements=8`, `missing_bridge_requirements=8`, and
  `errors=0`.
- Aggregate renderer manifests now record
  `bridge_activation_complete_lanes=0`,
  `bridge_partial_activation_lanes=0`,
  `bridge_inactive_activation_lanes=2`,
  `bridge_activation_requirements=8`,
  `bridge_satisfied_activation_requirements=0`,
  `bridge_pending_activation_requirements=8`,
  `missing_bridge_requirements=8`, and `errors=0`.
- A partial Vulkan bridge-class regression test now records
  `activation_status=partial_activation_blocked` and
  `next_activation_requirement=native_family_export_present` while still
  failing the lane.
- Accepted local aggregate evidence:
  - `.tmp/rmlui/runtime-capture/renderer-matrix.json`
- Coordinator validation accepted:
  - `python -m pytest tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py`:
    passed with `23` focused bridge/capture tests.
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_renderer_matrix.py tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py tools\ui_smoke\test_check_rmlui_dependency_integration.py tools\ui_smoke\test_check_rmlui_dependency_decision.py tools\ui_smoke\test_check_rmlui_cvar_inventory.py tools\ui_smoke\test_check_rmlui_runtime_assets.py tools\ui_smoke\test_check_rmlui_menu_entrypoints.py tools\ui_smoke\test_check_rmlui_runtime_stub_eligibility.py`:
    passed with `87` focused RmlUi tests.
  - `python tools\ui_smoke\check_rmlui_vulkan_bridge_readiness.py --format json`:
    passed with the accepted standalone activation-status counts above.
  - `python tools\ui_smoke\check_rmlui_runtime_capture.py --renderer-matrix --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\renderer-matrix.json --format json`:
    passed with the accepted aggregate activation-status counts above.
  - `python tools\ui_smoke\check_rmlui_dependency_decision.py --format json`:
    passed with `ok=true` and `errors=[]`.
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`:
    passed with `ok=true` and `errors=[]`.

### Round 36 Evidence (2026-07-05)

Round 36 is accepted as native bridge source-set activation evidence. It does
not implement native Vulkan or RTX/vkpt rendering. It adds source-set wiring as
an explicit activation requirement so a non-OpenGL bridge class cannot count
unless the target renderer DLL also compiles the bridge source.

- Implementation log:
  `docs-dev/rmlui-round36-native-bridge-source-set-activation-2026-07-05.md`.
- Each non-OpenGL bridge-readiness lane now includes:
  - `native_bridge_source_compiled`.
- Standalone bridge-readiness output now records
  `activation_complete_lanes=0`, `partial_activation_lanes=0`,
  `inactive_activation_lanes=2`, `activation_requirements=10`,
  `satisfied_activation_requirements=0`,
  `pending_activation_requirements=10`, `missing_bridge_requirements=10`, and
  `errors=0`.
- Aggregate renderer manifests now record
  `bridge_activation_complete_lanes=0`,
  `bridge_partial_activation_lanes=0`,
  `bridge_inactive_activation_lanes=2`,
  `bridge_activation_requirements=10`,
  `bridge_satisfied_activation_requirements=0`,
  `bridge_pending_activation_requirements=10`,
  `missing_bridge_requirements=10`, and `errors=0`.
- A partial Vulkan bridge-class regression test now keeps the lane failed with
  `next_activation_requirement=native_bridge_source_compiled`.
- Accepted local aggregate evidence:
  - `.tmp/rmlui/runtime-capture/renderer-matrix.json`
- Coordinator validation accepted:
  - `python -m pytest tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py`:
    passed with `23` focused bridge/capture tests.
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_renderer_matrix.py tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py tools\ui_smoke\test_check_rmlui_dependency_integration.py tools\ui_smoke\test_check_rmlui_dependency_decision.py tools\ui_smoke\test_check_rmlui_cvar_inventory.py tools\ui_smoke\test_check_rmlui_runtime_assets.py tools\ui_smoke\test_check_rmlui_menu_entrypoints.py tools\ui_smoke\test_check_rmlui_runtime_stub_eligibility.py`:
    passed with `86` focused RmlUi tests.
  - `python tools\ui_smoke\check_rmlui_vulkan_bridge_readiness.py --format json`:
    passed with the accepted standalone source-set activation counts above.
  - `python tools\ui_smoke\check_rmlui_runtime_capture.py --renderer-matrix --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\renderer-matrix.json --format json`:
    passed with the accepted aggregate source-set activation counts above.
  - `python tools\ui_smoke\check_rmlui_dependency_decision.py --format json`:
    passed with `ok=true` and `errors=[]`.
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`:
    passed with `ok=true` and `errors=[]`.

### Round 37 Evidence (2026-07-05)

Round 37 is accepted as inactive non-OpenGL bridge source wiring evidence. It
does not implement native Vulkan or RTX/vkpt rendering. It wires the shared
RmlUi renderer bridge source into the Vulkan and RTX/vkpt renderer source sets
without enabling RmlUi runtime dependencies, native family claims, or native
render-interface exports for those lanes.

- Implementation log:
  `docs-dev/rmlui-round37-inactive-non-gl-bridge-source-wiring-2026-07-05.md`.
- `meson.build` now includes `src/renderer/rmlui_bridge.cpp` in:
  - `renderer_vk_src`;
  - `renderer_vk_rtx_src`.
- `renderer_vk_cpp_args`, `renderer_vk_rtx_cpp_args`, `renderer_vk_deps`, and
  `renderer_vk_rtx_deps` remain free of RmlUi runtime enablement.
- Standalone bridge-readiness output now records
  `activation_complete_lanes=0`, `partial_activation_lanes=2`,
  `inactive_activation_lanes=0`, `activation_requirements=10`,
  `satisfied_activation_requirements=2`,
  `pending_activation_requirements=8`, `missing_bridge_requirements=8`, and
  `errors=0`.
- Aggregate renderer manifests now record
  `bridge_activation_complete_lanes=0`,
  `bridge_partial_activation_lanes=2`,
  `bridge_inactive_activation_lanes=0`,
  `bridge_activation_requirements=10`,
  `bridge_satisfied_activation_requirements=2`,
  `bridge_pending_activation_requirements=8`,
  `missing_bridge_requirements=8`, and `errors=0`.
- Each non-OpenGL lane now satisfies only
  `native_bridge_source_compiled=true` and remains
  `partial_activation_blocked`.
- Runtime-adapter validation now records `renderer_bridge_meson_occurrences=3`
  and requires the bridge source in `renderer_src`, `renderer_vk_rtx_src`, and
  `renderer_vk_src`.
- Both non-OpenGL lanes still report
  `next_activation_requirement=native_bridge_class_present`.
- Accepted local aggregate evidence:
  - `.tmp/rmlui/runtime-capture/renderer-matrix.json`
- Coordinator validation accepted:
  - `python -m pytest tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py`:
    passed with `23` focused bridge/capture tests.
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_renderer_matrix.py tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py tools\ui_smoke\test_check_rmlui_dependency_integration.py tools\ui_smoke\test_check_rmlui_dependency_decision.py tools\ui_smoke\test_check_rmlui_cvar_inventory.py tools\ui_smoke\test_check_rmlui_runtime_assets.py tools\ui_smoke\test_check_rmlui_menu_entrypoints.py tools\ui_smoke\test_check_rmlui_runtime_stub_eligibility.py`:
    passed with `86` focused RmlUi tests.
  - `python tools\ui_smoke\check_rmlui_vulkan_bridge_readiness.py --format json`:
    passed with the accepted inactive bridge source wiring counts above.
  - `python tools\ui_smoke\check_rmlui_runtime_capture.py --renderer-matrix --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\renderer-matrix.json --format json`:
    passed with the accepted aggregate inactive bridge source wiring counts
    above.
  - `python tools\ui_smoke\check_rmlui_dependency_decision.py --format json`:
    passed with `ok=true` and `errors=[]`.
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`:
    passed with `ok=true` and `errors=[]`.

### Round 38 Evidence (2026-07-05)

Round 38 is accepted as inactive non-OpenGL bridge class-stub evidence. It
does not implement native Vulkan or RTX/vkpt rendering. It adds inert
Vulkan and RTX/vkpt `Rml::RenderInterface` class stubs while keeping
non-OpenGL renderer-family exports, RmlUi runtime dependencies, native
render-interface exports, and route-visible captures blocked.

- Implementation log:
  `docs-dev/rmlui-round38-inactive-non-gl-bridge-class-stubs-2026-07-05.md`.
- `src/renderer/rmlui_bridge.cpp` now declares:
  - `R_RmlUiVulkanRenderInterface`;
  - `R_RmlUiRtxVkptRenderInterface`.
- The OpenGL render-interface implementation and OpenGL native export remain
  guarded to the OpenGL renderer family.
- Standalone bridge-readiness output now records
  `activation_complete_lanes=0`, `partial_activation_lanes=2`,
  `inactive_activation_lanes=0`, `activation_requirements=10`,
  `satisfied_activation_requirements=4`,
  `pending_activation_requirements=6`, `missing_bridge_requirements=6`, and
  `errors=0`.
- Aggregate renderer manifests now record
  `bridge_activation_complete_lanes=0`,
  `bridge_partial_activation_lanes=2`,
  `bridge_inactive_activation_lanes=0`,
  `bridge_activation_requirements=10`,
  `bridge_satisfied_activation_requirements=4`,
  `bridge_pending_activation_requirements=6`,
  `missing_bridge_requirements=6`, and `errors=0`.
- Each non-OpenGL lane now satisfies
  `native_bridge_class_present=true` and
  `native_bridge_source_compiled=true`, but remains
  `partial_activation_blocked`.
- Both non-OpenGL lanes now report
  `next_activation_requirement=native_family_export_present`.
- Accepted local aggregate evidence:
  - `.tmp/rmlui/runtime-capture/renderer-matrix.json`
- Coordinator validation accepted:
  - `python -m pytest tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py tools\ui_smoke\test_check_rmlui_runtime_adapter.py`:
    passed with `32` focused bridge/capture/adapter tests.
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_renderer_matrix.py tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py tools\ui_smoke\test_check_rmlui_dependency_integration.py tools\ui_smoke\test_check_rmlui_dependency_decision.py tools\ui_smoke\test_check_rmlui_cvar_inventory.py tools\ui_smoke\test_check_rmlui_runtime_assets.py tools\ui_smoke\test_check_rmlui_menu_entrypoints.py tools\ui_smoke\test_check_rmlui_runtime_stub_eligibility.py`:
    passed with `87` focused RmlUi tests.
  - `python tools\ui_smoke\check_rmlui_vulkan_bridge_readiness.py --format json`:
    passed with the accepted inactive bridge class-stub counts above.
  - `python tools\ui_smoke\check_rmlui_runtime_capture.py --renderer-matrix --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\renderer-matrix.json --format json`:
    passed with the accepted aggregate inactive bridge class-stub counts
    above.
  - `python tools\ui_smoke\check_rmlui_renderer_matrix.py --format json`:
    passed with `native_guarded_lanes=1`, `blocked_lanes=2`, and `errors=0`.
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`:
    passed with `ok=true` and `errors=[]`.
  - `python tools\ui_smoke\check_rmlui_dependency_decision.py --format json`:
    passed with `ok=true` and `errors=[]`.

### Round 39 Evidence (2026-07-05)

Round 39 is accepted as inactive non-OpenGL renderer-family export evidence.
It does not implement native Vulkan or RTX/vkpt rendering. It lets the Vulkan
and RTX/vkpt renderer DLLs report distinct RmlUi family identities while
keeping non-OpenGL RmlUi runtime dependencies, `CanRender`, native
render-interface exports, and route-visible captures blocked.

- Implementation log:
  `docs-dev/rmlui-round39-inactive-non-gl-family-exports-2026-07-05.md`.
- `src/renderer/rmlui_bridge.cpp` now reports:
  - `R_RENDERER_RMLUI_FAMILY_VULKAN` for `RENDERER_VULKAN_LEGACY`;
  - `R_RENDERER_RMLUI_FAMILY_RTX_VKPT` for `RENDERER_VULKAN_RTX`.
- `src/renderer/renderer_api.c` exports renderer family/name hooks for every
  renderer family, but non-OpenGL builds still export `CanRender=false` and a
  null native interface.
- Standalone bridge-readiness output now records
  `activation_complete_lanes=0`, `partial_activation_lanes=2`,
  `inactive_activation_lanes=0`, `activation_requirements=10`,
  `satisfied_activation_requirements=6`,
  `pending_activation_requirements=4`, `missing_bridge_requirements=4`, and
  `errors=0`.
- Aggregate renderer manifests now record
  `bridge_activation_complete_lanes=0`,
  `bridge_partial_activation_lanes=2`,
  `bridge_inactive_activation_lanes=0`,
  `bridge_activation_requirements=10`,
  `bridge_satisfied_activation_requirements=6`,
  `bridge_pending_activation_requirements=4`,
  `missing_bridge_requirements=4`, and `errors=0`.
- Each non-OpenGL lane now satisfies
  `native_bridge_class_present=true`,
  `native_bridge_source_compiled=true`, and
  `native_family_export_present=true`, but remains
  `partial_activation_blocked`.
- Both non-OpenGL lanes now report
  `next_activation_requirement=runtime_dependency_enabled`.
- Accepted local aggregate evidence:
  - `.tmp/rmlui/runtime-capture/renderer-matrix.json`
- Coordinator validation accepted:
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_renderer_matrix.py tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py tools\ui_smoke\test_check_rmlui_dependency_integration.py tools\ui_smoke\test_check_rmlui_dependency_decision.py tools\ui_smoke\test_check_rmlui_cvar_inventory.py tools\ui_smoke\test_check_rmlui_runtime_assets.py tools\ui_smoke\test_check_rmlui_menu_entrypoints.py tools\ui_smoke\test_check_rmlui_runtime_stub_eligibility.py`:
    passed with `87` focused RmlUi tests.
  - `python tools\ui_smoke\check_rmlui_vulkan_bridge_readiness.py --format json`:
    passed with the accepted inactive family-export counts above.
  - `python tools\ui_smoke\check_rmlui_runtime_capture.py --renderer-matrix --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\renderer-matrix.json --format json`:
    passed with the accepted aggregate inactive family-export counts above.
  - `python tools\ui_smoke\check_rmlui_renderer_matrix.py --format json`:
    passed with `native_guarded_lanes=1`, `blocked_lanes=2`, and `errors=0`.
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`:
    passed with `ok=true` and `errors=[]`.
  - `python tools\ui_smoke\check_rmlui_dependency_decision.py --format json`:
    passed with `ok=true` and `errors=[]`.

### Round 40 Evidence (2026-07-05)

Round 40 is accepted as inactive non-OpenGL runtime-dependency evidence. It
does not implement native Vulkan or RTX/vkpt rendering. It wires the optional
RmlUi runtime dependency into the Vulkan and RTX/vkpt renderer DLL lanes while
keeping `CanRender=false`, native render-interface exports null, and
route-visible captures blocked.

- Implementation log:
  `docs-dev/rmlui-round40-inactive-non-gl-runtime-dependencies-2026-07-05.md`.
- `meson.build` now adds `rmlui_dep` and `-DUI_RML_HAS_RUNTIME=1` to:
  - `renderer_vk_deps` / `renderer_vk_cpp_args`;
  - `renderer_vk_rtx_deps` / `renderer_vk_rtx_cpp_args`.
- Renderer guardrails now report `runtime_dependency_inactive=true` for
  Vulkan and RTX/vkpt until native interfaces are exported.
- Standalone bridge-readiness output now records
  `activation_complete_lanes=0`, `partial_activation_lanes=2`,
  `inactive_activation_lanes=0`, `activation_requirements=10`,
  `satisfied_activation_requirements=8`,
  `pending_activation_requirements=2`, `missing_bridge_requirements=2`, and
  `errors=0`.
- Aggregate renderer manifests now record
  `bridge_activation_complete_lanes=0`,
  `bridge_partial_activation_lanes=2`,
  `bridge_inactive_activation_lanes=0`,
  `bridge_activation_requirements=10`,
  `bridge_satisfied_activation_requirements=8`,
  `bridge_pending_activation_requirements=2`,
  `missing_bridge_requirements=2`, and `errors=0`.
- Each non-OpenGL lane now satisfies
  `native_bridge_class_present=true`,
  `native_bridge_source_compiled=true`,
  `native_family_export_present=true`, and
  `runtime_dependency_enabled=true`, but remains
  `partial_activation_blocked`.
- Both non-OpenGL lanes now report
  `next_activation_requirement=native_interface_export_present`.
- Accepted local aggregate evidence:
  - `.tmp/rmlui/runtime-capture/renderer-matrix.json`
- Coordinator validation accepted:
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_renderer_matrix.py tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py`:
    passed with `38` focused bridge/capture/adapter tests.
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_renderer_matrix.py tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py tools\ui_smoke\test_check_rmlui_dependency_integration.py tools\ui_smoke\test_check_rmlui_dependency_decision.py tools\ui_smoke\test_check_rmlui_cvar_inventory.py tools\ui_smoke\test_check_rmlui_runtime_assets.py tools\ui_smoke\test_check_rmlui_menu_entrypoints.py tools\ui_smoke\test_check_rmlui_runtime_stub_eligibility.py`:
    passed with `87` focused RmlUi guardrail tests.
  - `python tools\ui_smoke\check_rmlui_vulkan_bridge_readiness.py --format json`:
    passed with the accepted inactive runtime-dependency counts above.
  - `python tools\ui_smoke\check_rmlui_runtime_capture.py --renderer-matrix --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\renderer-matrix.json --format json`:
    passed with the accepted aggregate inactive runtime-dependency counts
    above.
  - `python tools\ui_smoke\check_rmlui_renderer_matrix.py --format json`:
    passed with `native_guarded_lanes=1`, `blocked_lanes=2`, and `errors=0`.
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`:
    passed with `ok=true`, `renderer_runtime_dependencies=true`, and
    `errors=[]`.

### Round 41 Evidence (2026-07-06)

Round 41 is accepted as installed OpenGL menu-route loading evidence. It fixes
the route-open document lifetime crash and validates that the staged client can
open the full registered RmlUi route inventory without producing a new crash
dump or RmlUi parser/fallback/error log entries. The VSCode workflow follow-up
fixes `builddir-win` so VSCode setup/build/launch uses `-Drmlui=enabled`,
stages `rmlui_core.dll`, and launches the first/default debug profile into the
OpenGL RmlUi menu. It does not implement native Vulkan or RTX/vkpt rendering,
final controllers, final font/text services, or legacy JSON removal.

- Implementation log:
  `docs-dev/rmlui-round41-menu-route-loading-2026-07-06.md`.
- `src/client/ui_rml/ui_rml_runtime.cpp` now closes only the known active
  document after a replacement document has loaded successfully. It no longer
  calls `UnloadAllDocuments()` while the newly loaded route document is waiting
  to become active.
- The main menu stylesheet links are restored after crash reduction.
- Linked runtime route themes have RmlUi-native declarations on the validated
  route path:
  - `assets/ui/rml/common/theme/singleplayer.rcss`
  - `assets/ui/rml/common/theme/session.rcss`
  - `assets/ui/rml/common/theme/accessibility.rcss`
- Build/stage validation accepted:
  - `ninja -C .tmp\rmlui\round17-rmlui-enabled3 worr_engine_x86_64.dll worr_x86_64.exe -v`
  - `python tools\refresh_install.py --build-dir .tmp\rmlui\round17-rmlui-enabled3 --install-dir .install --base-game basew --platform-id windows-x86_64`
- Installed runtime validation accepted:
  - `.install\basew\logs\rmlui_main_route_fix.log`: `main` opened, runtime
    status `active=yes route='main'`, `updates=180`, `renders=180`, exit code
    `0`, and no fresh crash dump.
  - `.install\basew\logs\rmlui_route_swap_fix.log`: startup `main` opened,
    then `options` opened and became active, exit code `0`, and no fresh crash
    dump.
  - `.install\basew\logs\rmlui_top_menu_routes_fix2.log`: top menu route batch
    opened `main`, `options`, `singleplayer`, `multiplayer`, `video`,
    `downloads`, `quit_confirm`, `game`, `download_status`, and
    `core.runtime_smoke` with no parser/fallback/error hits.
  - `.install\basew\logs\rmlui_all_routes_fix2.log`: full registered route
    pass opened `57` routes, ended active on `core.runtime_smoke`, exited with
    code `0`, produced no fresh crash dump, and had no `Syntax error`,
    `failed`, `fallback`, `No font face`, `Parser`, or `error` log hits.
- VSCode workflow validation accepted:
  - `.vscode/tasks.json` reconfigures or creates `builddir-win` with
    `-Drmlui=enabled`, runs setup before the default fast compile/install path,
    and validates `.install` as `windows-x86_64`.
  - `.vscode/launch.json` makes `WORR (OpenGL RmlUi)` the first debug profile,
    starts in the menu, and sets `ui_rml_enable 1`; Vulkan and RTX profiles
    remain explicit non-RmlUi menu lanes until their native bridges are built.
  - `meson introspect builddir-win --buildoptions`: `rmlui=enabled`.
  - `.install` contains `rmlui_core.dll`, and the staged
    `worr_engine_x86_64.dll` imports it.
  - `.install\basew\logs\vscode_opengl_rmlui_launch_verify_20260706.log`:
    `main` opened through RmlUi, runtime status was `active=yes route='main'`,
    exit code was `0`, and no fresh crash dump was produced.

### Round 42 Evidence (2026-07-06)

Round 42 is accepted as OpenGL RmlUi resize-canvas and software-cursor
evidence. It keeps active RmlUi route layout, input coordinates, OpenGL
scissor rectangles, and cursor rendering in the renderer virtual UI canvas
while still validating the staged framebuffer screenshot sizes.

- Implementation log:
  `docs-dev/rmlui-round42-resize-canvas-cursor-2026-07-06.md`.
- `src/client/ui_rml/ui_rml.cpp` now computes the active virtual UI canvas for
  RmlUi updates, converts framebuffer mouse coordinates before dispatching
  them to RmlUi, draws a `/gfx/cursor.png` software cursor from the RmlUi path,
  and updates active route dimensions from `UI_Rml_ModeChanged()`.
- `src/client/ui_rml/ui_rml_runtime.cpp` now creates the initial route context
  at the current virtual UI canvas size instead of a temporary `640x480`.
- `src/renderer/rmlui_bridge.cpp` now converts RmlUi scissor rectangles back to
  framebuffer pixels before calling OpenGL `glScissor`.
- `inc/renderer/ui_scale.h` now has C++ linkage guards for renderer bridge
  consumers.
- Build/stage validation accepted:
  - `ninja -C builddir-win worr_engine_x86_64.dll worr_opengl_x86_64.dll worr_x86_64.exe`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Runtime validation accepted:
  - `.install\basew\logs\rmlui_resize_canvas_1280x720_final.log`: screenshot
    dimensions `(1280, 720)`, active route `main`, runtime dimensions
    `640x360`, frame counters advanced, synthetic input counters advanced, and
    no failure/fallback.
  - `.install\basew\logs\rmlui_resize_canvas_960x720_final.log`: screenshot
    dimensions `(960, 720)`, active route `main`, runtime dimensions
    `960x720`, frame counters advanced, synthetic input counters advanced, and
    no failure/fallback.
  - `.tmp\rmlui\resize-canvas\rmlui_resize_canvas_1280x720_final.png`: visual
    inspection shows the RmlUi software cursor visible on the rendered menu.
  - `.install\basew\logs\rmlui_os_window_resize_move_800_to_1280.log`: one
    active `main` route survived live Win32 `MoveWindow()` resize events in the
    same process, RmlUi runtime dimensions changed from `1178x844` to
    `949x512`, exit code was `0`, and no fresh crash dump was produced.

### Round 43 Evidence (2026-07-06)

Round 43 is accepted as staged menu layout refinement and TTF-backed RmlUi text
evidence. It keeps all registered OpenGL routes loading, replaces the guarded
smoke bitmap font path with SDL3_ttf-backed text textures when available,
refines player-visible fallback copy, and prevents long keybind-style utility
lists from drawing past the active vertical canvas.

- Implementation log:
  `docs-dev/rmlui-round43-menu-layout-ttf-refinement-2026-07-06.md`.
- `src/client/ui_rml/ui_rml_runtime.cpp` now installs
  `UI_Rml_TtfFontEngineInterface` for SDL3_ttf builds, loads packaged UI and
  monospace font faces, measures text through SDL_ttf, and uploads generated
  text as RmlUi callback textures.
- Shared RCSS layout now gives route bodies, settings forms, shell stacks,
  save-slot lists, session panes, utility tables, and keybind groups bounded
  flex/scroll ownership inside the active menu canvas.
- Utility/session visible fallback copy no longer exposes migration-controller
  wording on player-facing menu routes.
- Build/stage validation accepted:
  - `meson compile -C builddir-win`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Static validation accepted:
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py`
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew`
  - `python tools\ui_smoke\check_rmlui_runtime_registry.py`
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_runtime_capture.py`
- Runtime validation accepted:
  - `.install\basew\logs\rmlui_round43_refine_file_probe.log`: `58`
    registered runtime file probes passed, and TTF face/texture markers were
    logged.
  - `.install\basew\logs\rmlui_round43_final_all_route_open.log`: final
    staged OpenGL route pass recorded `59` opened documents, no failure/error/
    exception/unhandled/parser hits, and TTF face/texture markers.
  - `.tmp\rmlui\round43-screens\rmlui_round43_keys_final_960x720.png`:
    visual inspection shows the `keys` route rendered with TTF text, wrapped
    columns, visible cursor, and no button row clipped past the `960x720`
    viewport.
- Known remaining gap: RmlUi still logs expected missing data-model notices on
  controller-stub routes because live C++ data-model/controller registration is
  still pending. Those notices did not block route load, rendering, or the
  Round 43 screenshot evidence.

### Round 44 Evidence (2026-07-06)

Round 44 is accepted as Quake II Rerelease TTF source and dense menu refinement
evidence. It keeps the guarded OpenGL RmlUi path loading every registered route,
loads the default RmlUi display/UI/monospace faces from Quake II Rerelease font
assets, adds guardrails that require rerelease font-source markers, and tightens
Options, Admin Commands, Start Server, and Deathmatch Flags so their visible
content no longer depends on long lists drawing past the active viewport.

- Implementation log:
  `docs-dev/rmlui-round44-q2r-font-menu-refinement-2026-07-06.md`.
- `src/client/ui_rml/ui_rml_runtime.cpp` now prefers Quake II Rerelease
  `fonts/RussoOne-Regular.ttf`, `fonts/Montserrat-Regular.ttf`, and
  `fonts/RobotoMono-Regular.ttf` for the RmlUi display, UI, and monospace
  faces, and logs generated text texture source labels.
- Shared RCSS now gives semantic tags explicit block defaults, moves menu
  footer layout into the shared theme, wraps Options into two columns, and
  constrains dense single-player/session menus into bounded scroll regions.
- Build/stage validation accepted:
  - `meson compile -C builddir-win`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Static validation accepted:
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py`
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew`
  - `python tools\ui_smoke\check_rmlui_runtime_registry.py`
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_runtime_capture.py`
- Runtime validation accepted:
  - `.install\basew\logs\rmlui_round44_final_q2r_font_file_probe.log`: `58`
    registered runtime file probes passed, all three default RmlUi faces loaded
    from Quake II Rerelease font paths, and generated text source included a
    `Quake II Rerelease:` marker.
  - `.install\basew\logs\rmlui_round44_final_q2r_all_route_open.log`: final
    staged OpenGL route pass recorded `59` opened documents, `58` unique route
    IDs, no failure/error/exception/unhandled/parser hits, and Quake II
    Rerelease font-source markers.
  - `.tmp\rmlui\round44-screens\rmlui_round44_refined_options_q2r_960x720.png`:
    visual inspection shows the Options menu contained in two columns with
    visible Back/Close actions.
  - `.tmp\rmlui\round44-screens\rmlui_round44_refined_admin_commands_q2r_960x720.png`:
    visual inspection shows Admin Commands rendered as a readable bounded list.
  - `.tmp\rmlui\round44-screens\rmlui_round44_section_width_startserver_q2r_960x720.png`
    and `.tmp\rmlui\round44-screens\rmlui_round44_section_width_gameflags_q2r_960x720.png`:
    visual inspection shows dense single-player forms bounded with visible
    Back/Close actions.
- Known remaining gap: missing data-model notices on controller-stub routes are
  still expected until live C++ data-model/controller registration lands.

### Round 45 Evidence (2026-07-06)

Round 45 is accepted as bounded long-list and settings-form layout evidence. It
keeps the guarded OpenGL RmlUi path loading every registered route, preserves the
Quake II Rerelease TTF source markers from Round 44, and prevents long in-game,
settings, and save/load content from hiding the visible Back/Close actions.

- Implementation log:
  `docs-dev/rmlui-round45-bounded-menu-list-refinement-2026-07-06.md`.
- Shared RCSS now gives settings forms and save/load slot lists explicit
  viewport-safe dimensions, gives settings sections/rows explicit widths, and
  compacts repeated save-slot rows.
- The in-game menu now wraps its longer action list into two columns like the
  Options menu.
- Build/stage validation accepted:
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
  - `meson compile -C builddir-win`
- Static validation accepted:
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew`
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py`
  - `python tools\ui_smoke\check_rmlui_runtime_registry.py`
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_runtime_capture.py`
- Runtime validation accepted:
  - `.install\basew\logs\rmlui_round45_representative_capture.log`: opened
    `30` representative routes with `0` failure/error/exception/unhandled/
    parser hits and Quake II Rerelease font-source markers.
  - `.install\basew\logs\rmlui_round45_final_all_route_open.log`: final staged
    OpenGL route pass recorded `59` opened documents, `58` unique route IDs,
    no failure/error/exception/unhandled/parser hits, and Quake II Rerelease
    font-source markers.
  - `.tmp\rmlui\round45-screens\rmlui_round45_bounded_game_960x720.png`:
    visual inspection shows the in-game menu action list wrapped with visible
    Back/Close actions.
  - `.tmp\rmlui\round45-screens\rmlui_round45_width_fix_sound_960x720.png`,
    `.tmp\rmlui\round45-screens\rmlui_round45_width_fix_screen_960x720.png`,
    `.tmp\rmlui\round45-screens\rmlui_round45_width_fix_performance_960x720.png`,
    and `.tmp\rmlui\round45-screens\rmlui_round45_width_fix_video_960x720.png`:
    visual inspection shows long settings forms bounded above visible Back/
    Close actions.
  - `.tmp\rmlui\round45-screens\rmlui_round45_compact_loadgame_960x720.png`
    and `.tmp\rmlui\round45-screens\rmlui_round45_compact_savegame_960x720.png`:
    visual inspection shows save/load slot lists bounded above visible Back/
    Close actions.
- Known remaining gap: controller-backed lists still need final scrollbar/focus
  behavior once live data-model/controller work lands.

### Round 46 Evidence (2026-07-06)

Round 46 is accepted as targeted Single Player hub and generic utility-list
layout evidence. It keeps the guarded OpenGL RmlUi path loading every
registered route, preserves the Quake II Rerelease TTF source markers from
Round 44, and keeps the representative `singleplayer` and `ui_list` menu
surfaces inside the active `960x720` canvas.

- Implementation log:
  `docs-dev/rmlui-round46-singleplayer-utility-list-refinement-2026-07-06.md`.
- Shared single-player RCSS now gives the Single Player hub selector section
  and action controls explicit widths.
- Shared utility RCSS now gives the `ui_list` toolbar and list body explicit
  bounded dimensions, with compact repeated list buttons and `overflow: auto`.
- Build/stage validation accepted:
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
  - `meson compile -C builddir-win`
- Static validation accepted:
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew`
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py`
  - `python tools\ui_smoke\check_rmlui_runtime_registry.py`
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_runtime_capture.py`
- Runtime validation accepted:
  - `.install\basew\logs\rmlui_round46_representative_capture.log`: opened
    `30` representative routes with `0` failure/error/exception/unhandled/
    parser hits and Quake II Rerelease font-source markers.
  - `.install\basew\logs\rmlui_round46_hub_list_capture.log`: focused recapture
    of `singleplayer` and `ui_list` after the CSS changes with no failure/
    error/exception/unhandled/parser hits.
  - `.install\basew\logs\rmlui_round46_final_all_route_open.log`: final staged
    OpenGL route pass recorded `59` opened documents, `58` unique route IDs,
    and no failure/error/exception/unhandled/parser hits.
  - `.tmp\rmlui\round46-screens\rmlui_round46_refined_singleplayer_960x720.png`:
    visual inspection shows the Single Player hub controls aligned at stable
    widths with visible Back/Close actions.
  - `.tmp\rmlui\round46-screens\rmlui_round46_refined_ui_list_960x720.png`:
    visual inspection shows the Session List toolbar, bounded list body, and
    Previous/Next/Return footer controls visible inside `960x720`.
- Known remaining gap: live controller-backed list data, scrollbar/focus
  parity, and automated layout assertions for all menu routes remain pending.

### Round 47 Evidence (2026-07-06)

Round 47 is accepted as targeted session-list and keybind layout-containment
evidence. It keeps the guarded OpenGL RmlUi path loading every registered
route, preserves the Quake II Rerelease TTF source markers from Round 44, and
keeps the representative `admin_commands`, `callvote_main`, `dm_join`, and
`keys` menu surfaces inside the active `960x720` canvas.

- Implementation log:
  `docs-dev/rmlui-round47-session-keybind-containment-2026-07-06.md`.
- Shared session RCSS now bounds long admin, callvote, and match-lobby lists
  while preserving visible footer actions.
- Shared utility RCSS now gives the `keys` route a denser bounded keybind grid
  so Interface and footer actions remain visible.
- Build/stage validation accepted:
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
  - `meson compile -C builddir-win`
- Static validation accepted:
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew`
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py`
  - `python tools\ui_smoke\check_rmlui_runtime_registry.py`
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_runtime_capture.py`
- Runtime validation accepted:
  - `.install\basew\logs\rmlui_round47_representative_capture.log`: opened
    `30` representative routes with `0` failure/error/exception/unhandled/
    parser hits.
  - `.install\basew\logs\rmlui_round47_focused_capture2.log`: focused
    recapture of `admin_commands`, `callvote_main`, `dm_join`, and `keys`
    after the CSS changes with no failure/error/exception/unhandled/parser
    hits.
  - `.install\basew\logs\rmlui_round47_dm_join_final_capture.log`: final
    `dm_join` recapture after shortening its bounded session menu with no
    failure/error/exception/unhandled/parser hits.
  - `.install\basew\logs\rmlui_round47_final_all_route_open.log`: final staged
    OpenGL route pass recorded `59` opened documents, `58` unique route IDs,
    no failure/error/exception/unhandled/parser hits, and Quake II Rerelease
    font-source markers.
  - `.tmp\rmlui\round47-screens\rmlui_round47_refined2_admin_commands_960x720.png`:
    visual inspection shows the long Admin Commands list bounded above a
    visible Back action.
  - `.tmp\rmlui\round47-screens\rmlui_round47_refined2_callvote_main_960x720.png`:
    visual inspection shows the Call Vote options bounded above a visible
    Return action.
  - `.tmp\rmlui\round47-screens\rmlui_round47_refined3_dm_join_960x720.png`:
    visual inspection shows the Match Lobby session list, community copy, and
    Close action visible inside `960x720`.
  - `.tmp\rmlui\round47-screens\rmlui_round47_refined2_keys_960x720.png`:
    visual inspection shows the Interface keybind group and footer actions
    visible inside `960x720`.
- Known remaining gap: live controller-backed session/list/keybind behavior,
  focus/scrollbar parity, and automated screenshot assertions for all menu
  routes remain pending.

### Round 48 Evidence (2026-07-06)

Round 48 is accepted as targeted settings-toggle and local-host form layout
evidence. It keeps the guarded OpenGL RmlUi path loading every registered
route, preserves the Quake II Rerelease TTF source markers from Round 44, and
keeps the representative `performance`, `sound`, `downloads`, and
`startserver` form surfaces inside the active `960x720` canvas with visible
Back/Close actions.

- Implementation log:
  `docs-dev/rmlui-round48-settings-toggle-form-refinement-2026-07-06.md`.
- Shared settings RCSS now draws `data-control="toggle"` inputs as compact
  square controls instead of full-width fields.
- Shared settings/single-player RCSS now uses shorter form scroll regions for
  long settings pages while keeping Downloads tall enough to show its final
  HTTP Downloads toggle.
- Build/stage validation accepted:
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
  - `meson compile -C builddir-win`
- Static validation accepted:
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew`
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py`
  - `python tools\ui_smoke\check_rmlui_runtime_registry.py`
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_runtime_capture.py`
- Runtime validation accepted:
  - `.install\basew\logs\rmlui_round48_representative_capture.log`: opened
    `30` representative routes with `0` failure/error/exception/unhandled/
    parser hits.
  - `.install\basew\logs\rmlui_round48_focused_forms_capture.log`: focused
    recapture of `performance`, `sound`, `downloads`, and `startserver` after
    the CSS changes with no failure/error/exception/unhandled/parser hits.
  - `.install\basew\logs\rmlui_round48_downloads_final_capture.log`: final
    `downloads` recapture after the route-specific height adjustment with no
    failure/error/exception/unhandled/parser hits.
  - `.install\basew\logs\rmlui_round48_final_all_route_open.log`: final staged
    OpenGL route pass recorded `59` opened documents, `58` unique route IDs,
    no failure/error/exception/unhandled/parser hits, and Quake II Rerelease
    font-source markers.
  - `.tmp\rmlui\round48-screens\rmlui_round48_refined_performance_960x720.png`:
    visual inspection shows compact Client Behavior toggles with all rows and
    Back/Close visible.
  - `.tmp\rmlui\round48-screens\rmlui_round48_refined_sound_960x720.png`:
    visual inspection shows compact music toggle presentation and footer
    actions visible.
  - `.tmp\rmlui\round48-screens\rmlui_round48_refined2_downloads_960x720.png`:
    visual inspection shows all download toggles, the final HTTP Downloads
    toggle, and Back/Close visible.
  - `.tmp\rmlui\round48-screens\rmlui_round48_refined_startserver_960x720.png`:
    visual inspection shows Start Server ending on complete rows above
    Back/Close.
- Known remaining gap: live cvar/controller persistence, focus traversal,
  scrollbar parity, and automated screenshot assertions for all menu routes
  remain pending.

### Round 49 Evidence (2026-07-06)

Round 49 is accepted as the transition/aesthetic refinement baseline. It keeps
the guarded OpenGL RmlUi path loading every registered route, preserves the
Quake II Rerelease TTF source markers, adds conservative RmlUi-native
transitions and hover/focus treatment, and verifies the adjusted Sound and
Start Server scroll regions still end on complete rows inside `960x720`.

- Implementation log:
  `docs-dev/rmlui-round49-transition-aesthetic-refinement-2026-07-06.md`.
- Shared base RCSS now adds header/footer dividers plus short
  color/border/text transitions for buttons and fields.
- Settings, session, and utility RCSS now apply matching hover/focus polish to
  compact toggles, panels, rows, toolbars, and keybind buttons.
- Route-specific form refinements keep `sound` and `startserver` from ending
  on partial rows after the visual framing pass.
- Build/stage validation accepted:
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
  - `meson compile -C builddir-win`
- Static validation accepted:
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py`
  - `python tools\ui_smoke\check_rmlui_runtime_registry.py`
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew`
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_runtime_capture.py`
- Runtime validation accepted:
  - `.install\basew\logs\rmlui_round49_aesthetic_capture.log`: focused
    representative capture recorded `15` total opens, `14` unique route IDs,
    and `0` failure/error/exception/unhandled/parser/transition/animation/
    unsupported hits.
  - `.tmp\rmlui\round49-screens\rmlui_round49_contact_sheet_1.png` and
    `.tmp\rmlui\round49-screens\rmlui_round49_contact_sheet_2.png`: visual
    review covers shell, settings, session, and utility route polish.
  - `.install\basew\logs\rmlui_round49_form_spacing_capture.log`: focused
    recapture after settings-section spacing was tightened.
  - `.tmp\rmlui\round49-screens\rmlui_round49_refined_performance_960x720.png`:
    visual inspection shows Performance remains contained with visible footer
    actions.
  - `.tmp\rmlui\round49-screens\rmlui_round49_refined2_startserver_960x720.png`:
    visual inspection shows Start Server ending on complete rows above
    Back/Close.
  - `.install\basew\logs\rmlui_round49_sound_final_capture.log`: final Sound
    recapture after route-specific height adjustment.
  - `.tmp\rmlui\round49-screens\rmlui_round49_refined3_sound_960x720.png`:
    visual inspection shows Sound ending on a complete row above footer
    actions.
  - `.install\basew\logs\rmlui_round49_final_all_route_open.log`: final staged
    OpenGL route pass recorded `59` opened documents, `58` unique route IDs,
    `0` failure/error/exception/unhandled/parser/transition/animation/
    unsupported hits, and Quake II Rerelease font-source markers.
- Known remaining gap: live controller behavior, focus/scrollbar parity,
  native Vulkan/RTX-vkpt RmlUi rendering, final live accessibility preference
  wiring, and automated screenshot assertions for all menu routes remain
  pending.

### Round 50 Evidence (2026-07-06)

Round 50 is accepted as the scaling/positioning refinement baseline. It keeps
the guarded OpenGL RmlUi path loading every registered route, preserves the
Quake II Rerelease TTF source markers, corrects fullscreen-style over-scaling
against a `960x720` reference canvas, and fixes the main menu's viewport fill
and right-edge button clipping.

- Implementation log:
  `docs-dev/rmlui-round50-scaling-positioning-refinement-2026-07-06.md`.
- Client/runtime scaling now uses a shared `960x720` RmlUi reference canvas
  instead of the old 640x480 HUD virtual sizing path.
- RmlUi rendering and software cursor drawing now set the renderer 2D scale
  that corresponds to the active RmlUi canvas before drawing.
- Shared base RCSS now anchors `body` and `.screen` to the RmlUi viewport with
  top/right/bottom/left edges.
- Shell RCSS now gives the main menu a stable action-column width so button
  right borders are not clipped by shrink-to-content layout.
- Build/stage validation accepted:
  - `meson compile -C builddir-win`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Static validation accepted:
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py`
  - `python tools\ui_smoke\check_rmlui_runtime_registry.py`
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew`
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_runtime_capture.py`
  - `rg -n "var\(|gap:" assets\ui\rml -g "*.rcss"`
- Runtime validation accepted:
  - `.install\basew\logs\rmlui_round50_main_964x765.log`: `main` capture at
    `964x765` passed and reported `960x762` runtime dimensions.
  - `.install\basew\logs\rmlui_round50_main_1280x720.log`: `main` capture at
    `1280x720` passed and reported `1280x720` runtime dimensions.
  - `.install\basew\logs\rmlui_round50_main_1280x960.log`: `main` capture at
    `1280x960` passed.
  - `.install\basew\logs\rmlui_round50_main_2048x1152.log`: `main` capture at
    `2048x1152` passed and reported `1280x720` runtime dimensions.
  - `.tmp\rmlui\round50-screens\rmlui_round50_main_scaling_contact_sheet.png`:
    visual inspection shows viewport fill and intact main-menu button right
    edges across the tested aspect ratios.
  - `.install\basew\logs\rmlui_round50_final_main_2048x1152.log`: final staged
    widescreen capture after install refresh passed with Q2R TTF source
    markers and `1280x720` runtime dimensions.
  - `.tmp\rmlui\round50-screens\rmlui_round50_final_main_2048x1152.png`:
    visual inspection shows the fullscreen-style main menu no longer clips its
    button right edges.
  - `.install\basew\logs\rmlui_round50_final_all_route_open.log`: final staged
    OpenGL route pass recorded `59` opened documents, `58` unique route IDs,
    `0` failure/error/exception/unhandled/parser/transition/animation/
    unsupported hits, and Quake II Rerelease font-source markers.
- Known remaining gap: route-specific automated visual clipping assertions,
  live controller behavior, focus/scrollbar parity, native Vulkan/RTX-vkpt
  RmlUi rendering, and full visual parity remain pending.

### Round 51 Evidence (2026-07-06)

Round 51 is accepted as the widget/layout refinement baseline. It keeps the
Round 50 `960x720` reference canvas and main-menu scaling behavior, then
refines the non-main control surfaces so settings and utility menus present
appropriate widgets with a uniform, modern row layout.

- Implementation log:
  `docs-dev/rmlui-round51-widget-layout-refinement-2026-07-06.md`.
- Shared settings RCSS now gives typed rows stable label/control/value columns
  and per-widget treatment for toggles, ranges, selects, combos, image-value
  selectors, fields, numeric fields, and progress rows.
- Start Server and Deathmatch Flags now use the same settings widths while
  preserving bounded scroll regions and visible footer actions.
- Utility forms now use explicit label/control widths, fixing collapsed empty
  Address Book text inputs while preserving the Player Setup form/preview
  split.
- `download_status` now imports `settings.rcss`, so its progress row uses the
  same typed control styling as the rest of the settings stack.
- Build/stage validation accepted:
  - `meson compile -C builddir-win`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Static validation accepted:
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py`
  - `python tools\ui_smoke\check_rmlui_runtime_registry.py`
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew`
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_runtime_capture.py`
  - `rg -n "var\(|gap:|box-shadow|filter:|calc\(|@media|:checked|::" assets\ui\rml -g "*.rcss"`
- Runtime validation accepted:
  - `.install\basew\logs\rmlui_round51_widget_layout_capture2.log`:
    representative `960x720` direct route-open capture for settings,
    single-player setup, utility, and progress routes.
  - `.install\basew\logs\rmlui_round51_utility_form_recapture.log`: focused
    recapture for the utility form-width fix.
  - `.tmp\rmlui\round51-screens\rmlui_round51_widget_layout_contact_sheet.png`:
    visual inspection confirms the typed-widget layout across `12`
    representative routes.
  - `.tmp\rmlui\round51-screens\rmlui_round51_addressbook_960x720_full.png`:
    visual inspection confirms Address Book text inputs no longer collapse.
  - `.tmp\rmlui\round51-screens\rmlui_round51_players_960x720_full.png`:
    visual inspection confirms Player Setup keeps a balanced form/preview
    split.
  - `.install\basew\logs\rmlui_round51_final_all_route_open.log`: final staged
    OpenGL route pass recorded `59` opened documents, `58` unique route IDs,
    `0` failure/error/exception/unhandled/parser/transition/animation/
    unsupported hits, and Quake II Rerelease font-source markers.
- Known remaining gap: live controller/data-model behavior, route-wide
  automated pixel clipping assertions, full focus/scrollbar parity, native
  Vulkan/RTX-vkpt RmlUi rendering, and full visual parity remain pending.

### Round 52 Evidence (2026-07-06)

Round 52 is accepted as the navigation layout refinement baseline. It keeps
the Round 50 `960x720` reference canvas, preserves the Round 51 typed-widget
contracts, and replaces long single-column command surfaces with deterministic
two-column grids where the legacy menu intent is finite and command-driven.

- Implementation log:
  `docs-dev/rmlui-round52-navigation-layout-refinement-2026-07-06.md`.
- Shell command controls in `main`, `options`, and `game` now use real
  `button` elements rather than generic `div.ui-button` command rows.
- Options, Game, Single Player, Load Game, Save Game, Multiplayer, Call Vote,
  Join, and Deathmatch Join now use bounded two-column command regions instead
  of long vertical lists that push past the visible footer.
- Shared settings forms now use the same `604px` layout contract with narrower
  label/control/value columns while preserving typed controls for toggles,
  ranges, selects, fields, numeric fields, image-value selectors, and progress
  rows.
- Build/stage validation accepted:
  - `meson compile -C builddir-win`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Static validation accepted:
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py`
  - `python tools\ui_smoke\check_rmlui_runtime_registry.py`
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew`
  - `python tools\ui_smoke\check_rmlui_semantics.py`
  - `python tools\ui_smoke\check_rmlui_command_inventory.py`
  - `python tools\ui_smoke\check_rmlui_condition_inventory.py`
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_runtime_capture.py`
  - `rg -n "var\(|gap:|box-shadow|filter:|calc\(|@media|:checked|::" assets\ui\rml -g "*.rcss"`
- Runtime validation accepted:
  - `.install\basew\logs\rmlui_round52_960x720_layout_capture.log`:
    representative `960x720` route-open captures for shell, settings,
    single-player, save/load, multiplayer, and session routes.
  - `.tmp\rmlui\round52-screens\rmlui_round52_960x720_layout_contact_sheet.png`:
    visual inspection confirms compact two-column navigation grids and the
    narrowed settings widget layout.
  - `.install\basew\logs\rmlui_round52_final_all_route_open.log`: final staged
    OpenGL route pass recorded `59` opened documents, `58` unique route IDs,
    `0` failure/error/exception/unhandled/parser/transition/animation/
    unsupported hits, and Quake II Rerelease font-source markers.
- Known remaining gap: the screenshot capture path accepted a `640x480`
  request but still reported `960x720` runtime dimensions, so true
  narrow-viewport screenshot evidence needs a harness follow-up before
  responsive matrix parity is claimed. Live controller/data-model behavior,
  route-wide automated pixel clipping assertions, full focus/scrollbar parity,
  native Vulkan/RTX-vkpt RmlUi rendering, and full visual parity also remain
  pending.

### Round 53 Evidence (2026-07-06)

Round 53 is accepted as the menu polish refinement baseline. It preserves the
Round 52 deterministic two-column positions, then softens the visual treatment
so command regions read as spaced action tiles instead of table grids.

- Implementation log:
  `docs-dev/rmlui-round53-menu-polish-refinement-2026-07-06.md`.
- Options, Game, Single Player, save/load, Multiplayer, Call Vote, Join, and
  Deathmatch Join command controls now use reduced-width spaced tiles with
  rounded corners, darker fills, and hover/focus left accents.
- Save/load grids preserve Autosave as a full-width highlighted row while the
  remaining slots use the same two-column tile rhythm.
- Shared settings rows are denser and cleaner, with smaller section headings,
  rounded row frames, and preserved typed widgets for toggles, ranges, selects,
  fields, numeric fields, image-value selectors, and progress rows.
- Build/stage validation accepted:
  - `meson compile -C builddir-win`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Static validation accepted:
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py`
  - `python tools\ui_smoke\check_rmlui_runtime_registry.py`
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew`
  - `python tools\ui_smoke\check_rmlui_semantics.py`
  - `python tools\ui_smoke\check_rmlui_command_inventory.py`
  - `python tools\ui_smoke\check_rmlui_condition_inventory.py`
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_runtime_capture.py`
  - `rg -n "var\(|gap:|box-shadow|filter:|calc\(|@media|:checked|::" assets\ui\rml -g "*.rcss"`
- Runtime validation accepted:
  - `.install\basew\logs\rmlui_round53_refinement_capture.log`:
    representative `960x720` route-open captures for shell, settings,
    single-player, save/load, multiplayer, and session routes.
  - `.tmp\rmlui\round53-screens\rmlui_round53_refinement_contact_sheet.png`:
    visual inspection confirms the spaced tile treatment and denser settings
    rows across the representative route set.
  - `.install\basew\logs\rmlui_round53_final_all_route_open.log`: final staged
    OpenGL route pass recorded `59` opened documents, `58` unique route IDs,
    `0` failure/error/exception/unhandled/parser/transition/animation/
    unsupported hits, and Quake II Rerelease font-source markers.
- Known remaining gap: live controller/data-model behavior, true
  narrow-viewport screenshot evidence, route-wide automated pixel clipping
  assertions, full focus/scrollbar parity, native Vulkan/RTX-vkpt RmlUi
  rendering, and full visual parity remain pending.

### Round 54 Evidence (2026-07-06)

Round 54 is accepted as the action-intent and widget-semantics refinement
baseline. It keeps the Round 52/53 `604px` layout contract and command-grid
positions, while making command intent clearer and replacing remaining
pseudo-button command controls with real buttons.

- Implementation log:
  `docs-dev/rmlui-round54-action-intent-widget-refinement-2026-07-06.md`.
- Remaining command-like `div.ui-button` controls in Quit Confirm, Downloads,
  Download Status, and Runtime Smoke are now real `<button type="button">`
  controls with the same command attributes.
- Shared primary/destructive/secondary action treatments now mark Resume,
  Apply, Start, Quit, Disconnect, Cancel, Leave Match, and confirmation actions
  with consistent filled states and focused/hovered borders.
- Shell and session grid selectors now preserve those intent states even inside
  the high-specificity fixed two-column navigation grids.
- Player Setup and utility surfaces keep typed inputs, a clearer preview frame,
  compact form columns, and a primary Apply action; Quit Confirm now presents a
  compact confirmation panel with destructive Yes and secondary No actions.
- Build/stage validation accepted:
  - `meson compile -C builddir-win`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Static validation accepted:
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py`
  - `python tools\ui_smoke\check_rmlui_runtime_registry.py`
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew`
  - `python tools\ui_smoke\check_rmlui_semantics.py`
  - `python tools\ui_smoke\check_rmlui_command_inventory.py`
  - `python tools\ui_smoke\check_rmlui_condition_inventory.py`
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_runtime_capture.py`
  - `rg -n "var\(|gap:|box-shadow|filter:|calc\(|@media|:checked|::" assets\ui\rml -g "*.rcss"`
- Runtime validation accepted:
  - `.tmp\rmlui\round54-final-capture\round54_final_game.png`,
    `.tmp\rmlui\round54-final-capture\round54_final_main.png`, and
    `.tmp\rmlui\round54-final-capture\round54_final_download_status.png`:
    focused shell captures confirm primary/destructive action intent is visible.
  - `.tmp\rmlui\round54-final-capture\round54_open_dm_join.png` and
    `.tmp\rmlui\round54-final-capture\round54_open_players.png`: focused
    route-open captures confirm session destructive action treatment and the
    refined Player Setup form/preview layout.
  - `.install\basew\logs\rmlui_round54_final_all_route_open.log`: final staged
    OpenGL route pass recorded `59` opened documents, `58` unique route IDs,
    `0` failure/error/exception/unhandled/parser/transition/animation/
    unsupported hits, and Quake II Rerelease font-source markers.
- Known remaining gap: live controller/data-model behavior, true
  narrow-viewport screenshot evidence, route-wide automated pixel clipping
  assertions, full focus/scrollbar parity, native Vulkan/RTX-vkpt RmlUi
  rendering, and full visual parity remain pending.

### Round 55 Evidence (2026-07-07)

Round 55 is accepted as the popup/audio menu refinement baseline. It keeps the
existing route documents and typed-widget contracts, but adds an explicit popup
route command for confirmation menus and restores legacy-style menu feedback
sounds through the engine UI bridge.

- Implementation log:
  `docs-dev/rmlui-round55-popup-audio-menu-refinement-2026-07-07.md`.
- Runtime popup support now includes `ui_rml_runtime_popup <route_id>`, and
  RmlUi event handling recognizes `ui.popup`, `pushpopup`, `data-menu-sound`,
  and `data-command-cvar` command elements.
- The shared engine UI bridge exposes `UI_StartFeedbackSound()`, mapping open,
  move/change, close/back, and alert/confirm feedback to the legacy menu sound
  samples.
- Quit, Forfeit, Leave Match, and Tournament Replay confirmations now share
  centered popup markup and compact side-by-side Yes/No actions.
- Main/session destructive entry points open the confirmation documents through
  popup route commands instead of direct flat route navigation.
- Sound Settings now includes menu music metadata, exposes `ogg_menu_track` as
  a numeric field, and uses a two-column sound/volume plus music/effects layout
  that fits above the footer at `960x720`.
- Build/stage validation accepted:
  - `meson compile -C builddir-win`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Static validation accepted:
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py`
  - `python tools\ui_smoke\check_rmlui_runtime_registry.py`
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew`
  - `python tools\ui_smoke\check_rmlui_semantics.py`
  - `python tools\ui_smoke\check_rmlui_command_inventory.py`
  - `python tools\ui_smoke\check_rmlui_condition_inventory.py`
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py`
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_capture.py`
  - `rg -n "var\(|gap:|box-shadow|filter:|calc\(|@media|:checked|::" assets\ui\rml -g "*.rcss"`
- Runtime validation accepted:
  - `.tmp\rmlui\round55-screens\round55b_contact.png`: focused captures
    confirm popup confirmation geometry and side-by-side actions.
  - `.tmp\rmlui\round55-screens\round55c_sound_960.png`: final Sound Settings
    capture confirms the two-column typed-widget layout and no footer overlap.
  - `.install\basew\logs\rmlui_round55_final_all_route_open_b.log`: final
    staged OpenGL route pass recorded `59` opened documents, `58` unique route
    IDs, `58` runtime status samples, `0`
    failure/error/exception/unhandled/parser/transition/animation/unsupported
    hits, and `3` Quake II Rerelease font-source markers.
  - `.install\basew\logs\rmlui_round55_final_popup_command_b.log`: popup
    command validation recorded `2` popup route markers, `4` opened documents,
    `2` runtime status samples, and `0` bad lines.
- Known remaining gap: live controller/data-model behavior, route-wide
  automated pixel clipping assertions, full focus/scrollbar parity, native
  Vulkan/RTX-vkpt RmlUi rendering, and full visual parity remain pending.

### Round 56 Evidence (2026-07-07)

Round 56 is accepted as the menu-music and Game Quit popup parity baseline. It
keeps the Round 55 popup/dialog treatment and makes menu music metadata a
runtime behavior through the existing OGG playback path.

- Implementation log:
  `docs-dev/rmlui-round56-menu-music-popup-parity-2026-07-07.md`.
- The compiled RmlUi runtime now inspects successfully parsed documents for
  `data-menu-music` and calls `OGG_Play()` for `menu`/`auto` music cues.
- Main, Game, Options, Video, Single Player, Multiplayer, Downloads, and
  Download Status now declare `data-menu-music="menu"`.
- Game menu Quit now opens `quit_confirm` through `ui.popup`, matching Main
  menu Quit instead of executing `quit` directly.
- Build/stage validation accepted:
  - `meson compile -C builddir-win`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Static validation accepted:
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py`
  - `python tools\ui_smoke\check_rmlui_runtime_registry.py`
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew`
  - `python tools\ui_smoke\check_rmlui_semantics.py`
  - `python tools\ui_smoke\check_rmlui_command_inventory.py`
  - `python tools\ui_smoke\check_rmlui_condition_inventory.py`
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py`
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_capture.py`
  - `rg -n "var\(|gap:|box-shadow|filter:|calc\(|@media|:checked|::" assets\ui\rml -g "*.rcss"`
- Runtime validation accepted:
  - `.tmp\rmlui\round56-screens\round56_contact.png`: focused capture sheet
    confirms Game, Main, Quit Confirm, and Sound Settings remain composed.
  - `.install\basew\logs\rmlui_round56_all_route_music_open.log`: final
    staged OpenGL route pass recorded `59` opened documents, `58` unique route
    IDs, `58` runtime status samples, `14` menu music cue markers, `0`
    failure/error/exception/unhandled/parser/transition/animation/unsupported
    hits, and `3` Quake II Rerelease font-source markers.
  - `.install\basew\logs\rmlui_round56_game_popup_music.log`: focused Game
    and Main popup validation recorded `2` popup route markers, `5` music cue
    markers, `5` opened documents, `2` runtime status samples, and `0` bad
    lines.
- Known remaining gap: live controller/data-model behavior, route-wide
  automated pixel clipping assertions, full focus/scrollbar parity, native
  Vulkan/RTX-vkpt RmlUi rendering, and full visual parity remain pending.

### Round 57 Evidence (2026-07-07)

Round 57 is accepted as the open-sound and focus/change interaction-audio
baseline for the guarded OpenGL RmlUi path. It keeps the Round 56 menu-music
and popup parity behavior while letting successfully opened documents consume
`data-menu-sound-open` metadata and letting interactive controls produce
legacy move feedback on RmlUi focus/change events.

- Implementation log:
  `docs-dev/rmlui-round57-open-focus-audio-refinement-2026-07-07.md`.
- The compiled RmlUi runtime now consumes `data-menu-sound-open` on the
  document or body after successful parse/show/update.
- The runtime attaches direct focus/change audio listeners to command elements
  and form controls because RmlUi focus events are target-only.
- A short runtime feedback de-dupe prevents route-open sounds from doubling a
  click sound when a button immediately opens a route or popup.
- Main, Game, Options, Video, Sound, Single Player, Multiplayer, Downloads,
  and Download Status now declare `data-menu-sound-open="open"` alongside
  their menu music metadata.
- Confirmation popup routes keep `data-menu-sound-open="alert"`.
- Build/stage validation accepted:
  - `meson compile -C builddir-win`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Static validation accepted:
  - `python tools\ui_smoke\check_rmlui_semantics.py`
  - `python tools\ui_smoke\check_rmlui_command_inventory.py`
  - `python tools\ui_smoke\check_rmlui_condition_inventory.py`
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py`
  - `python tools\ui_smoke\check_rmlui_runtime_registry.py`
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew`
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_runtime_capture.py`
- Runtime validation accepted:
  - `.install\basew\logs\rmlui_round57_all_route_audio_open.log`: final
    staged OpenGL route pass recorded `59` opened documents, `58` unique route
    IDs, `58` runtime status samples, `14` menu music cue markers, `14` menu
    open-sound cue markers, `0`
    failure/error/exception/unhandled/parser/transition/animation/unsupported
    hits, and `3` RmlUi Quake II Rerelease TTF font-source markers.
  - `.install\basew\logs\rmlui_round57_popup_audio_flow.log`: focused
    popup validation recorded `2` `quit_confirm` popup route requests, `5`
    opened documents, `2` runtime status samples, `5` music cue markers, `5`
    open-sound cue markers, and `0` bad lines.
  - `.install\basew\logs\rmlui_round57_quit_popup_capture.log`: focused
    Quit Confirm capture recorded `1` popup route request, `1` active
    `quit_confirm` runtime status sample, `3` opened documents, `3` music
    cue markers, `3` open-sound cue markers, and `0` bad lines.
  - `.tmp\rmlui\round57-screens\round57_audio_route_matrix_tga_main.png`,
    `.tmp\rmlui\round57-screens\round57_audio_route_matrix_tga_game.png`,
    `.tmp\rmlui\round57-screens\round57_audio_route_matrix_tga_download_status.png`,
    and `.tmp\rmlui\round57-screens\round57_quit_popup.png`: visual captures
    confirm the Main, Game, Download Status, and Quit Confirm layouts remain
    contained and centered.
- Known remaining gap: live controller/data-model behavior, route-wide
  automated pixel clipping assertions, full focus/scrollbar parity, native
  Vulkan/RTX-vkpt RmlUi rendering, and full visual parity remain pending.

### Round 58 Evidence (2026-07-07)

Round 58 is accepted as the client/cgame `pushmenu` bridge baseline for the
guarded OpenGL RmlUi path. It keeps the Round 57 audio/popup treatment while
making normal legacy menu producers prefer RmlUi routes when `ui_rml_enable`
is active.

- Implementation log:
  `docs-dev/rmlui-round58-pushmenu-popup-bridge-2026-07-07.md`.
- The public RmlUi client API now exposes popup-route detection and popup-route
  opening helpers for callers that need to preserve confirmation-menu
  presentation.
- The legacy client `pushmenu` command path now opens known RmlUi routes
  directly and uses popup route opens for confirmation routes.
- The cgame UI import/export contract is now `CGameUI_Import_v5` /
  `CGameUI_Export_v5` and includes `InsertCommandString` for deterministic
  command insertion across the cgame/client boundary.
- The cgame `pushmenu` command now recognizes registered RmlUi route IDs and
  inserts `ui_rml_runtime_open` or `ui_rml_runtime_popup` before later queued
  launch/script commands.
- Build/stage validation accepted:
  - `meson compile -C builddir-win`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Static validation accepted:
  - `python tools\ui_smoke\check_rmlui_semantics.py`
  - `python tools\ui_smoke\check_rmlui_command_inventory.py`
  - `python tools\ui_smoke\check_rmlui_condition_inventory.py`
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py`
  - `python tools\ui_smoke\check_rmlui_runtime_registry.py`
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --install-dir .install --base-game basew`
  - `python -m pytest tools\ui_smoke`
- Runtime validation accepted:
  - `.install\basew\logs\round58_insert_pushmenu_options.log`: `pushmenu
    options` routed through `ui_rml_runtime_open`, opened
    `ui/rml/shell/options.rml`, requested `open` sound plus `menu` music, and
    reported active runtime status `route='options'`.
  - `.install\basew\logs\round58_insert_pushmenu_quit_confirm.log`:
    `pushmenu quit_confirm` routed through `ui_rml_runtime_popup`, requested
    the popup route, opened `ui/rml/shell/quit_confirm.rml`, requested
    `alert` sound plus `menu` music, and reported active runtime status
    `route='quit_confirm'`.
  - `.install\basew\logs\round58_insert_pushmenu_forfeit_confirm.log`,
    `.install\basew\logs\round58_insert_pushmenu_leave_match_confirm.log`,
    and
    `.install\basew\logs\round58_insert_pushmenu_tourney_replay_confirm.log`:
    the session confirmation routes routed through `ui_rml_runtime_popup`,
    opened their RmlUi documents, requested `alert` sound plus `menu` music,
    and reported active runtime status for their route IDs.
- Known remaining gap: live session data-model/controller behavior, route-wide
  automated pixel clipping assertions, full focus/scrollbar parity, native
  Vulkan/RTX-vkpt RmlUi rendering, and full visual parity remain pending.

### Round 59 Evidence (2026-07-07)

Round 59 is accepted as the Multiplayer hub parity refinement on top of the
Round 58 `pushmenu` bridge. The hub now captures the original pre-RmlUi menu
intent in the actual RmlUi page instead of exposing a starter-only custom
connect command.

- Implementation log:
  `docs-dev/rmlui-round59-multiplayer-hub-parity-refinement-2026-07-07.md`.
- `assets/ui/rml/multiplayer/multiplayer.rml` now uses the shared shell grid
  contract and declares real legacy command strings for:
  - q2servers.com browsing
  - address-book/broadcast browsing
  - demo browsing
  - Start Server setup defaults followed by `pushmenu startserver`
  - Player Setup
  - Options
- The dead `multiplayer.connect_address` command is removed.
- `assets/ui/rml/common/theme/shell.rcss` owns the final Multiplayer grid
  placement and primary Start Server treatment; stale session-theme
  Multiplayer placement selectors were removed.
- Runtime-only placeholder data-model attributes were removed from the hub
  until live controllers exist, eliminating missing-model warnings during
  staged route opens.
- Build/stage validation accepted:
  - `meson compile -C builddir-win`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Static validation accepted:
  - `python tools\ui_smoke\check_rmlui_semantics.py`
  - `python tools\ui_smoke\check_rmlui_command_inventory.py`
  - `python tools\ui_smoke\check_rmlui_condition_inventory.py`
  - `python tools\ui_smoke\check_rmlui_data_model_inventory.py`
  - `python tools\ui_smoke\check_rmlui_navigation_graph.py`
  - `python tools\ui_smoke\check_rmlui_metadata_sync.py`
  - `python tools\ui_smoke\check_rmlui_runtime_registry.py`
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew`
  - `python -m pytest tools\ui_smoke` (`224 passed`)
- Runtime/visual validation accepted:
  - `.install\basew\logs\round59_pushmenu_multiplayer_clean.log`:
    `pushmenu multiplayer` routed through `ui_rml_runtime_open`, opened
    `ui/rml/multiplayer/multiplayer.rml`, requested `open` sound plus `menu`
    music, reported active runtime status `route='multiplayer'`, and recorded
    Quake II Rerelease TTF font-source markers without missing-model warnings.
  - `.install\basew\logs\round59_multiplayer_visual_final.log`: final visual
    probe opened the Multiplayer route, wrote
    `.install\basew\screenshots\round59_multiplayer_final.tga`, and reported
    active OpenGL RmlUi runtime status.
  - `.tmp\rmlui\round59-screens\round59_multiplayer_final.png`: converted
    visual evidence confirms the two-column Multiplayer hub is contained at
    `960x720`.
  - `.tmp\rmlui\runtime-capture\round59_main_capture.tga`: guarded Main menu
    capture still validates at `960x720` after the shell-style changes.
- Known remaining gap: live multiplayer/session controllers, route-wide
  automated pixel clipping assertions, full keyboard/controller navigation
  parity, native Vulkan/RTX-vkpt RmlUi rendering, and full visual parity remain
  pending.

### Round 60 Evidence (2026-07-07)

Round 60 is accepted as the Video Setup parity refinement on top of the
Round 58 `pushmenu` bridge and the Round 59 menu-audio/layout baseline. The
page now restores the original pre-RmlUi Video Setup control set instead of
exposing the earlier starter subset.

- Implementation log:
  `docs-dev/rmlui-round60-video-settings-parity-refinement-2026-07-07.md`.
- `assets/ui/rml/settings/video.rml` now exposes typed controls for:
  - `r_fullscreen`
  - `r_borderless` as the original Off/Fullscreen/Always select
  - `pushmenu multimonitor`
  - `gl_swapinterval`
  - `gl_multisamples`
  - `r_gamma`
  - `r_hwgamma`
  - `gl_picmip`
  - `gl_texturemode`, including the bilinear value
  - `gl_anisotropy`
  - `gl_saturation`
  - `intensity`
  - `gl_coloredlightmaps`
  - `gl_brightness`
  - `gl_shaders`
- `assets/ui/rml/common/theme/settings.rcss` now owns a compact three-column
  Video Setup layout and shared action-row button sizing.
- Build/stage validation accepted:
  - `meson compile -C builddir-win`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Static validation accepted:
  - `python -m pytest tools\ui_smoke` (`224 passed`)
- Runtime/visual validation accepted:
  - `.install\basew\logs\round60_video_visual_compact.log`: `pushmenu video`
    routed through `ui_rml_runtime_open`, opened `ui/rml/settings/video.rml`,
    requested `open` sound plus `menu` music, reported active runtime status
    `route='video'`, recorded Quake II Rerelease TTF font-source markers, and
    rendered `60` frames at `960x720`.
  - `.tmp\rmlui\round60-screens\round60_video_compact.png`: converted visual
    evidence confirms the three-column Video Setup page keeps all restored
    controls visible above Back/Close at `960x720`.
- Known remaining gap: full live settings persistence/navigation parity,
  route-wide automated pixel clipping assertions, localization stress, native
  Vulkan/RTX-vkpt RmlUi rendering, and full visual parity remain pending.

### Round 61 Evidence (2026-07-07)

Round 61 is accepted as a settings-family audio and action-row refinement on
top of the Round 60 Video Setup parity pass. It brings the rest of the
settings pages into the same entry audio/music contract and fixes the
remaining Screen/Effects footer clipping found during focused QA.

- Implementation log:
  `docs-dev/rmlui-round61-settings-audio-action-row-refinement-2026-07-07.md`.
- `assets/ui/rml/settings/accessibility.rml`,
  `assets/ui/rml/settings/crosshair.rml`,
  `assets/ui/rml/settings/effects.rml`,
  `assets/ui/rml/settings/input.rml`,
  `assets/ui/rml/settings/language.rml`,
  `assets/ui/rml/settings/multimonitor.rml`,
  `assets/ui/rml/settings/performance.rml`,
  `assets/ui/rml/settings/railtrail.rml`, and
  `assets/ui/rml/settings/screen.rml` now request `data-menu-music="menu"`
  and `data-menu-sound-open="open"`.
- `assets/ui/rml/settings/screen.rml` converts Crosshair Setup into a typed
  settings action row and uses a two-column HUD/Console+Scale layout.
- `assets/ui/rml/settings/effects.rml` converts Railgun Trail Setup into a
  typed settings action row and uses a two-column Rendering/Gameplay layout.
- `assets/ui/rml/common/theme/settings.rcss` now provides shared compact
  two-column sizing for Screen Setup and Effects Setup.
- Build/stage validation accepted:
  - `meson compile -C builddir-win`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Static validation accepted:
  - `python -m pytest tools\ui_smoke` (`224 passed`)
- Runtime/visual validation accepted:
  - `.install\basew\logs\round61_settings_audio_actionrows_final.log`:
    `pushmenu screen` and `pushmenu effects` both routed through
    `ui_rml_runtime_open`, opened their settings documents, requested `open`
    sound plus `menu` music, reported active runtime status, and recorded
    Quake II Rerelease TTF font-source markers.
  - `.tmp\rmlui\round61-screens\round61_screen_actionrow_final.png`: visual
    evidence confirms the Screen Setup two-column layout contains HUD,
    Console, and Scale controls above Back/Close at `960x720`.
  - `.tmp\rmlui\round61-screens\round61_effects_actionrow_final.png`: visual
    evidence confirms the Effects Setup two-column layout contains Rendering
    and Gameplay controls above Back/Close at `960x720`.
- Known remaining gap: live settings persistence, route-wide automated pixel
  clipping assertions, full keyboard/controller navigation parity, native
  Vulkan/RTX-vkpt RmlUi rendering, and full visual parity remain pending.

### Round 62 Evidence (2026-07-07)

Round 62 is accepted as the single-player/local-session audio and Start Server
layout refinement on top of the Round 61 settings-family baseline. It brings
the remaining Agent 4 single-player pages into the same entry audio contract
and fixes Start Server containment under the static fallback renderer.

- Implementation log:
  `docs-dev/rmlui-round62-singleplayer-audio-startserver-refinement-2026-07-07.md`.
- `assets/ui/rml/singleplayer/skill_select.rml`,
  `assets/ui/rml/singleplayer/loadgame.rml`,
  `assets/ui/rml/singleplayer/savegame.rml`,
  `assets/ui/rml/singleplayer/gameflags.rml`, and
  `assets/ui/rml/singleplayer/startserver.rml` now request
  `data-menu-music="menu"` and `data-menu-sound-open="open"`.
- Skill Select difficulty actions now carry explicit `confirm` menu sounds.
- Start Server's Deathmatch Flags and Begin Game actions now carry explicit
  `open` and `confirm` sounds.
- Start Server now uses a three-column layout: Server/actions, Match Setup,
  and Rules, with compact sizing in `common/theme/singleplayer.rcss`.
- Build/stage validation accepted:
  - `meson compile -C builddir-win`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Static validation accepted:
  - `python -m pytest tools\ui_smoke` (`224 passed`)
- Runtime/visual validation accepted:
  - `.install\basew\logs\round62_singleplayer_audio_actions_final3.log`:
    `pushmenu skill_select` and `pushmenu startserver` both routed through
    `ui_rml_runtime_open`, opened their RmlUi documents, requested `open`
    sound plus `menu` music, reported active runtime status, and recorded
    Quake II Rerelease TTF font-source markers.
  - `.tmp\rmlui\round62-screens\round62_skill_select_audio_final3.png`:
    visual evidence confirms Skill Select remains contained at `960x720`.
  - `.tmp\rmlui\round62-screens\round62_startserver_audio_final3.png`:
    visual evidence confirms Start Server's static fallback content and footer
    controls are visible at `960x720`.
- Known remaining gap: live condition evaluation for deathmatch-only and
  coop-only rows, route-wide automated pixel clipping assertions, full
  keyboard/controller navigation parity, native Vulkan/RTX-vkpt RmlUi
  rendering, and full visual parity remain pending.

### Round 63 Evidence (2026-07-07)

Round 63 is accepted as the utility-family audio and layout refinement on top
of the Round 62 single-player/local-session baseline. It brings the remaining
Agent 5 utility pages into the shared menu audio contract and fixes the
remaining representative utility long-list containment issues at `960x720`.

- Implementation log:
  `docs-dev/rmlui-round63-utility-audio-layout-refinement-2026-07-07.md`.
- `assets/ui/rml/utility/addressbook.rml`,
  `assets/ui/rml/utility/demos.rml`,
  `assets/ui/rml/utility/keys.rml`,
  `assets/ui/rml/utility/legacykeys.rml`,
  `assets/ui/rml/utility/players.rml`,
  `assets/ui/rml/utility/servers.rml`,
  `assets/ui/rml/utility/ui_list.rml`, and
  `assets/ui/rml/utility/weapons.rml` now request
  `data-menu-music="menu"` and `data-menu-sound-open="open"`.
- Utility actions now carry explicit intent sounds for route/browser opens,
  apply/connect/play confirmations, refresh/paging/key-capture changes, and
  Back/Return/Reset cancels.
- Keybind capture routes now declare `data-action-type="capture"`, and the
  Key Bindings movement Backpedal control no longer collides with the footer
  Back control id.
- `assets/ui/rml/common/theme/utility.rcss` now provides bounded utility
  browser, player, address-field, keybind, and weapon-bind sizing.
- Build/stage validation accepted:
  - `meson compile -C builddir-win`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Static validation accepted:
  - `python -m pytest tools\ui_smoke` (`224 passed`)
- Runtime/visual validation accepted:
  - `.install\basew\logs\round63_utility_addressbook_pushmenu_layout_final.log`,
    `.install\basew\logs\round63_utility_demos_pushmenu_layout_final.log`,
    `.install\basew\logs\round63_utility_keys_pushmenu_layout_final.log`,
    `.install\basew\logs\round63_utility_legacykeys_pushmenu_layout_final.log`,
    `.install\basew\logs\round63_utility_players_pushmenu_layout_final.log`,
    `.install\basew\logs\round63_utility_servers_pushmenu_layout_final.log`,
    `.install\basew\logs\round63_utility_ui_list_pushmenu_layout_final.log`,
    and
    `.install\basew\logs\round63_utility_weapons_pushmenu_layout_final2.log`:
    all eight utility routes route through the `pushmenu` bridge into
    `ui_rml_runtime_open`, request `open` sound plus `menu` music, report
    active OpenGL RmlUi runtime status, and record Quake II Rerelease TTF
    font-source markers.
  - `.tmp\rmlui\round63-screens\round63_utility_addressbook_pushmenu_layout_final.png`:
    visual evidence confirms all sixteen Address Book fields are visible in a
    four-column bounded grid above the footer.
  - `.tmp\rmlui\round63-screens\round63_utility_keys_pushmenu_layout_final.png`:
    visual evidence confirms Key Bindings uses a three-column capture grid
    with footer controls clear of the list.
  - `.tmp\rmlui\round63-screens\round63_utility_weapons_pushmenu_layout_final2.png`:
    visual evidence confirms Weapon Bindings uses a two-column arsenal layout
    with the Grenades row fully visible above Back.
  - The same round directory contains final captures for Demos, Legacy Keys,
    Player Setup, Servers, and Session List.
- Known remaining gap: live browser/list/keybind/player preview controllers,
  full keyboard/controller navigation parity, automated route-wide pixel
  clipping assertions, native Vulkan/RTX-vkpt RmlUi rendering, and full visual
  parity remain pending.

### Round 75 Evidence (2026-07-08)

Round 75 is accepted as the next menu coverage and fallback refinement pass on
top of the Round 74 low-noise route-sweep baseline. It keeps the OpenGL-only
RmlUi renderer boundary unchanged while improving direct-open report/list
coverage and aligning static condition validation with the runtime grammar.

- Implementation log:
  `docs-dev/rmlui-round75-menu-coverage-refinement-2026-07-08.md`.
- Direct `match_stats` probes now show a static report-shaped fallback block
  when `ui_matchstats_line_0` is absent or falsey, while preserving the live
  `ui_matchstats_line_*` bindings and visibility gates.
- Direct `tourney_mapchoices` probes now show a static two-line fallback block
  when `ui_tourney_mapchoice_line_0` is absent or falsey, while preserving the
  live `ui_tourney_mapchoice_line_*` bindings and visibility gates.
- `download_status` now presents a contained idle state for
  `ui_download_active=0`, and the progress meter always shows an explicit `%`
  unit next to the bound numeric value.
- `tools/ui_smoke/check_rmlui_condition_inventory.py` now accepts leading
  `!cvar` static conditions, matching the compiled runtime evaluator, with
  focused pytest coverage for negated condition expressions.
- Build/stage validation accepted:
  - `meson compile -C builddir-win`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Static validation accepted:
  - `python -m pytest tools\ui_smoke -q` (`225 passed`)
  - `git diff --check` (clean apart from existing CRLF warnings)
- Runtime/visual validation accepted:
  - `.tmp\rmlui\round75-menu-improvements\round75_match_stats_960x720.png`,
    `round75_tourney_mapchoices_960x720.png`, and
    `round75_download_status_960x720.png` confirm report fallback rows,
    tournament map-choice fallback rows, and Download Status idle/progress
    presentation at `960x720`.
  - `.tmp\rmlui\round75-menu-improvements\round75_menu_improvements_all_route_open.log`
    opened `59` documents across `58` registered route IDs, recorded `58`
    runtime status samples, had `0` missing data-model notice lines at default
    settings, and had `0` parser/CSS/texture/runtime error lines.
- Known remaining gap: live list/save/keybind/player-preview/session
  data-model controllers, live match/tournament/download controller parity,
  full keyboard/controller navigation parity, true narrow-viewport capture
  parity, route-wide automated pixel assertions, native Vulkan/RTX-vkpt RmlUi
  rendering, and full visual parity remain pending.

### Round 74 Evidence (2026-07-08)

Round 74 is accepted as the next menu coverage and runtime-signal refinement
pass on top of the Round 73 direct-fallback baseline. It keeps controller-stub
route probes readable, tightens utility/list fallback layouts, and removes
expected missing data-model notices from default all-route sweep logs without
changing the OpenGL-only RmlUi renderer boundary.

- Implementation log:
  `docs-dev/rmlui-round74-menu-coverage-refinement-2026-07-08.md`.
- Runtime logging now suppresses exact RmlUi "Could not locate data model"
  notices by default through `ui_rml_log_missing_data_models 0`, while
  preserving `ui_rml_log_missing_data_models 1` as the opt-in diagnostic for
  live-controller work.
- Direct `ui_list` probes now show authored extra actions, item rows, and page
  controls unless a live cvar explicitly sets the relevant show cvar to `0`.
- Direct `map_selector` probes keep authored map candidates and the countdown
  fallback visible, with the status column contained at `960x720`.
- Direct `tourney_veto` probes keep the inactive fallback panel visible and
  bounded when live tournament cvars are absent.
- `servers` and `demos` now use explicit table/row/cell display rules and
  stable columns so empty states render under full-width headers instead of
  beside them.
- Build/stage validation accepted:
  - `meson compile -C builddir-win`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Static validation accepted:
  - `python -m pytest tools\ui_smoke -q` (`224 passed`)
  - `git diff --check` (clean apart from existing CRLF warnings)
- Runtime/visual validation accepted:
  - `.tmp\rmlui\round74-menu-improvements\round74_servers_960x720.png`,
    `round74_demos_960x720.png`, `round74_ui_list_960x720.png`,
    `round74_map_selector_960x720.png`, and
    `round74_tourney_veto_960x720.png` confirm the refined utility tables,
    generic list fallback, map-selector fallback, and veto fallback.
  - `.install\basew\logs\round74_menu_improvements_all_route_open.log` opened
    `59` documents across `58` registered route IDs, recorded `58` runtime
    status samples, had `0` missing data-model notice lines at default
    settings, and had `0` parser/CSS/texture/runtime error lines.
- Known remaining gap: live list/save/keybind/player-preview/session
  data-model controllers, full keyboard/controller navigation parity, true
  narrow-viewport capture parity, route-wide automated pixel assertions,
  native Vulkan/RTX-vkpt RmlUi rendering, and full visual parity remain
  pending.

### Round 73 Evidence (2026-07-08)

Round 73 is accepted as the follow-up menu coverage and runtime-fallback
refinement pass on top of the Round 72 containment baseline. It tightens direct
session route behavior, long-list containment, and readable fallback text
without changing the OpenGL-only RmlUi renderer boundary.

- Implementation log:
  `docs-dev/rmlui-round73-menu-coverage-refinement-2026-07-08.md`.
- Runtime text binding now preserves authored fallback text when
  `data-bind-cvar`, `data-label-cvar`, or `data-bind="cvars.*"` points at an
  existing but empty cvar. Direct `join`/`dm_join` probes no longer erase
  lobby titles or team/action labels.
- `callvote_main`, `join`, and `dm_join` use deterministic two-column command
  grids with route-contained heights so command rows and footer controls remain
  inside the `960x720` reference capture.
- `admin_commands` now presents command names, descriptions, and usage strings
  as readable command-reference rows instead of crowded inline columns.
- Save/load slot surfaces no longer depend on fixed absolute slot coordinates;
  they use a wrapping scroll-contained list layout.
- `startserver` keeps the future `$$com_maplist` bridge metadata but shows
  `q2dm1 - The Edge` as the visible static fallback instead of the raw macro
  token.
- `admin_menu` keeps Replay Game on the popup-confirmation path through
  `tourney_replay_confirm`.
- Build/stage validation accepted:
  - `meson compile -C builddir-win`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Static validation accepted:
  - `python -m pytest tools\ui_smoke -q` (`224 passed`)
  - `git diff --check` (clean apart from existing CRLF warnings)
  - `decorator_image_refs=78`
  - `missing_refs=0`
- Runtime/visual validation accepted:
  - `.tmp\rmlui\round73-menu-improvements\round73_callvote_main_final3_960x720.png`,
    `round73_dm_join_final5_960x720.png`,
    `round73_join_final2_960x720.png`,
    `round73_admin_commands_final_960x720.png`,
    `round73_startserver_final_960x720.png`,
    `round73_loadgame_final_960x720.png`,
    `round73_savegame_final_960x720.png`, and
    `round73_vote_menu_960x720.png` confirm the refined session grids,
    fallback labels, admin command rows, save/load layout, and Start Server
    fallback.
  - `.install\basew\logs\round73_menu_improvements_all_route_open.log` opened
    `59` documents across `58` registered route IDs, recorded `58` runtime
    status samples, and had `0` parser/CSS/texture/runtime error lines after
    excluding expected missing data-model notices.
- Known remaining gap: live list/save/keybind/player-preview/session
  data-model controllers, full keyboard/controller navigation parity, true
  narrow-viewport capture parity, route-wide automated pixel assertions,
  native Vulkan/RTX-vkpt RmlUi rendering, and full visual parity remain
  pending.

### Round 72 Evidence (2026-07-08)

Round 72 is accepted as the menu coverage and refinement pass on top of the
Round 71 stateful widget-skin baseline. It resolves visible coverage gaps in
direct route probes and tightens shared menu containment without changing the
OpenGL-only RmlUi renderer boundary.

- Implementation log:
  `docs-dev/rmlui-round72-menu-coverage-refinement-2026-07-08.md`.
- Text-box, select, and combo SVG skins no longer draw placeholder lines or
  duplicate select arrows over real menu text.
- Shared settings/utility rows hide the old widget pictograms so stateful widget
  skins carry the UI affordance.
- Range controls no longer expose RmlUi stepper arrow children, and dense
  settings slider tracks are wider.
- Vertical scrollbar dimensions are explicit and horizontal scrollbar strips are
  hidden, preventing scroll skins from covering overflow routes.
- Main-menu and hub button widths now leave room for padding and borders,
  preventing the right-edge clipping seen in earlier screenshots.
- `callvote_main` has direct-route fallback visibility when live session cvars
  are absent while still hiding explicitly disabled options, and
  `admin_commands` no longer requires a missing data model for its static
  command-reference list.
- Build/stage validation accepted:
  - `meson compile -C builddir-win`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Static validation accepted:
  - `python -m pytest tools\ui_smoke -q` (`224 passed`)
  - `git diff --check` (clean apart from existing CRLF warnings)
  - `widget_skin_svgs=55`
  - `widget_skin_refs=78`
  - all common-theme/component decorator references resolve, and all widget
    SVGs remain at or below `256x256`.
- Runtime/visual validation accepted:
  - `.tmp\rmlui\round72-menu-improvements\round72_main_960x720.png`,
    `round72_options_960x720.png`, `round72_video_refined_960x720.png`,
    `round72_sound_refined_960x720.png`, `round72_startserver_960x720.png`,
    `round72_players_960x720.png`, `round72_addressbook_960x720.png`,
    `round72_keys_960x720.png`,
    `round72_callvote_main_refined_960x720.png`,
    `round72_admin_commands_final_960x720.png`,
    `round72_download_status_960x720.png`, and
    `round72_quit_popup_960x720.png` confirm the refined widgets, shell hubs,
    settings pages, utility pages, session fallback routes, and popup frame.
  - `.install\basew\logs\round72_menu_improvements_all_route_open.log` opened
    `59` documents across `58` registered route IDs, recorded `58` runtime
    status samples, and had `0` bad lines matching SVG texture failure,
    invalid property, syntax error, missing texture, unsupported, fallback,
    failure, error, exception, unhandled, parser, or screenshot write failure.
- Known remaining gap: the staged Windows launch path still reported a
  `960x720` RmlUi runtime canvas when smaller `r_geometry` values were
  requested, so true narrow-viewport capture parity remains pending. Live
  list/save/keybind/player-preview/session data-model controllers, full
  keyboard/controller navigation parity, automated route-wide pixel assertions,
  native Vulkan/RTX-vkpt RmlUi rendering, and full visual parity also remain
  pending.

### Round 71 Evidence (2026-07-08)

Round 71 is accepted as the first stateful widget-skin pass on top of the
Round 70 widget-marker baseline. It adds renderer-safe SVG skins for actual
control surfaces and wires them through RmlUi `decorator: image(...)` rules in
the shared themes.

- Implementation log:
  `docs-dev/rmlui-round71-stateful-widget-skins-2026-07-08.md`.
- The new skin library contains `55` SVG assets under
  `assets/ui/rml/common/skins/widgets/`.
- Shared skin references now cover buttons, primary/destructive buttons,
  text boxes, combo/select boxes, drop-down panels/options, checkboxes,
  range tracks/thumbs, progress tracks/fills, scrollbar tracks/thumbs, arrow
  boxes, and confirmation popup frames.
- Build/stage validation accepted:
  - `meson compile -C builddir-win`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Static validation accepted:
  - `python -m pytest tools\ui_smoke -q` (`224 passed`)
  - `git diff --check` (clean apart from existing CRLF warnings)
  - `widget_skin_svgs=55`
  - `widget_skin_refs=74`
- Runtime/visual validation accepted:
  - `.install\basew\logs\round71_video_stateful_skins.log`,
    `.install\basew\logs\round71_sound_stateful_skins.log`,
    `.install\basew\logs\round71_startserver_stateful_skins.log`,
    `.install\basew\logs\round71_download_status_stateful_skins.log`, and
    `.install\basew\logs\round71_quit_popup_stateful_skins.log` show active
    OpenGL RmlUi route opens, runtime status markers, TGA screenshot writes,
    and widget-skin SVG texture generation without parser, texture-load, or
    SVG loader failure markers.
  - `.tmp\rmlui\round71-widget-skins\round71_video_stateful_skins.png`,
    `.tmp\rmlui\round71-widget-skins\round71_sound_stateful_skins.png`,
    `.tmp\rmlui\round71-widget-skins\round71_startserver_stateful_skins.png`,
    `.tmp\rmlui\round71-widget-skins\round71_download_status_stateful_skins.png`,
    and `.tmp\rmlui\round71-widget-skins\round71_quit_popup_stateful_skins.png`
    confirm stateful button, select/combo, text-box, checkbox, range,
    progress, and popup-frame skins render in representative menus.
  - `.install\basew\logs\round71_stateful_skins_all_route_open.log` opened
    `59` documents across `58` unique route IDs, recorded `58` runtime status
    samples, and had `0` bad lines matching SVG texture failure, invalid
    property, syntax error, missing texture, unsupported, fallback, failure,
    error, exception, unhandled, parser, or screenshot write failure.
- Known remaining gap: automated route-wide pixel assertions for every widget
  state, native Vulkan/RTX-vkpt RmlUi rendering, full keyboard/controller
  navigation parity, true narrow-viewport capture parity, and full visual
  parity remain pending.

### Round 70 Evidence (2026-07-08)

Round 70 is accepted as the widget-specific SVG asset refinement on top of the
Round 69 OpenGL SVG rasterization baseline. It removes the previous
high-level command-button pictograms and redirects authored SVG usage to
settings and utility widgets.

- Implementation log:
  `docs-dev/rmlui-round70-widget-svg-assets-2026-07-08.md`.
- The old `assets/ui/rml/common/icons/ux/` command-icon directory was removed.
- The new widget library contains `18` SVG assets under
  `assets/ui/rml/common/icons/widgets/`.
- Main, Options, Game, Multiplayer, Single Player, and Quit confirmation
  routes no longer use visible command pictograms or the `ux-icon-button`
  contract.
- Settings and utility forms now use `130` compact widget markers for
  toggles, selects, combo boxes, image-value selectors, ranges, fields,
  numeric fields, actions, progress rows, Player Setup, and Address Book.
- Build/stage validation accepted:
  - `meson compile -C builddir-win`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Static validation accepted:
  - `python -m pytest tools\ui_smoke -q` (`224 passed`)
  - `git diff --check` (clean apart from existing CRLF warnings)
  - Static reference scan found no remaining `common/icons/ux`,
    `ux-icon-button`, `ux-button-icon`, or `popup-title-icon` RML/RCSS
    references.
- Runtime/visual validation accepted:
  - `.install\basew\logs\round70_video_widget_svg_final2.log`,
    `.install\basew\logs\round70_sound_widget_svg.log`,
    `.install\basew\logs\round70_startserver_widget_svg_final.log`,
    `.install\basew\logs\round70_players_widget_svg.log`,
    `.install\basew\logs\round70_addressbook_widget_svg.log`,
    `.install\basew\logs\round70_download_status_widget_svg.log`, and
    `.install\basew\logs\round70_main_plain_no_menu_icons.log` show active
    OpenGL RmlUi route opens, runtime status markers, screenshot writes, and
    widget SVG texture generation without SVG loader failure markers.
  - `.tmp\rmlui\round70-widget-svg\round70_video_widget_svg_final2.png`
    confirms compact Video widgets keep select/action/toggle/range markers
    readable without clipping the three-column setup layout.
  - `.tmp\rmlui\round70-widget-svg\round70_sound_widget_svg.png` confirms
    Sound widgets render select/range/toggle/number markers in the two-column
    layout.
  - `.tmp\rmlui\round70-widget-svg\round70_startserver_widget_svg_final.png`
    confirms Start Server widgets render combo/select/field/number markers in
    the three-column setup layout.
  - `.tmp\rmlui\round70-widget-svg\round70_players_widget_svg.png` and
    `.tmp\rmlui\round70-widget-svg\round70_addressbook_widget_svg.png`
    confirm utility-form widget markers render without crowding fields.
  - `.tmp\rmlui\round70-widget-svg\round70_download_status_widget_svg.png`
    confirms progress widget markers render on Downloading Content.
  - `.tmp\rmlui\round70-widget-svg\round70_main_plain_no_menu_icons.png`
    confirms Main menu command buttons are plain text again.
- Known remaining gap: SVGs are static widget type markers rather than dynamic
  state-aware skins; full SVG plugin/spec parity, native Vulkan/RTX-vkpt RmlUi
  rendering, full keyboard/controller navigation parity, automated route-wide
  pixel clipping assertions, true narrow-viewport capture parity, and full
  visual parity remain pending.

### Round 69 Evidence (2026-07-07)

Round 69 is accepted as the first SVG UX asset integration pass on top of the
Round 68 meter-widget and Crosshair containment baseline. It keeps popup
confirmations, menu audio, and Quake II Rerelease TTF loading intact while
adding first-party SVG icons to high-level menu commands and a conservative
OpenGL SVG texture generation path.

- Implementation log:
  `docs-dev/rmlui-round69-svg-ux-assets-2026-07-07.md`.
- The OpenGL RmlUi bridge now detects `.svg` texture requests, parses the
  supported first-party SVG subset, supersamples it into premultiplied RGBA,
  and uploads through the existing generated texture path.
- The first UX icon set contains `32` first-party assets under
  `assets/ui/rml/common/icons/ux/`, with an asset README documenting the
  conservative shape/path subset that is currently supported.
- Main, Options, Game, Multiplayer, Single Player, and Quit confirmation popup
  surfaces now use shared icon-button styling and visible command/state icons.
- Reusable command-button templates now carry the same icon/label layout
  contract so future authored command buttons can opt into the asset set.
- Build/stage validation accepted:
  - `meson compile -C builddir-win`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Static validation accepted:
  - `python -m pytest tools\ui_smoke -q` (`224 passed`)
  - `git diff --check` (clean apart from existing CRLF warnings)
- Runtime/visual validation accepted:
  - `.install\basew\logs\round69_main_svg_icons.log`,
    `.install\basew\logs\round69_game_svg_icons.log`,
    `.install\basew\logs\round69_options_svg_icons.log`,
    `.install\basew\logs\round69_multiplayer_svg_icons.log`, and
    `.install\basew\logs\round69_quit_popup_svg_icons.log` show active
    OpenGL RmlUi routes or popup routing, Quake II Rerelease TTF font-source
    markers, screenshot writes, and SVG texture generation markers.
  - The focused log scan recorded generated SVG texture markers without SVG
    loader failure markers: Main `6`, Game `16`, Options `20`,
    Multiplayer `12`, and Quit popup `8`.
  - `.tmp\rmlui\round69-svg-icons\round69_main_svg_icons.png` confirms the
    Main menu column uses icons without clipping the right edge of the
    buttons.
  - `.tmp\rmlui\round69-svg-icons\round69_game_svg_icons.png` confirms Game
    hub icons render in the Session, Browse, and Save And Exit groups.
  - `.tmp\rmlui\round69-svg-icons\round69_options_svg_icons.png` confirms the
    Options hub icons render across all option sections while staying inside
    the bounded command grid.
  - `.tmp\rmlui\round69-svg-icons\round69_multiplayer_svg_icons.png` confirms
    Multiplayer hub icons render across Find, Host, and Profile sections.
  - `.tmp\rmlui\round69-svg-icons\round69_quit_popup_svg_icons.png` confirms
    the Quit popup title/action icons render inside the centered confirmation
    dialog.
- Known remaining gap: the active implementation supports a first-party SVG
  subset rather than the complete SVG specification; full RmlUi SVG
  plugin/LunaSVG integration, dynamic SVG tinting, broader route-wide icon
  rollout, native Vulkan/RTX-vkpt RmlUi rendering, full keyboard/controller
  navigation parity, automated route-wide pixel clipping assertions, true
  narrow-viewport capture parity, and full visual parity remain pending.

### Round 68 Evidence (2026-07-07)

Round 68 is accepted as the cvar-driven meter-widget and Crosshair layout
refinement on top of the Round 67 runtime cvar/condition baseline. It keeps
popup confirmations, menu audio, and Quake II Rerelease TTF loading intact
while improving range/progress setting readability without adding unsupported
SVG assets.

- Implementation log:
  `docs-dev/rmlui-round68-meter-widget-crosshair-layout-refinement-2026-07-07.md`.
- The compiled runtime now consumes `data-meter-cvar` plus
  `data-meter-min`/`data-meter-max` and updates meter fill widths from live
  cvars during the existing binding refresh.
- Video, Sound, Screen, Crosshair, Rail Trail, and Download Status range or
  progress rows now use RmlUi-native meter/value badges with live cvar text.
- Crosshair Setup now uses two bounded columns so the original Crosshair and
  Hit Feedback controls remain visible above the footer at `960x720`.
- SVG widget assets are intentionally deferred because the active renderer
  path loads RmlUi textures through the engine image loader and does not yet
  provide SVG rasterization.
- Build/stage validation accepted:
  - `meson compile -C builddir-win`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Static validation accepted:
  - `python -m pytest tools\ui_smoke -q` (`224 passed`)
- Runtime/visual validation accepted:
  - `.install\basew\logs\round68_video_meter_widgets.log`,
    `.install\basew\logs\round68_sound_meter_widgets.log`,
    `.install\basew\logs\round68_crosshair_meter_widgets_final.log`, and
    `.install\basew\logs\round68_quit_popup_route.log` show active OpenGL
    RmlUi routes, consumed open/alert sound plus menu-music metadata,
    screenshot writes, Quake II Rerelease TTF font-source markers, and Quit
    popup routing.
  - `.tmp\rmlui\round68-menu-refine\round68_video_meter_widgets.png`
    confirms the Video page meter/value badges stay contained in the
    three-column setup layout.
  - `.tmp\rmlui\round68-menu-refine\round68_sound_meter_widgets.png`
    confirms Sound latency, sound volume, and music volume meters render in
    the two-column Sound Setup layout.
  - `.tmp\rmlui\round68-menu-refine\round68_crosshair_meter_widgets_final.png`
    confirms Crosshair and Hit Feedback controls fit in the two-column layout
    with footer controls clear.
  - `.tmp\rmlui\round68-menu-refine\round68_quit_popup_route.png`
    confirms Quit remains on the centered popup confirmation route.
- Known remaining gap: live list/save/keybind/player-preview/session data
  models, true SVG/vector widget asset support, native slider thumb/track
  skinning beyond styled RmlUi-native badges, full keyboard/controller
  navigation parity, automated route-wide pixel clipping assertions, true
  narrow-viewport capture parity, native Vulkan/RTX-vkpt RmlUi rendering, and
  full visual parity remain pending.

### Round 67 Evidence (2026-07-07)

Round 67 is accepted as the compiled-runtime cvar, condition, and widget
binding refinement on top of the Round 66 containment/popup baseline. It keeps
popup confirmations and pre-RmlUi command intent intact while making authored
RmlUi controls respond to live cvars instead of static fallback text.

- Implementation log:
  `docs-dev/rmlui-round67-runtime-cvar-condition-widget-refinement-2026-07-07.md`.
- The runtime now initializes and writes back `data-cvar` form controls,
  refreshes `data-bind-cvar`, `data-label-cvar`, and
  `data-bind="cvars.*"` text, and evaluates `data-visible-if` /
  `data-show-if` plus `data-enable-if` / `data-enabled-if` expressions.
- Settings rows now use typed control accents and live value badges for select,
  range, progress, toggle, action, and field controls.
- DM Join now uses cvar-backed title/subtitle and button labels, hides
  cvar-gated unavailable actions, reflows available session actions into a
  bounded two-column grid, and keeps Leave Match visually destructive.
- Build/stage validation accepted:
  - `meson compile -C builddir-win`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Static validation accepted:
  - `python -m pytest tools\ui_smoke -q` (`224 passed`)
- Runtime/visual validation accepted:
  - `.install\basew\logs\round67_video_tga_probe.log`,
    `.install\basew\logs\round67_sound_cvar_binding_final.log`,
    `.install\basew\logs\round67_startserver_conditions_final.log`,
    `.install\basew\logs\round67_dm_join_conditions_flex_final3.log`, and
    `.install\basew\logs\round67_quit_popup_confirm_final.log` show active
    OpenGL RmlUi routes, consumed open/alert sound plus menu-music metadata,
    screenshot writes, and Quake II Rerelease TTF font-source markers.
  - `.tmp\rmlui\round67-menu-refine\round67_video_cvar_binding_final.png`
    and
    `.tmp\rmlui\round67-menu-refine\round67_sound_cvar_binding_final.png`
    confirm settings controls and value badges are populated from cvars.
  - `.tmp\rmlui\round67-menu-refine\round67_startserver_conditions_final.png`
    confirms cvar-gated Start Server branches hide unavailable coop-only rows.
  - `.tmp\rmlui\round67-menu-refine\round67_dm_join_conditions_flex_final3.png`
    confirms live cvar title/subtitle/labels, hidden unavailable actions, and
    bounded session command layout.
  - `.tmp\rmlui\round67-menu-refine\round67_quit_popup_confirm_final.png`
    confirms Quit remains on the centered popup confirmation route.
- Known remaining gap: live list/save/keybind/player-preview/session data
  models, native range thumb/fill richness, full keyboard/controller
  navigation parity, automated route-wide pixel clipping assertions, true
  narrow-viewport capture parity, native Vulkan/RTX-vkpt RmlUi rendering, and
  full visual parity remain pending. DM Join still emits expected missing
  `session.dm_join*` data-model warnings until the session controller bridge
  is promoted.

### Round 66 Evidence (2026-07-07)

Round 66 is accepted as the menu containment, reusable audio-template, and
confirmation-popup visual refinement on top of the Round 65 shell hub/audio
baseline. It keeps the pre-RmlUi command surfaces intact while making fixed
menu panels safer under canvas changes and making popup confirmation routes
feel like compact modal confirmations.

- Implementation log:
  `docs-dev/rmlui-round66-menu-containment-popup-refinement-2026-07-07.md`.
- Shared popup styling now gives Quit, Forfeit, Leave Match, and Tournament
  Replay confirmations consistent modal framing, danger/primary/secondary
  action states, and side-by-side Yes/No actions while preserving their
  original commands.
- Shell, settings, single-player, utility, and session fixed panels now use
  looser minimum widths and contained scroll overflow instead of clipping
  fixed grids/lists when the canvas becomes constrained.
- Shared component templates now declare explicit menu sound/change intent so
  future live command, cvar, save/load, image-grid, list-table, preview, and
  keybind controllers inherit the same audio contract as authored route pages.
- Build/stage validation accepted:
  - `meson compile -C builddir-win`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Static validation accepted:
  - `python -m pytest tools\ui_smoke -q` (`224 passed`)
  - `rg -n -P "<button(?![^>]*data-menu-sound)" assets/ui/rml -g "*.rml"`
    returned no missing authored/template button sound metadata.
  - `git diff --check` reported only LF/CRLF normalization warnings for edited
    files, with no whitespace errors.
- Runtime/visual validation accepted:
  - `.install\basew\logs\round66_quit_popup_final_960x720.log`,
    `.install\basew\logs\round66_options_layout_final.log`,
    `.install\basew\logs\round66_video_widget_layout_final.log`,
    `.install\basew\logs\round66_keys_containment_final.log`, and
    `.install\basew\logs\round66_dm_join_containment_final.log` show active
    OpenGL RmlUi routes, consumed open-sound/menu-music metadata, screenshot
    writes, and Quake II Rerelease TTF font-source markers.
  - `.tmp\rmlui\round66-menu-refine\round66_quit_popup_final_960x720.png`
    confirms Quit uses the popup route with a compact centered modal and
    side-by-side actions.
  - `.tmp\rmlui\round66-menu-refine\round66_options_layout_final.png`,
    `.tmp\rmlui\round66-menu-refine\round66_video_widget_layout_final.png`,
    `.tmp\rmlui\round66-menu-refine\round66_keys_containment_final.png`, and
    `.tmp\rmlui\round66-menu-refine\round66_dm_join_containment_final.png`
    confirm representative hub, settings, utility, and session surfaces remain
    contained with footer actions visible.
- Known remaining gap: live settings/list/keybind/player-preview/session
  controllers, full keyboard/controller navigation parity, automated
  route-wide pixel clipping assertions, true narrow-viewport capture parity,
  native Vulkan/RTX-vkpt RmlUi rendering, and full visual parity remain
  pending. The staged Windows launch path still reported a `960x720` RmlUi
  runtime canvas when a smaller `r_geometry` was requested, so Round 66 does
  not claim narrow-viewport runtime parity.

### Round 65 Evidence (2026-07-07)

Round 65 is accepted as the shell hub grouping and cross-family action-audio
refinement on top of the Round 64 session baseline. It replaces older flat
hub button walls with grouped RmlUi sections, completes explicit sound intent
metadata for authored route buttons, and keeps popup confirmation routing on
the shared `ui.popup` contract.

- Implementation log:
  `docs-dev/rmlui-round65-shell-hub-audio-refinement-2026-07-07.md`.
- Options, Game, and Multiplayer now use grouped hub sections rather than
  unstructured button stacks. The groups preserve the original pre-RmlUi
  intent for player/input, display/feel, audio/network, session, browse,
  save/exit, find, host, and profile actions.
- Main/Game Quit remain routed through the `quit_confirm` popup; direct Game
  Disconnect remains direct because the pre-RmlUi JSON menu exposed it as a
  direct command rather than a confirmation menu.
- Remaining authored shell/settings/single-player/save-load/download-status
  buttons now carry explicit action sounds, while Single Player and Start
  Server typed widgets add change-sound hints.
- The first grouped capture found Options/Multiplayer right-column clipping;
  final shared hub widths were tightened and revalidated at `960x720`.
- Build/stage validation accepted:
  - `meson compile -C builddir-win`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Static validation accepted:
  - `python -m pytest tools\ui_smoke -q` (`224 passed`)
  - Authored shell, multiplayer, single-player, settings, session, and utility
    route pages have no buttons missing explicit `data-menu-sound`.
  - The same authored route pages all carry `data-menu-music` and
    `data-menu-sound-open` metadata.
- Runtime/visual validation accepted:
  - `.install\basew\logs\round65_main_audio_final.log`,
    `.install\basew\logs\round65_options_hub_final.log`,
    `.install\basew\logs\round65_game_hub_final.log`,
    `.install\basew\logs\round65_multiplayer_hub_final.log`, and
    `.install\basew\logs\round65_singleplayer_audio_tga.log` show active
    OpenGL RmlUi routes, consumed open-sound/menu-music metadata, screenshot
    writes, and Quake II Rerelease TTF font-source markers.
  - `.tmp\rmlui\round65-hub-capture\round65_main_audio_final.png` confirms
    Main buttons are no longer clipped at the right edge.
  - `.tmp\rmlui\round65-hub-capture\round65_options_hub_final.png`,
    `.tmp\rmlui\round65-hub-capture\round65_game_hub_final.png`, and
    `.tmp\rmlui\round65-hub-capture\round65_multiplayer_hub_final.png`
    confirm the grouped hubs fit within the `960x720` capture frame with
    footer actions clear.
  - `.tmp\rmlui\round65-hub-capture\round65_singleplayer_audio.png` confirms
    the Single Player selector/action surface remains contained after adding
    explicit action/change audio metadata.
- Known remaining gap: live settings/list/keybind/player-preview/session
  controllers, full keyboard/controller navigation parity, automated
  route-wide pixel clipping assertions, true narrow-viewport capture parity,
  native Vulkan/RTX-vkpt RmlUi rendering, and full visual parity remain
  pending.

### Round 64 Evidence (2026-07-07)

Round 64 is accepted as the session-family audio, popup-flow, and bounded
layout refinement on top of the Round 63 utility baseline. It brings the
session/match route family into the shared menu audio contract, restores live
vote commands, and keeps representative session hubs/reference pages contained
at `960x720`.

- Implementation log:
  `docs-dev/rmlui-round64-session-audio-layout-refinement-2026-07-07.md`.
- All non-popup session pages now request `data-menu-music="menu"` and
  `data-menu-sound-open="open"`. Confirmation pages remain popup-presented
  and keep alert open sounds plus menu music.
- Session controls now carry explicit action-intent sounds for route opens,
  join/vote confirmations, flag toggles, Back/Return/Close cancels, and
  dangerous confirmation entry points.
- Dynamic session navigation now preserves the original sgame command path
  for Call Vote, MyMap, Host Info, Match Info, Admin, Forfeit, and replay
  picker flows so cvars can be published before `pushmenu` routes through
  RmlUi.
- `vote_menu.rml` now uses the live `ui_vote_*` cvars and
  `worr_vote_yes`, `worr_vote_no`, and `worr_vote_close` commands instead of
  mock `session.vote_*` events.
- `assets/ui/rml/common/theme/session.rcss` now gives the lobby and callvote
  hubs explicit two-column placement and bounds Admin Commands, random vote
  ranges, map/MyMap flags, Match Stats, and Tournament Map Choices in
  scrollable panels above footer actions.
- Build/stage validation accepted:
  - `meson compile -C builddir-win`
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Static validation accepted:
  - `python -m pytest tools\ui_smoke -q` (`224 passed`)
- Runtime/visual validation accepted:
  - `.install\basew\logs\round64_session_dm_join_pushmenu_layout_final2.log`,
    `.install\basew\logs\round64_session_callvote_main_pushmenu_layout_final2.log`,
    `.install\basew\logs\round64_session_admin_commands_pushmenu_layout_final3.log`,
    `.install\basew\logs\round64_session_match_stats_pushmenu_layout_final4.log`,
    and
    `.install\basew\logs\round64_session_forfeit_confirm_pushmenu_layout_final.log`
    show `pushmenu` routing into OpenGL RmlUi, consumed open/alert sound and
    menu-music metadata, active runtime status, and Quake II Rerelease TTF
    font-source markers.
  - `.tmp\rmlui\round64-screens\round64_session_dm_join_pushmenu_layout_final2.png`:
    visual evidence confirms the lobby team/session actions use a contained
    two-column layout with the Forfeit/Leave Match confirmation paths visible.
  - `.tmp\rmlui\round64-screens\round64_session_callvote_main_pushmenu_layout_final2.png`:
    visual evidence confirms the Call Vote hub uses a contained two-column
    layout above Return.
  - `.tmp\rmlui\round64-screens\round64_session_admin_commands_pushmenu_layout_final3.png`:
    visual evidence confirms Admin Commands uses a bounded reference list
    with Back clear of the rows.
  - `.tmp\rmlui\round64-screens\round64_session_match_stats_pushmenu_layout_final4.png`
    and
    `.tmp\rmlui\round64-screens\round64_session_tourney_mapchoices_pushmenu_layout_final3.png`:
    visual evidence confirms fixed-list fallback rows remain readable inside
    bounded panels.
  - `.tmp\rmlui\round64-screens\round64_session_forfeit_confirm_pushmenu_layout_final.png`:
    visual evidence confirms popup presentation for the Forfeit confirmation.
- Known remaining gap: live session/list/keybind/player controllers, full
  keyboard/controller navigation parity, automated route-wide pixel clipping
  assertions, true narrow-viewport capture parity, native Vulkan/RTX-vkpt
  RmlUi rendering, and full visual parity remain pending.

## Purpose

Move WORR from the current cgame JSON/widget menu stack to RmlUi while keeping
the supported Quake II rerelease menu flows intact under the WORR banner. This
roadmap covers the menu shell, settings pages, browser/config tools, and
multiplayer/session menus. It does not treat the gameplay HUD or bootstrapper
UI as part of the first migration wave unless that work is explicitly added
later.

## Current Baseline

- Active runtime ownership is split across the client, cgame UI, and sgame menu
  command producers.
- The current data-driven menu surface is centered on
  `src/game/cgame/ui/worr.json` and `src/game/cgame/ui/worr-multiplayer.json`,
  loaded by `src/game/cgame/ui/ui_json.cpp`.
- The current named JSON menu footprint is `56` menus.
- Rich/code-driven menu surfaces still exist around:
  - `src/game/cgame/ui/ui_page_servers.cpp`
  - `src/game/cgame/ui/ui_page_demos.cpp`
  - `src/game/cgame/ui/ui_page_player.cpp`
  - `src/game/cgame/ui/ui_list.cpp`
- sgame match/menu code under `src/game/sgame/menu/*` still owns important
  state publication and menu-opening entry points for vote, tournament, MyMap,
  match info, and other multiplayer session flows.
- Legacy reference content still exists under `src/client/ui/` and
  `src/client/ui/worr.menu`. Even where it is no longer the primary runtime, it
  still needs an audit pass before final cleanup so old behavior is not lost by
  accident.

## Target End State

- RmlUi is the only document/layout runtime for WORR menus.
- Current JSON menu definitions and legacy `.menu` content are replaced by
  `.rml`, `.rcss`, templates, and RmlUi-backed controllers.
- Menu ownership is explicit: presentation lives in one runtime, while cgame
  and sgame expose data and commands through a narrow bridge.
- Rendering support is native across `rend_gl`, `rend_vk`, and `rend_rtx`.
  Vulkan and RTX paths must remain native and must not route through OpenGL.
- Current cvar bindings, commands, localized strings, and supported match/menu
  flows preserve parity unless a roadmap task explicitly calls for a redesign.
- Build/install workflows refresh `.install/` with the RmlUi documents,
  stylesheets, and supporting assets alongside current binaries.

## Architecture Recommendation

### Decision to close in `FR-09-T01`

- Recommended default: a client-owned RmlUi runtime with explicit cgame/sgame
  data-provider bridges.
- Fallback option: keep RmlUi inside cgame only if the T01 spike shows that the
  DLL boundary is materially cheaper than a client-owned runtime.

### Why the default is recommended

- Main menu, game menu, download UI, browsers, and match/session overlays all
  become part of one presentation runtime instead of being split by ownership
  accidents.
- Renderer integration stays close to the backend abstraction, which matters for
  native OpenGL, Vulkan, and RTX support.
- The migration can close `FR-03-T08` and advance `DV-04-T02` by turning
  today's mixed ownership into an explicit contract.

### Proposed repo layout

- `src/client/ui_rml/*`: context lifecycle, document loading, event dispatch,
  shared menu shell, and runtime services.
- `src/client/ui_rml_backends/*` or renderer-adjacent equivalents: RmlUi render
  bridge and backend-specific upload/draw helpers.
- `src/game/cgame/ui_rml/*`: cgame-backed data models for in-game and
  multiplayer/session menu state.
- `assets/ui/rml/*`: source `.rml`, `.rcss`, templates, sprites, and theme
  assets.
- `.install/basew/ui/rml/*`: staged runtime documents/assets refreshed by the
  install workflow.

## Migration Rules

- Do not build new JSON-only widgets unless they are short-lived blockers for
  the migration itself.
- Keep command names and cvar names stable wherever possible; migrate the
  presentation layer before changing gameplay-facing behavior.
- Preserve the current localization keys and dynamic text sources unless the
  migration exposes a clear defect or duplication issue.
- Prefer reusable templates, controllers, and data models over page-specific
  ad-hoc C++.
- Allow a short-lived dual-stack only as a migration safety net. Remove it
  after parity gates pass.
- Each significant slice lands with:
  - a focused implementation log under `docs-dev/`,
  - roadmap task updates in the canonical strategic doc,
  - `.install/` refresh validation when packaged assets change.

## Translation Mapping

| Current surface | RmlUi target | Notes |
|---|---|---|
| `action`, `toggle`, `switch` | shared button/checkbox components | Route through command and cvar controllers. |
| `range` | slider/number component | Preserve clamping, defaults, and live display formatting. |
| `values`, `strings`, `pairs` | shared select/dropdown/list components | Replace JSON-specific spin/pairs behavior with one controller family. |
| `field` | text/number input component | Keep width, numeric/integer filtering, and status hints. |
| `bind` | custom key-capture controller | Preserve existing key wait and binding conflict behavior. |
| `imagevalues` | image-grid component | Needed for crosshair and other visual selectors. |
| `episode_selector`, `unit_selector` | templated list/data-model views | Pull from existing mapdb-backed sources. |
| `savegame`, `loadgame` | save-slot component | Keep slot metadata, empty-state handling, and command wiring. |
| `servers`, `demos`, `ui_list` | reusable table/list controller | Support sort, paging, status rows, and scrolling. |
| player preview | hybrid RmlUi plus engine viewport surface | Requires explicit preview texture/render ownership. |

## Migration Waves

### Wave A: Shell and Settings

- [ ] `main`
- [ ] `game`
- [ ] `options`
- [ ] `video`
- [ ] `multimonitor`
- [ ] `performance`
- [ ] `accessibility`
- [ ] `sound`
- [ ] `railtrail`
- [ ] `effects`
- [ ] `crosshair`
- [ ] `screen`
- [ ] `language`
- [ ] `downloads`
- [ ] `download_status`
- [ ] `addressbook`
- [ ] `input`
- [ ] `keys`
- [ ] `legacykeys`
- [ ] `weapons`
- [ ] `quit_confirm`

### Wave B: Single-Player, Local Session, and Utility Tools

- [ ] `gameflags`
- [ ] `startserver`
- [ ] `multiplayer`
- [ ] `singleplayer`
- [ ] `skill_select`
- [ ] `loadgame`
- [ ] `savegame`
- [ ] `servers`
- [ ] `demos`
- [ ] `players`
- [ ] `ui_list`

### Wave C: Multiplayer and Match Session Flows

- [ ] `dm_welcome`
- [ ] `dm_join`
- [ ] `join`
- [ ] `dm_hostinfo`
- [ ] `dm_matchinfo`
- [ ] `callvote_main`
- [ ] `callvote_ruleset`
- [ ] `callvote_timelimit`
- [ ] `callvote_scorelimit`
- [ ] `callvote_unlagged`
- [ ] `callvote_random`
- [ ] `callvote_map_flags`
- [ ] `mymap_main`
- [ ] `mymap_flags`
- [ ] `forfeit_confirm`
- [ ] `leave_match_confirm`
- [ ] `admin_menu`
- [ ] `admin_commands`
- [ ] `tourney_info`
- [ ] `tourney_mapchoices`
- [ ] `tourney_veto`
- [ ] `tourney_replay_confirm`
- [ ] `vote_menu`
- [ ] `map_selector`
- [ ] `match_stats`

## Scrutiny Findings

- The previous serial milestone shape described the correct dependency order,
  but it made the work look serial. That would strand document translators
  behind renderer/bootstrap work even when they can use mocked data contracts.
- Runtime, renderer, data bridge, content translation, and validation ownership
  were mixed together inside milestones. Parallel AI agents need path ownership
  and handoff artifacts, not just broad phase names.
- Wave A/B/C are useful content buckets, but they are not enough to prevent
  merge conflicts. The refactor below assigns both code paths and asset paths
  to specific agents.
- Validation was too late. The smoke harness and parity checklists must start
  during the first synchronization pass so each agent can add evidence as its
  slice lands.
- Legacy removal must stay gated, but the deletion inventory can be prepared in
  parallel with migration work.

## Five-Agent Parallel Workload

Run the migration as five long-lived AI subagents. Each agent owns a lane, a
small set of repository paths, and a repeated evidence loop. The lanes are
allowed to work in parallel after the initial contract synchronization pass.

### Parallel Contract Rules

- All agents use the task IDs listed in this document and reference them in
  implementation logs under `docs-dev/`.
- All temporary probes, generated inventories, and mock data live under
  `.tmp/rmlui/`.
- Agent-owned source paths are exclusive by default. Shared files require a
  short coordination note in the implementation log before edit.
- Content agents may author `.rml` and `.rcss` against mock data models before
  the runtime is fully integrated.
- Runtime and component agents must keep the mock contracts runnable until the
  real bridge is ready.
- Every agent adds or updates a validation note with build status, document-load
  status, and known parity gaps before handing off.

### Synchronization Pass S0: Contract Freeze

Tasks: `FR-09-T01`, `FR-03-T08`, `DV-04-T02`, `DV-03-T07`

This is the only intentionally shared startup pass. It should be short and
should produce contracts that unblock all five agents.

- [ ] Inventory active UI entry points:
  - `UI_OpenMenu(...)`
  - `pushmenu`
  - download auto-open/auto-close behavior
  - `menu_loadgame`
  - escape-key routing
  - sgame-originated menu commands
- [ ] Inventory active menu assets, page classes, list controllers, and legacy
  references.
- [ ] Freeze the runtime owner decision and the cgame/sgame data boundary.
- [ ] Freeze the RmlUi route namespace, document IDs, data-model names, command
  event names, and cvar binding schema.
- [ ] Freeze the source asset layout and `.install/` staging layout.
- [ ] Create mock data files under `.tmp/rmlui/contracts/` for settings,
  browsers, player preview, save/load, and multiplayer/session flows.
- [ ] Create the initial smoke-harness manifest listing every Wave A, B, and C
  document, even before all documents exist.
- [ ] Write the JSON-to-RmlUi translation rules for:
  - `showIf`
  - `enableIf`
  - `labelCvar`
  - cvar-backed value widgets
  - feeder/list pages

S0 exit gate:
- One approved ownership and asset-staging plan.
- One route/data/command contract that all agents can build against.
- One document manifest that maps every current menu surface to an owning agent.

### Menu Ownership Map

Agent 4 owns shell, settings, and single-player content:
- Wave A: `main`, `game`, `options`, `video`, `multimonitor`,
  `performance`, `accessibility`, `sound`, `railtrail`, `effects`,
  `crosshair`, `screen`, `language`, `downloads`, `download_status`, `input`,
  and `quit_confirm`.
- Wave B: `gameflags`, `startserver`, `singleplayer`, `skill_select`,
  `loadgame`, and `savegame`.

Agent 5 owns rich utility, multiplayer, and session content:
- Wave A utility/keybind surfaces: `addressbook`, `keys`, `legacykeys`, and
  `weapons`.
- Wave B utility/multiplayer surfaces: `multiplayer`, `servers`, `demos`,
  `players`, and `ui_list`.
- Wave C: all multiplayer and match-session flows.

### Agent 1: Platform, Runtime, and Packaging

Primary tasks: `FR-09-T01`, `FR-09-T02`, `FR-09-T03`, `DV-06-T01`

Owned paths:
- `src/client/ui_rml/core/*`
- `src/client/ui_rml/runtime/*`
- `subprojects/*` and dependency wrap files for RmlUi
- relevant Meson build files
- `tools/refresh_install.py`
- `.install/basew/ui/rml/*` staging behavior

Work packets:
- [ ] Add RmlUi as a first-class dependency with an intentional
  Meson/vendoring strategy.
- [ ] Implement RmlUi system and file interfaces using WORR time, logging,
  filesystem, and localization lookup hooks.
- [ ] Implement context lifecycle, document loading, route dispatch, menu
  open/close state, and escape/back semantics.
- [ ] Provide a sample document route that can open from the normal WORR menu
  entry points.
- [ ] Add build/install rules so `.install/` always includes current RmlUi
  documents, styles, fonts, and supporting assets.
- [ ] Keep a mock-runtime mode available for content agents until the real data
  bridge is ready.

Agent 1 exit gate:
- A sample `.rml` document opens through the normal menu path.
- Build and install refresh place RmlUi assets in `.install/basew/ui/rml/`.
- Runtime contracts are stable enough for agents 2 through 5 to integrate
  without editing bootstrap code.

### Agent 2: Native Renderer, Input, Theme, and Accessibility

Primary tasks: `FR-09-T03`, `FR-09-T04`, `DV-07-T02`, `DV-07-T04`

Owned paths:
- `src/client/ui_rml/render/*`
- `src/client/ui_rml/input/*`
- renderer-adjacent RmlUi bridge files in `rend_gl`, `rend_vk`, and RTX/vkpt
  code
- `assets/ui/rml/common/theme/*`
- `assets/ui/rml/common/fonts/*`

Work packets:
- [ ] Implement the RmlUi render bridge for OpenGL, Vulkan, and RTX/vkpt.
- [ ] Keep Vulkan and RTX rendering native; do not add an OpenGL fallback path
  for any Vulkan renderer route.
- [ ] Decide whether to use the default RmlUi font engine or a WORR-specific
  font bridge, and document the reason.
- [ ] Hook cursor rendering, focus changes, text input, pointer capture, menu
  sounds, and keyboard/gamepad navigation affordances.
- [ ] Define the base theme tokens, high-visibility policy, readable text
  defaults, and long-string overflow behavior.
- [ ] Add renderer-specific smoke cases for the sample document and at least
  one scrolling/content-heavy document.

Agent 2 exit gate:
- OpenGL, Vulkan, and RTX/vkpt can all draw the sample document natively.
- Font, input, cursor, audio, and accessibility behavior is available through
  stable runtime services.
- Theme assets can be consumed by all content agents without local copies.

### Agent 3: Data Models, Controllers, and Shared Components

Primary tasks: `FR-09-T05`, `FR-09-T07`, `FR-03-T08`, `DV-04-T02`

Owned paths:
- `src/client/ui_rml/controllers/*`
- `src/client/ui_rml/components/*`
- `src/game/cgame/ui_rml/*`
- shared component templates under `assets/ui/rml/common/components/*`

Work packets:
- [ ] Build a shared cvar binding layer for bool, integer, float, enum, and
  string-backed controls.
- [ ] Build the command/event dispatch layer that replaces current `command`
  and `commandCvar` behavior.
- [ ] Recreate current condition handling for visibility and enabled-state
  changes.
- [ ] Bind localized strings and dynamic cvar-backed labels into the document
  model.
- [ ] Build shared components for:
  - toggle/checkbox
  - slider
  - select/dropdown
  - image-grid selector
  - keybind capture
  - save/load slot
  - shared list/table
- [ ] Define the player preview ownership model and expose it as a reusable
  component contract.
- [ ] Maintain mock data adapters that let Agents 4 and 5 validate content
  before the live bridge is complete.

Agent 3 exit gate:
- Current JSON widget types have documented and implemented RmlUi replacement
  paths.
- One simple settings page, one list/table page, and one preview page run
  through shared components.
- Content translation is mostly `.rml`/`.rcss` work instead of one-off C++.

### Agent 4: Shell, Settings, and Single-Player Content

Primary tasks: `FR-09-T06`, `FR-09-T07`, `FR-09-T04`, `FR-09-T09`

Owned paths:
- `assets/ui/rml/shell/*`
- `assets/ui/rml/settings/*`
- `assets/ui/rml/singleplayer/*`
- shell/settings parity notes under `docs-dev/`

Work packets:
- [ ] Translate the Agent 4-owned Wave A menu set into `.rml` and `.rcss`.
- [ ] Translate the single-player and local-session subset of Wave B:
  `singleplayer`, `skill_select`, `gameflags`, `startserver`, `loadgame`, and
  `savegame`.
- [ ] Preserve the current menu hierarchy, main-menu/in-game-menu distinction,
  and escape/back behavior.
- [ ] Preserve all settings cvar writes and command routes unless another task
  explicitly redesigns them.
- [ ] Carry over accessibility and high-visibility controls instead of dropping
  them during the visual rewrite.
- [ ] Audit shell/settings flows against `src/game/cgame/ui/worr.json` and the
  legacy `src/client/ui/worr.menu` reference content.
- [ ] Add smoke manifest coverage for every shell/settings/single-player route.

Agent 4 exit gate:
- Main menu, game menu, settings, and single-player/local-session flows run
  through RmlUi.
- No shell/settings/single-player flow requires the JSON runtime.
- Settings persistence and long-string layout parity are validated.

### Agent 5: Rich Tools, Multiplayer Session Flows, Validation, and Cutover

Primary tasks: `FR-09-T07`, `FR-09-T08`, `FR-09-T09`, `FR-09-T10`,
`FR-07-T01`, `FR-07-T02`, `DV-03-T07`

Owned paths:
- `assets/ui/rml/utility/*`
- `assets/ui/rml/multiplayer/*`
- `assets/ui/rml/session/*`
- `tools/ui_smoke/*`
- legacy-removal inventory under `.tmp/rmlui/legacy-removal/`
- multiplayer/session implementation logs under `docs-dev/`

Work packets:
- [ ] Translate the Agent 5-owned Wave A utility/keybind surfaces:
  `addressbook`, `keys`, `legacykeys`, and `weapons`.
- [ ] Translate the Agent 5-owned Wave B utility/multiplayer surfaces:
  `multiplayer`, `servers`, `demos`, `players`, and `ui_list`.
- [ ] Translate `servers` with sorting, refresh, connect, and status feedback.
- [ ] Translate `demos` with directory navigation, cache behavior, and sorting.
- [ ] Translate `players` with model/skin/weapon preview and current bind/icon
  behavior.
- [ ] Translate other rich utility surfaces that depend on table/list/keybind
  controllers.
- [ ] Translate the Wave C menu set into RmlUi documents.
- [ ] Keep sgame/cgame data publication narrow and explicit for tournament,
  vote, MyMap, forfeit, replay, map selector, match info, and match stats
  flows.
- [ ] Preserve the current session-only routing split between core menus and
  multiplayer-only menus.
- [ ] Build the automated UI smoke harness for document load success,
  navigation/open-close flows, screenshot/layout capture, renderer-specific
  coverage, and match-state transitions.
- [ ] Prepare the legacy JSON loader/widget removal inventory while waiting for
  parity gates; perform deletion only after all agent exit gates pass.
- [ ] Update developer and user docs for the final RmlUi path and any
  user-visible workflow changes.

Agent 5 exit gate:
- Browser/config utility surfaces and multiplayer/session flows run through
  RmlUi with current supported behavior intact.
- Automated UI smoke coverage exists for document loading, navigation,
  renderer-specific rendering, and match/session transitions.
- Legacy JSON removal is either complete or intentionally archived with a
  documented reason.

## Integration Gates

These gates replace the old serial milestones. Agents can work in parallel, but
merge only when the relevant gate is green.

### Gate G0: Contracts Ready

Required from S0:
- Runtime owner, route namespace, data model schema, command events, asset
  layout, and install layout are frozen.
- Every menu in Wave A, B, and C has an owning agent.
- Smoke manifest exists even if most documents are placeholders.

### Gate G1: Vertical Runtime Proof

Required from Agents 1, 2, and 3:
- Sample document opens from a normal menu entry point.
- OpenGL, Vulkan, and RTX/vkpt draw the sample through native paths.
- One cvar-backed control, one command button, and one conditional element work
  against the real runtime.
- `.install/` refresh stages the sample assets.

### Gate G2: Component Parity Proof

Required from Agents 2, 3, 4, and 5:
- Shared components cover all current JSON widget families.
- One settings page, one list/table page, and one preview page pass manual and
  smoke validation.
- Theme, font, input, cursor, and accessibility behavior are stable across
  content lanes.

### Gate G3: Surface Cutover

Required from Agents 4 and 5:
- Wave A, Wave B, and Wave C documents all open through RmlUi.
- Settings persistence, browser behavior, demo behavior, player preview,
  save/load, and multiplayer/session transitions pass parity checks.
- JSON routing is disabled for migrated flows except for explicitly documented
  fallback routes.

### Gate G4: Legacy Removal

Required from all agents:
- UI smoke automation passes across the selected renderer matrix.
- Manual layout checks pass for 4:3, 16:9, ultrawide, high-visibility text,
  localization stress, mouse, keyboard, and text entry.
- Legacy JSON menu loading/widgets and dead menu assets are removed, or an
  explicit archive reason is recorded.
- The canonical strategic roadmap marks the corresponding tasks complete.

## Validation Matrix

- Build validation:
  - client/cgame compile with the new dependency path
  - `.install/` refresh includes the new UI assets
- Runtime validation:
  - menu open/close
  - main menu to in-game menu transitions
  - shell/settings persistence
  - server/demo/player utility pages
  - multiplayer session transitions
- Renderer validation:
  - OpenGL
  - Vulkan
  - RTX
- Layout validation:
  - 4:3
  - 16:9
  - ultrawide
  - high-visibility text mode
- Content validation:
  - localization length stress
  - dynamic cvar-bound labels
  - conditionally shown controls
  - player preview and image-grid selectors

## Risks and Mitigations

- Risk: ownership drift between client, cgame, and sgame survives the migration.
  Mitigation: close T01 early, and treat any new cross-boundary call as a
  design review point.
- Risk: renderer support lands first in OpenGL and lags behind Vulkan/RTX.
  Mitigation: require Gate G1 native renderer proof before page translation
  starts in earnest.
- Risk: document translation outruns shared component maturity, creating many
  one-off controllers.
  Mitigation: require Gate G2 shared component parity before broad page
  migration.
- Risk: list/browser pages become the hidden schedule sink.
  Mitigation: treat `servers`, `demos`, `players`, and `ui_list` as explicit
  Agent 5 deliverables rather than late polish.
- Risk: old JSON or legacy `.menu` behavior gets lost because it is no longer in
  the active runtime.
  Mitigation: keep the legacy audit checklist in S0, Agents 4 and 5, and Gate
  G4; reference both active JSON menus and legacy menu sources before deletion.

## Definition of Done

- All Wave A, B, and C menu surfaces run through RmlUi.
- Current supported flows keep functional parity or have a documented,
  intentional redesign.
- Renderer support remains native across all supported backends.
- `.install/` staging and runtime loading use the new asset path.
- Legacy JSON menu loading/widgets are removed or intentionally archived.
- The canonical strategic roadmap marks the corresponding tasks complete.
