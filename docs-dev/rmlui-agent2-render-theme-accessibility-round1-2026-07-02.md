# RmlUi Agent 2 Theme and Accessibility Round 1 (2026-07-02)

Task IDs: `FR-09-T03`, `FR-09-T04`, `DV-07-T02`, `DV-07-T04`

## Summary

This round adds the first shared RmlUi theme/accessibility contract assets for
WORR. It intentionally does not add renderer C++ yet. The new assets give
future RmlUi documents a single base stylesheet to import for readable defaults,
keyboard focus styling, high-visibility hooks, and long-string handling.

## Files Changed

- `assets/ui/rml/common/theme/base.rcss`
  - Defines shared color, spacing, radius, font-family, control, focus, and
    high-visibility variables.
  - Adds default document, text, panel, button, field, scroll, wrap, truncate,
    and readable-backplate classes.
  - Uses `:focus-visible`, `tab-index: auto`, and `nav: auto` hooks so the
    input bridge can expose keyboard/gamepad navigation without page-local
    focus styles.

- `assets/ui/rml/common/fonts/README.md`
  - Records the pending font staging decision without adding binary fonts.
  - Reserves the family names `WORR UI` and `WORR Mono` for the future font
    service.

## Renderer and Input Assumptions

- RmlUi documents should import the stylesheet once from their document head.
  Preferred UI-root href after the file interface is finalized:
  `<link type="text/rcss" href="common/theme/base.rcss" />`. Documents in
  subdirectories may need route-relative paths until the resolver contract is
  closed.
- The eventual native renderer bridge must draw these base rules in OpenGL,
  Vulkan, and RTX/vkpt. Vulkan and RTX/vkpt work must stay native and must not
  be redirected through OpenGL fallback paths.
- The base stylesheet avoids renderer-heavy assumptions such as image
  decorators, shader decorators, filters, and required box shadows. Focus is
  visible through reserved borders and colors so it can validate before
  advanced renderer features are implemented.
- Input integration is expected to map RmlUi focus state, `:focus-visible`,
  `tab-index: auto`, and `nav: auto` into mouse, keyboard, text-input, and
  gamepad navigation services.
- Cursor names are declared only as generic RCSS hooks for now. The C++ input
  and cursor service still owns the final mapping.

## Theme and Accessibility Contract

- Default text uses a readable 18px body size, 1.28 line height, normal kerning,
  and high-contrast foreground/background colors.
- `.ui-high-visibility` can be applied to the `body` or a subtree to switch to
  black surfaces, white text, yellow focus/accent, and stronger contrast.
- `.ui-readable-backplate` provides a shared hook for text that needs a solid
  dark backing over noisy scenes or previews.
- `.ui-wrap`, `.ui-break-all`, `.ui-truncate`, `.ui-scroll`, and `.ui-code`
  define the first shared long-string and overflow policy for content agents.
- The theme keeps visual controls compact and low-radius so menus remain
  practical for repeated use instead of becoming a marketing-style page.

## Validation

- Verified the new scoped files are present with `rg --files assets/ui/rml`.
- Verified the task IDs appear in this implementation log with
  `rg -n "FR-09-T03|FR-09-T04|DV-07-T02|DV-07-T04" docs-dev/rmlui-agent2-render-theme-accessibility-round1-2026-07-02.md`.
- No build was run because this round only adds source assets and documentation;
  RmlUi is not wired into the build/runtime path yet.

## Next Handoff Notes

- Agent 1 should ensure build/install staging copies `assets/ui/rml/common/`
  into `.install/basew/ui/rml/common/` when `FR-09-T02` lands.
- Agent 2 follow-up should implement the native renderer bridge and keep Vulkan
  and RTX/vkpt draw paths independent from OpenGL.
- Agent 2 follow-up should close the font decision: stock RmlUi font engine vs.
  a WORR-specific font bridge, selected TTF assets, license record, fallback
  order, and install staging.
- Agent 3 and Agent 4 can consume `base.rcss` for early `.rml` documents and
  should use the shared `.ui-*` classes instead of cloning per-page theme rules.
- Agent 5 should include high-visibility, focus-visible navigation, scrolling,
  truncation, and long localized strings in the RmlUi smoke/parity matrix.
