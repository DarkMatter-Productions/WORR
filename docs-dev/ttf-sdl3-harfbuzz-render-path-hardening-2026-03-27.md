# SDL3_ttf + HarfBuzz Render Path Hardening (2026-03-27)

Task ID: `FR-06-T03`

## Summary
- Fixed a drop-on-failure bug in the TTF draw path where a failed SDL3_ttf text object build would skip rendering entirely instead of falling back to existing glyph-by-glyph rendering.
- Hardened glyph-cache generation for HarfBuzz-shaped glyph indices by no longer requiring `TTF_GetGlyphMetrics(...)` to succeed before a glyph can be considered renderable.
- Added guardrails so TTF mode is only marked ready when both SDL3_ttf and the SDL3_ttf surface text engine are actually initialized.
- Hardened measurement so failed SDL3_ttf width queries no longer produce silent zero-width segments.

## Problem Statement
The current TTF path depended on successful creation of `TTF_Text` and successful metrics queries for every shaped glyph. In practice, this introduced two failure modes that could produce an apparent "no font rendered" result:

1. **Segment drop on shaping object failure**
   - `Font_DrawString(...)` unconditionally `continue`d after entering the SDL3_ttf branch, even when `TTF_CreateText(...)` failed.
   - That meant no draw work and no fallback draw happened for the segment.

2. **Cache population dependent on metrics call**
   - Glyph caching used HarfBuzz-shaped glyph indices (from SDL3_ttf text draw ops).
   - If `TTF_GetGlyphMetrics(...)` failed for that index, glyph cache generation aborted early for that glyph, even when `TTF_GetGlyphImageForIndex(...)` could still provide raster data.

## Implementation Details
### 1) Draw-path fallback recovery
- Added a local `drew_ttf_segment` guard in `Font_DrawString(...)`.
- The function now only short-circuits (`continue`) when a segment was actually emitted through SDL3_ttf draw operations.
- If SDL3_ttf text object creation fails, the code now naturally falls back to the existing glyph-by-glyph rendering path (kfont/legacy fallback chain), preserving visible output instead of dropping text.

### 2) Glyph cache robustness for HarfBuzz indices
- Updated `font_ttf_render_bitmap(...)` to treat metrics as optional:
  - Keep attempting `TTF_GetGlyphMetrics(...)`.
  - Do **not** early-return on metrics failure.
  - If `TTF_GetGlyphImageForIndex(...)` succeeds, mark glyph as valid and cache bitmap data.
- Added conservative advance fallback (`surface->w`) when metrics are unavailable, so fixed-size fallback behavior remains stable.

This aligns the cache behavior with SDL3_ttf + HarfBuzz shaping output by prioritizing the shaped glyph image path used for actual rendering.

### 3) Initialization safety improvements
- Updated `Font_Init(...)` to require successful creation of `TTF_CreateSurfaceTextEngine()` before enabling TTF mode.
- If the engine object cannot be created, TTF mode remains disabled and emits a warning.

### 4) Measurement safety
- Updated `Font_MeasureString(...)` so the SDL3_ttf segment fast path only applies when `TTF_GetStringSize(...)` succeeds.
- On failure, control now falls through to the existing per-codepoint fallback measurement path.

## Why this better utilizes SDL3_ttf + HarfBuzz
- The render path now consistently uses SDL3_ttf’s shaped draw operations when available.
- Failures in shaping/metrics no longer zero out rendering; fallback rendering remains active.
- HarfBuzz-shaped glyph indices now remain renderable as long as SDL3_ttf can produce glyph image data, reducing false-negative cache misses.

## Files Updated
- `src/client/font.cpp`
- `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`
