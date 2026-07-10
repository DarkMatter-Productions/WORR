# RmlUi Round 15 Agent 1 Dependency Source Audit

Date: 2026-07-02

Task IDs: `DV-06-T01`, `FR-09-T02`, `FR-09-T03`

## Scope

This slice pins the first upstream RmlUi source acquisition record for WORR
without changing build wiring. Worker 2 owns Meson target wiring, so this pass
does not edit `meson.build` or `meson_options.txt`.

Owned changes:

- `subprojects/rmlui.wrap`
- `docs-dev/rmlui-agent1-dependency-source-round15-2026-07-02.md`

Scratch evidence was written under `.tmp/rmlui/round15-dependency-source/`.

## Upstream Source

- Upstream repository: `https://github.com/mikke89/RmlUi`
- Selected release/tag: `6.2`
- Tag commit observed with `git ls-remote --tags`: `2230d1a6e8e0848ed87a5761e2a5160b2a175ba4`
- Archive URL: `https://github.com/mikke89/RmlUi/archive/refs/tags/6.2.tar.gz`
- Archive filename used by the wrap: `RmlUi-6.2.tar.gz`
- Archive root directory: `RmlUi-6.2`
- Archive SHA-256: `814c3ff7b9666280338d8f0dda85979f5daf028d01c85fc8975431d1e2fd8e8b`

The hash was computed from the exact archive URL above after downloading it
with `Invoke-WebRequest` and checking it with `Get-FileHash -Algorithm SHA256`.

## License And Provenance Findings

- Root license: MIT, from upstream `LICENSE.txt`.
- Upstream README states RmlUi is a C++ UI package forked from libRocket and
  published under the MIT license.
- Core required dependency noted by upstream README: FreeType, unless WORR
  supplies a custom font engine.
- Optional upstream CMake features add optional dependencies:
  - `RMLUI_LUA_BINDINGS`: Lua or LuaJIT.
  - `RMLUI_LOTTIE_PLUGIN`: `rlottie`.
  - `RMLUI_SVG_PLUGIN`: `lunasvg`.
  - `RMLUI_HARFBUZZ_SAMPLE`: HarfBuzz for samples only.
- Core bundled third-party container code is documented in
  `Include/RmlUi/Core/Containers/LICENSE.txt` as MIT licensed.
- The debugger includes Courier Prime font assets under the SIL Open Font
  License, documented in `Source/Debugger/LICENSE.txt`.
- The Vulkan backend includes Vulkan Memory Allocator under an MIT-style
  license in `Backends/RmlUi_Vulkan/LICENSE.txt`.
- The SDL GPU backend includes generated shader tooling notice for
  SDL_shadercross under the zlib license at
  `Backends/RmlUi_SDL_GPU/SDL_shadercross/LICENSE.txt`.

## Build-System Findings

The `6.2` archive is CMake-first:

- Root `CMakeLists.txt` declares project `RmlUi` version `6.2`.
- No `meson.build` file exists anywhere in the extracted archive.
- Upstream CMake creates `RmlUi::RmlUi` as an interface alias over core and
  debugger targets, with optional Lua bindings.
- Default upstream CMake options include `BUILD_SHARED_LIBS=ON`,
  `RMLUI_SAMPLES=OFF`, `RMLUI_FONT_ENGINE=freetype`,
  `RMLUI_LUA_BINDINGS=OFF`, `RMLUI_LOTTIE_PLUGIN=OFF`,
  `RMLUI_SVG_PLUGIN=OFF`, and `RMLUI_THIRDPARTY_CONTAINERS=ON`.

## Wrap Result

`subprojects/rmlui.wrap` was added as a downloadable Meson wrap using
`method = cmake`. This lets Meson resolve and download the pinned upstream
source because the archive contains `CMakeLists.txt` but no `meson.build`.

The wrap is intentionally not build-wired yet:

- No `[provide]` section is declared, because WORR has no Meson dependency
  object or CMake subproject bridge for RmlUi yet.
- No patch overlay is included, because this slice only records upstream source
  acquisition and provenance.
- No WORR build files were changed. A future `FR-09-T02` wiring pass must decide
  whether to use Meson's CMake subproject module, add a Meson patch overlay, or
  vendor a reviewed source snapshot.

This means the wrap is an actual downloadable acquisition record, but it does
not make `dependency('rmlui')`, `dependency('RmlUi')`, or `RmlUi::RmlUi`
available to WORR targets by itself.

## Remaining Risks

- GitHub-generated archive bytes are pinned by SHA-256 here, but a future
  supply-chain policy may prefer a signed release asset or vendored snapshot.
- Upstream has no native Meson build, so Worker 2 must bridge the CMake targets
  or add a Meson patch before compile/link validation can happen.
- License packaging for final distribution still needs a notices plan covering
  the root MIT license and any bundled or enabled optional components.
- The upstream README points to `Backends/RmlUi_SDL_GPU/LICENSE.txt`, while the
  archive places the SDL_shadercross zlib notice under
  `Backends/RmlUi_SDL_GPU/SDL_shadercross/LICENSE.txt`.
- Future Vulkan/RTX work must use native `rend_vk`/`vk_`/`pt_` integration.
  This source audit does not authorize any Vulkan-to-OpenGL renderer fallback.

## Validation

Commands run for this slice:

```text
Invoke-WebRequest -Uri https://github.com/mikke89/RmlUi/archive/refs/tags/6.2.tar.gz -OutFile .tmp\rmlui\round15-dependency-source\RmlUi-6.2.tar.gz
Get-FileHash -Algorithm SHA256 .tmp\rmlui\round15-dependency-source\RmlUi-6.2.tar.gz
tar -tf .tmp\rmlui\round15-dependency-source\RmlUi-6.2.tar.gz
git ls-remote --tags https://github.com/mikke89/RmlUi.git 6.2 refs/tags/6.2 refs/tags/6.2^{}
meson wrap search rmlui
```

Post-edit validation:

```text
git diff --check -- subprojects/rmlui.wrap docs-dev/rmlui-agent1-dependency-source-round15-2026-07-02.md
git diff --no-index --check -- NUL subprojects\rmlui.wrap
git diff --no-index --check -- NUL docs-dev\rmlui-agent1-dependency-source-round15-2026-07-02.md
meson subprojects download --sourcedir .tmp\rmlui\round15-dependency-source\meson-wrap-check --types file rmlui
```

The path-limited `git diff --check` exited cleanly. Because both owned files
are new and untracked, the no-index checks above were also run against `NUL`;
they reported only Git's LF-to-CRLF normalization warnings and no whitespace
errors. The scratch Meson download resolved the wrap and created
`.tmp/rmlui/round15-dependency-source/meson-wrap-check/subprojects/RmlUi-6.2`
without touching the real `subprojects/RmlUi-6.2` path.

No full Meson configure or compile was run.
