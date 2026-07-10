# WORR RmlUi Theme Contract

Task IDs: `FR-09-T04`, `FR-09-T03`, `DV-07-T02`, `DV-07-T04`

This directory contains shared RCSS contracts for the staged WORR RmlUi
migration. The files are source assets only: no binary fonts, renderer C++, or
runtime bridge code lives here.

## Import Order

Use this order when a document or route imports theme files:

1. `common/theme/base.rcss`
2. Optional component RCSS files, such as `common/components/list-table.rcss`
3. One page-family theme, such as `common/theme/utility.rcss` or
   `common/theme/session.rcss`
4. `common/theme/accessibility.rcss`

`accessibility.rcss` should be imported last so high-visibility, reduced-motion,
long-string, and focus-test hooks can override page and component defaults.
Subdirectory routes may need relative paths such as `../common/theme/base.rcss`
until the runtime file interface supports stable UI-root paths.

## File Roles

- `base.rcss`: shared colors, spacing, typography, panels, controls, focus, and
  generic long-string helpers.
- `shell.rcss`, `settings.rcss`, `singleplayer.rcss`: first-round route family
  scaffolds.
- `utility.rcss`: table/browser toolbar layouts, status bars, two-column
  form/preview pages, and preview surface hooks.
- `session.rcss`: vote, match, multiplayer session, admin, status, and command
  flow scaffolds.
- `accessibility.rcss`: opt-in hooks for high visibility, reduced motion, large
  text, long localized strings, readable preview overlays, and focus testing.

## Pending Runtime and Renderer Work

- The RmlUi bootstrap still needs native renderer, input, file, font,
  localization, cursor, and audio bridge work before these styles are visible in
  the game.
- The native renderer bridge must support these rules in OpenGL, Vulkan, and
  RTX/vkpt paths. Vulkan and RTX/vkpt work must remain native; do not route
  Vulkan renderer behavior through OpenGL fallback paths.
- The input bridge still owns mouse, keyboard, text input, gamepad navigation,
  cursor mapping, focus-visible behavior, and controller repeat policy.
- Font files are intentionally absent from the RmlUi source tree. `base.rcss`
  reserves `WORR Display`, `WORR UI`, and `WORR Mono`; the runtime resolves
  those families from Quake II Rerelease fonts on the existing filesystem
  search path before any platform fallback.
- Accessibility classes are contractual hooks. Runtime cvars or settings can
  later apply them to `body` or scoped route roots without requiring page-local
  rewrites.

## Authoring Notes

- Keep page themes renderer-neutral. Prefer colors, borders, spacing, and text
  wrapping over shader decorators, filters, or backend-specific effects.
- Use lowercase `ui-*`, `utility-*`, and `session-*` classes for shared hooks.
- Keep long strings safe by default on data-driven labels, paths, player names,
  server names, and localized copy.
- Place engineering changes under `docs-dev/`; end-user docs belong under
  `docs-user/`.
