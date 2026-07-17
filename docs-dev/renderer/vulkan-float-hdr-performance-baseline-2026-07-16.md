# Native Vulkan Float-HDR Performance Baseline

Tasks: `FR-01-T15`, `FR-02-T13`

## Scope

This is an evidence-only baseline for the native float-scene HDR path. It is
not a performance budget or a cross-machine claim: the local driver identity
was not pinned and the result is intended to direct the next optimization.

## Reproduction

- Fixture/config: `renderer_parity/fr01_renderer_perf_hdr_static.cfg` on
  `worr_fr01_bmodel_first_frame.bsp` at `960x720` through the hidden native
  surface.
- Adapter: `Intel(R) Iris(R) Xe Graphics`.
- Collector: 120 samples per backend, 10 warm-up samples discarded, no Vulkan
  validation layer enabled.
- Provenance-bound captures and analysis:
  `.tmp/renderer-perf/hdr-static-float-scene-local-release/capture.json` and
  `.tmp/renderer-perf/hdr-static-float-scene-local-release/analysis.json`.

## Result

| Metric | Vulkan | OpenGL | Vulkan / OpenGL |
| --- | ---: | ---: | ---: |
| CPU mean | 0.807 ms | 0.856 ms | 0.943x |
| GPU mean | 1.935 ms | 0.365 ms | 5.305x |
| GPU post mean | 0.835 ms | 0.353 ms | 2.367x |
| GPU scene mean | 1.072 ms | n/a | n/a |

The native Vulkan HDR path records four draws and 192 upload bytes per sample;
the fixed OpenGL scene records two draws. The CPU result means the next work
should not trade additional CPU submission for a small GPU change. The GPU
postprocess result identifies the float scene copy/presentation path as the
priority before accepting any HDR performance budget.

The validation-enabled paired run is intentionally kept separate: validation
increased Vulkan GPU post mean to 1.095 ms, so it is a correctness gate rather
than a release-performance result.

## Direct scene sampling follow-up

Task: `FR-01-T15`

The simple float-HDR path now transitions the completed
`R16G16B16A16_SFLOAT` scene attachment directly to shader-read and samples it
in the final native Vulkan composite. It no longer records the full-resolution
scene-to-copy `vkCmdCopyImage` when liquid refraction, automatic exposure,
bloom, DOF, and LUT processing are all inactive. Those effects retain the
copy/mip path they require. A dedicated final-pass descriptor keeps the
shader's statically declared (but inactive) auto-exposure sampler bound to the
direct scene image, avoiding an undefined-layout dependency.

The release-style follow-up at the same local fixture collected 110 post-warmup
samples and passed its capture analysis. It measured `0.838 ms` Vulkan GPU-post
mean, within `0.003 ms` of the previous local `0.835 ms` result. That is not a
demonstrable GPU win on this unpinned Intel driver, so no budget or superiority
claim is made from it. It does prove the native copy removal is output-neutral:
the validation-enabled static HDR and HDR-disabled captures exact-compare all
50,000 crop pixels and emit no Vulkan validation errors.

Artifacts: `.tmp/renderer-perf/hdr-static-direct-scene-local-release/analysis.json`
and `.tmp/renderer-parity/hdr-direct-scene-validation-clean/`.
