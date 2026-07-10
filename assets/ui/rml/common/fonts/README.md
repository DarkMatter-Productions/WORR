# WORR RmlUi Font Staging

No binary fonts are staged in this first-round asset contract.

Expected runtime contract:
- The RmlUi font service registers the display family as `WORR Display`.
- The primary readable UI family is registered as `WORR UI`.
- The optional monospace/debug family is registered as `WORR Mono`.
- Runtime default faces should prefer Quake II Rerelease fonts from
  `Q2Game.kpf`, including `fonts/RussoOne-Regular.ttf`,
  `fonts/Montserrat-Regular.ttf`, `fonts/NotoSansKR-Regular.otf`, and
  `fonts/RobotoMono-Regular.ttf`, before any platform fallback.
- `assets/ui/rml/common/theme/base.rcss` refers to those family names through
  RCSS variables so the final implementation can switch between the stock
  RmlUi font engine and a WORR-specific font bridge without rewriting content.
- Font fallback order is owned by `src/client/ui_rml/ui_rml_runtime.cpp`.
  Binary fonts remain sourced from the Quake II Rerelease install/search path
  rather than copied into this directory.

Notes for future work:
- Do not add binary fonts here without recording the source, license, and
  install-path decision in `docs-dev/`.
- RmlUi requires TTF faces to be loaded through the C++ interfaces before RCSS
  can reference the family names.
