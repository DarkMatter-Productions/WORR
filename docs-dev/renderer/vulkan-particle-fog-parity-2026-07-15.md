# Native Vulkan Particle Fog Parity

Date: 2026-07-15

Task ID: `FR-01-T12`

Status: deterministic visible particle fog is covered by retained native-Vulkan
versus OpenGL evidence. Beam, flare, sprite, and other effect receivers remain
open.

## Fixture

`assets/renderer_parity/fr01_particle_fog.cfg` reuses the authored global-fog
map and enables the debug-build `cl_testparticles` field after the fixed camera
is in place. The engine explicitly initializes 8,192 fixed visible particles
in view, while both backends retain the legacy non-additive radial style and a
part scale of two. This avoids gameplay timing, networking, and random emitter
state while exercising the native Vulkan particle submission path as a real
fog receiver.

## Gate and result

The 640 x 500 crop contains 320,000 pixels. The visible receiver passes at
maximum RGB `1 / 1 / 1`, mean absolute RGB
`0.021778 / 0.001706 / 0.025975`, and zero pixels above one. The exact
fogged-particle-colour probe records 24,255 OpenGL and 24,251 Vulkan pixels of
`75 / 132 / 174`, a `0.016491%` count delta and IoU `0.998929`; the retained
limits are at least 24,000 pixels, `0.1%` count delta, and IoU `0.998`.
Vulkan validation emitted no diagnostics.

## Scope

The fixture needs a debug-capable capture build because `cl_testparticles` is
intentionally compiled under `USE_DEBUG`; the repository's parity build is
configured with debug support. This is a test-only scene, not a user-facing
renderer control. The companion additive/shape matrix is recorded in
`vulkan-particle-shape-additive-visual-parity-2026-07-19.md`. Beam, flare,
sprite, and specialised effect fog receivers remain `FR-01-T12` work.
