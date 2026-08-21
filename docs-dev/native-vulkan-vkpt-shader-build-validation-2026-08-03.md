# Native Vulkan and VKPT shader build validation

Date: 2026-08-03  
Roadmap task: `DV-02-T07`

## Outcome

Native Vulkan embedded-shader freshness and VKPT shader compilation are now
mandatory renderer build dependencies. Both paths fail closed if their Vulkan
SDK tools are unavailable, and every SPIR-V module is passed through
`spirv-val` before a successful stamp can be published.

The implementation deliberately keeps the two renderer contracts separate:

- Native Vulkan has 28 tracked GLSL sources producing 48 embedded modules in
  eight tracked headers, including the UI2D shaders consumed by `vk_ui.c`. Its
  CI path verifies freshness without rewriting those
  headers.
- VKPT has 47 explicitly listed shader sources and 26 explicitly listed include
  dependencies producing exactly 51 build-directory modules. Its compiler path
  replaces the complete output directory atomically.

## Native Vulkan embedded shaders

`tools/gen_vk_world_spv.py` now has two explicit modes:

- `--check` is the read-only build and CI mode. It reconstructs every embedded
  bytecode array, verifies the module's deterministic provenance instruction,
  renders the entire expected header canonically, requires a byte-for-byte
  match, and validates all 48 modules with `spirv-val`.
- `--write` is the developer regeneration mode. It compiles all modules with
  `glslangValidator`, injects and validates provenance, and publishes the eight
  tracked headers as one rollback-capable set only after the complete in-memory
  generation pass succeeds. Existing file modes are preserved.

The historical `--validate` option remains as a deprecated alias for `--write`
so old engineering notes and local scripts keep working. It is not used by the
build or CI because it intentionally mutates tracked files.

The native compiler and validator both use `--target-env vulkan1.0`. The
freshness contract is intentionally compiler-version independent: normal
builds do not produce noisy tracked-header rewrites merely because CI and a
developer have different Vulkan SDK patch versions. Every compiled module has
a SPIR-V 1.0-compatible `OpString` containing a SHA-256 provenance value bound
to the normalized source bytes, symbol, defines, target environment, and
generator schema. The fixed instruction is 32 words so insertion does not
shift following eight-word header lines. Editing only the visible header digest
cannot make unrelated or stale bytecode pass. Any generation-semantic change
must update `GENERATOR_SCHEMA` in the generator.

Source hashing canonicalizes CRLF and lone CR to LF, and `.gitattributes`
requires LF for every shader-source extension used by the two renderers. This
keeps Windows and Linux freshness checks identical.

The generator rejects missing or unlisted `.vert`/`.frag` source files and
requires every `src/rend_vk/*_spv.h` header to be present in the exact generator
and Meson inventories. Missing `spirv-val`, provenance mismatches, noncanonical
array tokens, wrong size declarations, extra content, reordered symbols,
malformed arrays, and missing/extra embedded symbols all prevent a success
stamp.

## VKPT shaders

`tools/compile_vkpt_shaders.py` replaces directory scanning and timestamp-based
partial compilation with an exact inventory. It rejects missing or unlisted
shader sources and include dependencies before compilation starts.

Every invocation performs a clean compilation of all 51 modules into a sibling
staging directory. Ray-generation sources produce both pipeline and ray-query
variants, which accounts for the four additional modules over the 47 source
files. `glslangValidator` and `spirv-val` both use
`--target-env vulkan1.2`. Only a fully compiled, fully validated, exact output
set is atomically published; stale outputs from an earlier inventory are
removed by complete-directory replacement. A failed compile or validation
leaves the previous published directory and build stamp untouched.

Publication is restricted to a lexical output basename of `shader_vkpt`.
Existing roots and ancestors must not be links, junctions/reparse points, or
special files. A non-empty output is replaceable only when it contains the
fixed `.worr-vkpt-shader-output` ownership marker; every owned entry must be a
single-link regular file in a flat, no-follow scan. Missing and empty roots are
adoptable. The marker is included in every staged publication, preventing an
arbitrary non-empty directory from being used as a destructive replacement
target. Stamps are lexical paths and are rejected inside the output tree.

The exact script mapping is checked against `LIST_SHADER_MODULES`,
`LIST_RT_RGEN_SHADER_MODULES`, and `LIST_RT_PIPELINE_SHADER_MODULES` in
`src/rend_rtx/vkpt/vkpt.h`. This binds the 35 ordinary modules, four
ray-generation sources with pipeline/query variants, and eight pipeline-only
ray-tracing modules to the runtime loader contract.

Tool discovery accepts explicit `--glslang` and `--spirv-val` paths, then checks
`VULKAN_SDK`, installed Windows SDK locations, and `PATH`. Absence of either
tool is a hard error rather than a skipped renderer check.

## Meson and pull-request CI

Meson exposes the following build-by-default targets when Vulkan is enabled on
a 64-bit target:

- `native_vk_shader_freshness` depends on the exact 28 native sources, eight
  headers, generator script, and atomic-publication helper. The external native
  Vulkan library has a hard link dependency on this target.
- `vkpt_shaders` depends on the exact 47 sources, 26 includes, compiler script,
  runtime `vkpt.h` inventory, and atomic-directory helper. It recompiles only
  when an input changes.
- `vkpt_shader_validation` is an always-stale, read-only check depending on the
  compiler target. Every normal or renderer build no-follow scans the marker
  and exact output inventory and runs `spirv-val` on all 51 existing modules.
  The external RTX library has a hard link dependency on this validation
  target, so deleting or corrupting a side-effect module cannot be hidden by an
  up-to-date compile stamp.

The pull-request workflow has independently named `renderer-opengl`,
`renderer-vulkan`, and `renderer-rtx` jobs rather than a renderer matrix. The
native Vulkan lane explicitly runs `native_vk_shader_freshness`; the RTX lane
explicitly runs both `vkpt_shaders` and `vkpt_shader_validation`. Both required
lanes install glslang and SPIR-V Tools. Release, nightly, and normal build/test
dependency installation also includes SPIR-V Tools so Meson's build-by-default
checks cannot be silently skipped there.

## Verification

The focused source-contract suite in `tools/test_shader_build_contract.py`
covers:

- exact source/header/include/output inventories, runtime-list mapping, and
  Meson parity;
- read-only native freshness, provenance forgery rejection, canonical header
  rendering, and CRLF/LF equivalence;
- rollback-capable native header-set publication with mode preservation;
- the legacy regeneration alias;
- atomic VKPT replacement of an injected stale owned output;
- refusal of unowned, misnamed, linked/special, or stamp-containing outputs;
- read-only validation of every exact owned VKPT module;
- explicit compiler and validator target environments;
- independently named PR renderer jobs and mandatory Meson dependencies; and
- fail-closed diagnostics for unavailable tools.

The implementation was also exercised against the installed Vulkan SDK at
`C:\VulkanSDK\1.4.313.1\Bin`: the native check validated 48 embedded modules
across eight headers, and the VKPT compile and independent read-only check each
validated 51 modules from 47 sources. These checks perform no engine or client
launch and need no input or render-target capture.

## Maintenance limits

Adding or removing a shader is intentionally a coordinated change. Update the
script inventory and the corresponding Meson input list together; the focused
contract test reports deterministic missing and extra entries when they drift.
For native Vulkan, regenerate the tracked headers with `--write`. For VKPT, no
generated module belongs in source control; a successful build replaces the
build-directory output set.

This task validates shader inventory, freshness, compiler success, and SPIR-V
structural validity. It does not replace runtime renderer tests, GPU-specific
validation-layer testing, or image-quality comparisons.
