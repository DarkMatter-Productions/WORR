# FR-06-T03 Accessibility Defaults and Fallback Font Controls (2026-03-27)

Task ID: `FR-06-T03`

## Summary
- Enabled text background readability mode by default via `cl_font_draw_black_background 1`.
- Enabled cgame accessibility contrast bars by default via `ui_acc_contrast 1`.
- Added archived fallback font-path cvars so fallback font behavior can be tuned without code changes:
  - `cl_font_fallback_kfont` (default `fonts/qfont.kfont`)
  - `cl_font_fallback_legacy` (default `conchars.png`)

## Motivation
`FR-06-T03` calls for a practical accessibility pass that covers text backgrounds, contrast defaults, and fallback fonts. The engine already had these systems in place, but default posture and runtime configurability still needed tightening so accessibility behavior is available out-of-the-box and can be adjusted by users/modders without rebuilding.

## Implementation Details

### 1) Readability defaults
- `src/client/font.cpp`
  - Changed `cl_font_draw_black_background` default from `0` to `1`.
  - This keeps text background bars enabled unless a user explicitly disables them.

- `src/game/cgame/cg_draw.cpp`
  - Changed `ui_acc_contrast` default from `0` to `1`.
  - Notify and centerprint high-contrast bars are now enabled by default.

### 2) Fallback font controls
- `src/client/font.cpp`
  - Added two archived cvars:
    - `cl_font_fallback_kfont`
    - `cl_font_fallback_legacy`
  - `font_load_internal(...)` now uses these cvars when fallback paths are omitted.
  - `Font_Init(...)` proactively registers the cvars during font subsystem startup.

## Result
- Accessibility-oriented text backgrounds and contrast bars are now default-on.
- Fallback font paths are configurable from cvars while preserving current defaults.
- `FR-06-T03` now has explicit implementation coverage for:
  - text backgrounds,
  - contrast defaults,
  - fallback fonts,
  - and previously landed scaling/hardening work.

## Files Updated
- `src/client/font.cpp`
- `src/game/cgame/cg_draw.cpp`
- `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`
