# RmlUi Inactive Vulkan Stub Build-Warning Cleanup

Date: 2026-07-10

Tasks: `FR-09-T03`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`

## Summary

The inactive native Vulkan and RTX/vkpt RmlUi render-interface placeholders
were declared `final` while still inheriting pure virtual methods from
`Rml::RenderInterface`. Clang therefore emitted `-Wabstract-final-class` once
the RmlUi dependency was enabled for those renderer targets.

The placeholders are now explicitly non-final abstract classes. This keeps the
bridge-readiness class markers in place, accurately represents that native
methods remain unimplemented, and removes the warnings without activating or
redirecting either renderer path.

## Validation

- Rebuilt the Windows Meson/Ninja target set in `builddir-win`.
- Confirmed both `src/renderer/rmlui_bridge.cpp` renderer compilations complete
  without `-Wabstract-final-class` or other warnings.
- Ran the focused RmlUi Vulkan bridge-readiness tests.
- Refreshed and validated `.install/` from the completed build.
