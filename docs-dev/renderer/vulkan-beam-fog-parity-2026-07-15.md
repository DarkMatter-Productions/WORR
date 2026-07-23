# Native Vulkan Beam Fog Parity

Date: 2026-07-15

Task ID: `FR-01-T12`

Status: the native `RF_BEAM` fog receiver has retained OpenGL-versus-Vulkan
evidence. Flare, sprite, and other specialised effect receivers remain open.

## Fixture

`tools/renderer_parity/generate_beam_fog_fixture.py` generates
`worr_fr01_beam_fog.bsp` from the authored global-fog world. It adds a
start-on, fat `target_laser`, which submits through the normal game/cgame
`RF_BEAM` path and reaches the native `VK_Entity_AddBeam` path rather than any
OpenGL fallback.

The fixture sets all four `rgba` bytes to palette index 224. The client
chooses one of the four packed beam palette bytes per frame; making those
bytes identical removes that legacy presentation jitter while retaining the
real beam renderer. The generated map, configuration, manifest, and source
checks are packaged as normal renderer-parity assets.

## Gate and result

The fixed 400 x 320 lower-right crop contains 128,000 pixels. The retained
gate allows no pixel above RGB error seven and constrains mean absolute error
to `0.25 / 1.0 / 0.25`. The final validation run measured maximum error
`2 / 1 / 2`, mean absolute error `0.231367 / 0.056672 / 0.220961`, and zero
pixels over the threshold.

The fogged target-laser probe accepts the palette/fog range
`80..145 / 110..150 / 130..172`. It found 39,526 OpenGL and 39,941 Vulkan
pixels, a 1.039% count delta and 0.989610 mask intersection-over-union. The
otherwise identical fogged map with no laser contains zero pixels in that
range, so the probe cannot pass on the world backdrop alone. Vulkan validation
emitted no diagnostics.

## Fog activation control

Paired no-laser controls isolate the beam contribution from the fogged world.
With authored fog, the OpenGL/Vulkan beam contributions affect 34,892 / 35,434
pixels above RGB delta eight; their mean absolute deltas are respectively
`22.358 / 5.493 / 21.355` and `22.899 / 5.625 / 21.871`. The otherwise
identical fog-disabled beam captures affect 47,930 / 47,996 pixels and have
larger contribution deltas of `41.627 / 23.601 / 11.729` and
`42.131 / 23.893 / 11.866`. This proves the retained receiver is actually
attenuated and recoloured by authored fog rather than merely occupying a
fogged scene.

## Remaining scope

Global, height, sky, transparent world, particle, beam, flare, and sprite fog
receivers now have durable native Vulkan/OpenGL evidence. Broader specialised
effect receivers remain `FR-01-T12` work.
