# Vulkan Viewmodel, Dynamic-Light Shadow, and Item Glow Fixes

Date: 2026-06-12

Task IDs: `FR-01-T04`, `FR-02-T10`, `FR-02-T11`, `DV-02-T06`, `DV-07-T05`

## Summary

This pass closes three gameplay-visible renderer regressions:

- Native Vulkan first-person weapon models could render as scrambled translucent geometry.
- Short-lived effect dynamic-light shadowmaps could flicker or churn in both OpenGL and Vulkan.
- Native Vulkan did not apply the classic `RF_GLOW` bonus-item light pulse.

All renderer work remains native to each backend. No Vulkan path redirects to OpenGL.

## Vulkan View Weapon Meshes

The native Vulkan entity renderer used one depthhack pipeline for every
`RF_DEPTHHACK` batch. That pipeline was created as alpha blended, depth-test
always, and depth-write disabled, so opaque first-person weapon triangles
blended over each other in submission order. MD2 and MD5 view weapons were
therefore especially prone to a scrambled/inside-out look.

The fix splits the depthhack path into opaque and alpha pipelines:

- opaque weapon batches keep blending disabled and depth writes enabled;
- alpha weapon/effect batches keep alpha blending and no depth writes;
- both paths use the normal `LESS_OR_EQUAL` depth compare;
- the command buffer sets a near depth range (`maxDepth = 0.25`) for the
  depthhack pass, mirroring GL's `GL_DepthRange(0, 0.25)` behavior.

This preserves first-person depth compression without turning opaque weapon
geometry into translucent triangle soup.

## Effect Dynamic-Light Shadows

Effect lights previously reached the shared shadow frontend without stable
light identity. Moving projectiles, temporary explosions, and entity effect
lights could change their owner/cache identity when their origin, radius,
fade, or insertion order changed. That made shadow pages churn and could
show up as flicker in both `rend_gl` and `rend_vk`.

The fix adds stable-key plumbing for simple effect lights:

- `V_AddLightWithKey(...)` carries a stable key into `dlight_t::shadow_stable_id`.
- cdlight queues preserve their existing `cdlight_t::key`.
- packet-entity effect lights derive stable keys from entity number, effect
  bits, and callsite source.
- temporary explosion lights receive a per-allocation serial key.
- the cgame entity import API now exposes `V_AddLightWithKey`; its API
  version is bumped to `3`.
- the client light-cap sticky allocator uses stable keys for keyed simple
  lights and dynamic owner/source shadowlights.
- the shared shadow frontend namespaces dynamic stable IDs and excludes
  dynamic origin/direction/radius/cone drift from residency keys, because
  dynamic pages are already redrawn when light parameters change.

Static/authored shadowlights still include their spatial/projection data in
cache keys.

## Vulkan Item Glow Pulse

`RF_GLOW` now follows the OpenGL pulse rule in the Vulkan entity light color
path:

- pulse phase uses `frame_time * 7.0`;
- the light color is offset by the same `0.1 * sin(...)` amount;
- each channel keeps the GL floor of `80%` of the sampled light.

This restores the visible bonus-item glow pulse in Vulkan. This is separate
from texture glowmaps such as `*_glow.png`, which remain a distinct emissive
map follow-up from the broader Vulkan parity audit.

## Verification

Commands run from `E:\Repositories\WORR`:

- `git diff --check`
- `meson compile -C builddir-win`
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew`
- `python tools/shadowmapping_repro_smoke.py --renderer vulkan --scene translated-md2 --scene projectile-self-shadow --wait 60`
- `python tools/shadowmapping_repro_smoke.py --renderer opengl --scene projectile-self-shadow --wait 60`
- `python tools/check_shadowmapping_guardrails.py`
- Vulkan visual screenshot:
  - `.install\basew\screenshots\codex_vk_viewweapon_after.png`

The smoke logs reported selected shadow views with `unsupported=0` for the
Vulkan and OpenGL projectile cases. The Vulkan screenshot showed the
first-person blaster rendering as a coherent opaque viewmodel.
