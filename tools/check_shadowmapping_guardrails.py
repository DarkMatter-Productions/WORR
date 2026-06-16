#!/usr/bin/env python3
"""Fail if removed WORR shadowmapping fallback/slot-churn paths reappear."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys


BANNED_TOKENS = (
    "gl_shadowlight_no_slot_mode",
    "gl_shadowmap_pvs_priority",
    "gl_shadowmap_hysteresis",
    "gl_shadowmap_sticky_ms",
    "gl_shadowmap_sticky_boost",
    "gl_shadowmap_cache_lights",
    "shadow_fallback_no_slot",
    "shadow_skipped_no_slot",
    "fallback_no_slot",
)

DEFAULT_ROOTS = (
    "inc/renderer",
    "src/renderer",
    "src/rend_gl",
    "src/rend_vk",
    "src/rend_rtx",
)


TRANSIENT_NO_SHADOW_SNIPPETS = (
    ("src/game/cgame/cg_tent.cpp",
     "ex->ent.flags = RF_FULLBRIGHT | RF_NOSHADOW;"),
    ("src/game/cgame/cg_tent.cpp",
     "ex->ent.flags = RF_FULLBRIGHT | RF_TRANSLUCENT | RF_NOSHADOW;"),
    ("src/game/cgame/cg_tent.cpp",
     "ex->ent.flags = RF_BEAM | RF_NOSHADOW;"),
    ("src/renderer/shadow_frontend.c",
     "ShadowFrontend_ModelIsTransientNoShadow"),
    ("src/renderer/shadow_frontend.c",
     "\"sprites/\""),
    ("src/renderer/shadow_frontend.c",
     "\"models/objects/laser/\""),
    ("src/renderer/shadow_frontend.c",
     "\"models/objects/grenade\""),
    ("src/renderer/shadow_frontend.c",
     "\"models/objects/explode/\""),
    ("src/renderer/shadow_frontend.c",
     "\"models/proj/\""),
)

SHADOW_LIGHT_HANDLING_SNIPPETS = (
    ("inc/client/cgame_entity.h",
     "void (*V_AddLightExVis)(cl_shadow_light_t *light, bool strict_pvs);"),
    ("src/game/cgame/cg_effects.cpp",
     "else if (cl.baselines[entnum].number == entnum)"),
    ("src/game/cgame/cg_effects.cpp",
     "V_AddLightExVis(&cl.shadowdefs[i].light, strict_pvs);"),
    ("src/client/view.cpp",
     "Q_clipf(light->coneangle, 1.0f, 89.0f)"),
    ("src/renderer/shadow_frontend.c",
     "ShadowFrontend_AreaAllowsLight"),
    ("src/renderer/shadow_frontend.c",
     "ShadowFrontend_AreaAllowsBounds"),
    ("src/renderer/shadow_frontend.c",
     "ShadowFrontend_SphereTouchesPVS2"),
    ("src/renderer/shadow_frontend.c",
     "ShadowFrontend_BoundsSphereIntersectsCaster(light->influence_sphere"),
    ("src/renderer/shadow_frontend.c",
     "VectorCopy(dl->sphere, desc->influence_sphere);"),
    ("src/renderer/shadow_frontend.c",
     "desc->radius = max(dl->radius, 1.0f);"),
    ("src/rend_gl/shader.c",
     "glr.ppl_dlight_bits & BIT_ULL(n)"),
    ("src/rend_gl/mesh.c",
     "glr.ppl_dlight_bits |= BIT_ULL(i);"),
    ("src/rend_vk/vk_main.c",
     "bool R_SupportsPerPixelLighting(void)\n{\n    return true;\n}"),
    ("src/client/entities.cpp",
     "light.ignore_owner_casters = true;"),
    ("src/client/entities.cpp",
     "light.dynamic_shadow = true;"),
    ("src/game/cgame/cg_entities.cpp",
     "light.ignore_owner_casters = true;"),
    ("src/game/cgame/cg_entities.cpp",
     "light.dynamic_shadow = true;"),
    ("src/client/view.cpp",
     "light->dynamic_shadow ? DL_SHADOW_DYNAMIC : DL_SHADOW_LIGHT"),
    ("src/renderer/shadow_frontend.c",
     "ShadowFrontend_LightIgnoresOwnerCaster"),
    ("src/renderer/shadow_frontend.c",
     "caster->shadow_only || (caster->flags & RF_WEAPONMODEL)"),
    ("inc/renderer/shadow_frontend.h",
     "#define SHADOW_FRONTEND_MAX_LIGHTS      (MAX_DLIGHTS + 1)"),
    ("src/rend_gl/main.c",
     "const bool shadowmaps_active = shadow_policy.enabled;"),
    ("src/rend_gl/main.c",
     "bool use_ppl = gl_per_pixel_lighting->integer > 0 ||"),
    ("src/rend_gl/shader.c",
     "gl_per_pixel_lighting->integer != 0 ||\n         (glr.ppl_bits & GLS_DYNAMIC_LIGHTS) != 0"),
    ("src/rend_gl/shadow.c",
     "GL_SHADOW_CONE_RECEIVER_BIAS"),
    ("src/rend_vk/vk_shadow.c",
     "VK_SHADOW_CONE_RECEIVER_BIAS"),
    ("src/rend_gl/shadow.c",
     "#define GL_SHADOW_DEFAULT_STRENGTH 1.0f"),
    ("src/rend_vk/vk_shadow.c",
     "#define VK_SHADOW_DEFAULT_STRENGTH 1.0f"),
    ("src/rend_gl/gl.h",
     "GL_DLIGHT_RECEIVER_WEAPON"),
    ("src/rend_gl/shader.c",
     "owner_weapon_spot ? 0.0f : dl->conecos"),
    ("src/rend_gl/shader.c",
     "gls.u_dlights.pad[0] = 0;"),
    ("src/rend_vk/vk_entity.c",
     "static uint32_t VK_Entity_LightingFlags(const entity_t *ent, bool fullbright)"),
    ("src/rend_gl/mesh.c",
     "dl->shadow_owner_entity == glr.ent->owner_entity"),
    ("src/client/entities.cpp",
     "FLASHLIGHT_SHADOW_RADIUS = 1024.0f"),
    ("src/client/entities.cpp",
     "FLASHLIGHT_SHADOW_INTENSITY = 6.0f"),
    ("src/game/cgame/cg_entities.cpp",
     "FLASHLIGHT_SHADOW_RADIUS = 1024.0f"),
    ("src/game/cgame/cg_entities.cpp",
     "FLASHLIGHT_SHADOW_INTENSITY = 6.0f"),
    ("src/client/entities.cpp",
     "FLASHLIGHT_TORSO_DOWN_OFFSET = 18.0f"),
    ("src/client/entities.cpp",
     "FLASHLIGHT_TORSO_IDLE_BREATH_SPEED = 0.0017f"),
    ("src/client/entities.cpp",
     "CL_FlashlightTorsoSwayScale"),
    ("src/client/entities.cpp",
     "FLASHLIGHT_TORSO_DAMAGE_BOB_SPEED = 0.026f"),
    ("src/client/main.cpp",
     "cl_flashlight_torso_sway = Cvar_Get(\"cl_flashlight_torso_sway\", \"1\", CVAR_ARCHIVE)"),
    ("src/client/entities.cpp",
     "VectorMA(start, -FLASHLIGHT_TORSO_DOWN_OFFSET, cl.v_up, start)"),
    ("src/client/entities.cpp",
     "CL_ApplyFlashlightTorsoMotion(start, forward)"),
    ("src/game/cgame/cg_entities.cpp",
     "FLASHLIGHT_TORSO_DOWN_OFFSET = 18.0f"),
    ("src/game/cgame/cg_entities.cpp",
     "FLASHLIGHT_TORSO_IDLE_BREATH_SPEED = 0.0017f"),
    ("src/game/cgame/cg_entities.cpp",
     "CL_FlashlightTorsoSwayScale"),
    ("src/game/cgame/cg_entities.cpp",
     "FLASHLIGHT_TORSO_DAMAGE_BOB_SPEED = 0.026f"),
    ("src/game/cgame/cg_entity_api.cpp",
     "cl_flashlight_torso_sway = Cvar_Get(\"cl_flashlight_torso_sway\", \"1\", CVAR_ARCHIVE)"),
    ("src/game/cgame/cg_entities.cpp",
     "VectorMA(start, -FLASHLIGHT_TORSO_DOWN_OFFSET, cl.v_up, start)"),
    ("src/game/cgame/cg_entities.cpp",
     "CL_ApplyFlashlightTorsoMotion(start, forward)"),
)

VIEWWEAPON_RECEIVER_BANNED_SNIPPETS = (
    ("src/rend_gl/shader.c",
     "gls.u_dlights.pad[0] = weapon_receiver ? 1 : 0;"),
    ("src/rend_vk/vk_entity.c",
     "if (weapon_model) {\n        flags |= VK_ENTITY_VERTEX_NO_SHADOW;\n    }"),
)


def iter_files(repo: Path, roots: tuple[str, ...]):
    for root in roots:
        path = repo / root
        if path.is_file():
            yield path
            continue
        if not path.exists():
            continue
        for candidate in path.rglob("*"):
            if candidate.is_file() and candidate.suffix.lower() in {
                ".c",
                ".cc",
                ".cpp",
                ".h",
                ".hpp",
                ".glsl",
                ".vert",
                ".frag",
                ".comp",
            }:
                yield candidate


def read_text(repo: Path, rel: str, failures: list[str]) -> str:
    path = repo / rel
    try:
        return path.read_text(encoding="utf-8", errors="ignore")
    except OSError as exc:
        failures.append(f"{rel}: could not read: {exc}")
        return ""


def check_transient_no_shadow(repo: Path, failures: list[str]) -> None:
    skip_pattern = re.compile(
        r"shadow_skip_effects\s*=\s*[^;]*\bEF_GRENADE\b[^;]*;",
        re.DOTALL,
    )
    for rel in ("src/client/entities.cpp", "src/game/cgame/cg_entities.cpp"):
        text = read_text(repo, rel, failures)
        if text and len(skip_pattern.findall(text)) < 2:
            failures.append(
                f"{rel}: shadow_skip_effects must include EF_GRENADE in both "
                "main and first-person shadow caster paths")

    for rel, snippet in TRANSIENT_NO_SHADOW_SNIPPETS:
        text = read_text(repo, rel, failures)
        if text and snippet not in text:
            failures.append(f"{rel}: missing transient no-shadow guard {snippet!r}")


def check_shadow_light_handling(repo: Path, failures: list[str]) -> None:
    for rel, snippet in SHADOW_LIGHT_HANDLING_SNIPPETS:
        text = read_text(repo, rel, failures)
        if text and snippet not in text:
            failures.append(f"{rel}: missing shadow light handling guard {snippet!r}")


def check_viewweapon_receiver_shadows(repo: Path, failures: list[str]) -> None:
    for rel, snippet in VIEWWEAPON_RECEIVER_BANNED_SNIPPETS:
        text = read_text(repo, rel, failures)
        if text and snippet in text:
            failures.append(f"{rel}: viewweapon receivers must not opt out of shadowmaps")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", default=".", help="Repository root.")
    parser.add_argument("roots", nargs="*", help="Optional roots to scan.")
    args = parser.parse_args()

    repo = Path(args.repo).resolve()
    roots = tuple(args.roots) if args.roots else DEFAULT_ROOTS
    failures: list[str] = []

    for path in iter_files(repo, roots):
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError as exc:
            failures.append(f"{path}: could not read: {exc}")
            continue
        rel = path.relative_to(repo)
        for line_no, line in enumerate(text.splitlines(), start=1):
            for token in BANNED_TOKENS:
                if token in line:
                    failures.append(f"{rel}:{line_no}: banned shadow token '{token}'")

    check_transient_no_shadow(repo, failures)
    check_shadow_light_handling(repo, failures)
    check_viewweapon_receiver_shadows(repo, failures)

    if failures:
        print("Shadowmapping guardrail failed:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1

    print("Shadowmapping guardrail passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
