# RmlUi Agent 2 Theme and Accessibility Round 2 (2026-07-02)

Task IDs: `FR-09-T04`, `FR-09-T03`, `DV-07-T02`, `DV-07-T04`

## Summary

This round expands the shared RmlUi theme and accessibility contract for utility
and session surfaces. It does not edit existing RML documents and does not add
renderer C++ yet. The new RCSS files give later route imports stable hooks for
table browsers, toolbar utilities, preview/forms, votes, match/admin flows,
high-visibility testing, reduced-motion testing, long strings, and focus QA.

## Files Changed

- `assets/ui/rml/common/theme/utility.rcss`
  - Adds shared page scaffolds for server/demo/player utility routes.
  - Defines toolbar, table, sortable-header, placeholder-row, status-bar,
    two-column form, and preview-surface rules.
  - Keeps table and preview layouts renderer-neutral and long-string safe.

- `assets/ui/rml/common/theme/session.rcss`
  - Adds shared scaffolds for multiplayer session routes, vote prompts, match
    summaries, admin panels, command strips, player/admin rows, status badges,
    and flow-step states.
  - Covers the current vote menu and multiplayer hub class names while also
    adding forward-looking `session-*` hooks for future RML documents.

- `assets/ui/rml/common/theme/accessibility.rcss`
  - Adds opt-in hooks for `ui-a11y-large-text`, `ui-a11y-high-visibility`,
    `ui-a11y-reduced-motion`, `ui-a11y-long-string`,
    `ui-a11y-readable-overlay`, and focus-test/debug classes.
  - Keeps compatibility aliases for the first-round `.ui-high-visibility` and
    `.ui-reduced-motion` naming.

- `assets/ui/rml/common/theme/README.md`
  - Documents the preferred import order: base, optional component RCSS, one
    page-family theme, then accessibility overrides last.
  - Records pending runtime, input, font, localization, and renderer work.

## Renderer, Input, and Accessibility Guardrails

- The native renderer bridge must draw the same RmlUi rules through OpenGL,
  Vulkan, and RTX/vkpt. Vulkan renderer work must stay native; do not redirect
  Vulkan or RTX/vkpt behavior through OpenGL fallback paths.
- These styles avoid binary font assets, custom shader effects, filters, image
  decorators, and backend-specific assumptions so they can validate before the
  full native renderer bridge lands.
- Input integration still owns mouse, keyboard, text input, controller focus
  navigation, cursor mapping, focus-visible behavior, and repeat policy.
- Accessibility classes are contractual hooks. Runtime settings or smoke tests
  can apply them to `body` or scoped route roots once the bridge exists.
- No end-user documentation was added because this round changes source theme
  contracts only; there are no user-visible cvars or runtime settings yet.

## Validation

- `git status --short -- <owned files>` shows only the five Agent 2-owned
  round-2 files as changed in this lane.
- ASCII scan passed for all five new files.
- Trailing-whitespace scan passed for all five new files.
- `rg -n "FR-09-T04|FR-09-T03|DV-07-T02|DV-07-T04"` confirms the task IDs are
  present in this implementation log and the theme README.
- `rg` selector checks confirmed the expected utility, session, and
  accessibility hooks are present.
