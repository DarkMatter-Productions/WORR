# RmlUi Agent 2 Build Option Round 15

Date: 2026-07-02

Task IDs: `FR-09-T02`, `FR-09-T03`, `DV-06-T01`, `DV-03-T07`

## Summary

Round 15 adds a conservative Meson build option for the RmlUi dependency
without making RmlUi part of the default WORR build. The option is named
`rmlui`, uses Meson's `feature` type, and defaults to `disabled`.

The existing `src/client/ui_rml/ui_rml.cpp` scaffold already defaults
`UI_RML_HAS_RUNTIME` to `0`. Meson now defines `UI_RML_HAS_RUNTIME=1` only
when the `rmlui` option is allowed and a real dependency is found.

## Build Option Behavior

- `-Drmlui=disabled` is the default. Meson does not probe RmlUi, no dependency
  is linked, and the scaffold keeps its current stub behavior.
- `-Drmlui=auto` probes optional RmlUi dependencies. If no dependency is found,
  the build continues with the stub behavior.
- `-Drmlui=enabled` requires a dependency. Meson errors if it cannot find either
  the CMake package `RmlUi` with target `RmlUi::RmlUi` or a pkg-config style
  `rmlui` dependency.

No subproject wrap files were edited in this slice. Worker 1 still owns the
wrap/source decision and provenance work.

## Runtime Guard

When RmlUi is found, Meson appends the dependency to `client_deps` and adds:

```text
-DUI_RML_HAS_RUNTIME=1
```

When RmlUi is absent or disabled, Meson leaves that define unset. The scaffold's
local fallback keeps `UI_RML_HAS_RUNTIME` at `0`, so menu open requests continue
to probe documents and return to the legacy UI path.

## Validation

Commands run for this slice:

```text
meson --version
meson introspect builddir-win --buildoptions
meson setup builddir-win --reconfigure
$opts = meson introspect builddir-win --buildoptions | ConvertFrom-Json; $opts | Where-Object { $_.name -eq 'rmlui' } | ConvertTo-Json -Compress
ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj -v
meson setup builddir-win --reconfigure -Drmlui=auto
meson setup builddir-win --reconfigure -Drmlui=disabled
$opts = meson introspect builddir-win --buildoptions | ConvertFrom-Json; $opts | Where-Object { $_.name -eq 'rmlui' } | ConvertTo-Json -Compress
ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj -v
```

Observed results:

- Meson 1.9.1 accepted the reconfigure.
- The feature summary reported `rmlui: NO` for the default build.
- Build-option introspection reported `rmlui` as a user option with value
  `disabled` and choices `enabled`, `disabled`, and `auto`.
- The temporary `-Drmlui=auto` reconfigure executed the optional dependency
  probes, found no local RmlUi dependency, and continued with `rmlui: NO`.
- The build directory was restored to `-Drmlui=disabled` after the auto probe.
- The focused `src/client/ui_rml/ui_rml.cpp` object compile passed. The emitted
  compile command did not include `-DUI_RML_HAS_RUNTIME=1`, confirming that the
  default-disabled configuration keeps the runtime stub path.

The option is intentionally default-disabled until `FR-09-T02` selects the
accepted source/wrap and `FR-09-T03` proves native RmlUi runtime rendering
without a Vulkan-to-OpenGL fallback.
