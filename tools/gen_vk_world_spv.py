#!/usr/bin/env python3
"""Regenerate embedded native Vulkan SPIR-V headers from GLSL sources.

The native Vulkan renderer embeds its world, entity, and shadow shaders as
SPIR-V arrays. The GLSL sources of truth live in src/rend_vk/shaders/.
Whenever they change, run this script so the embedded headers, the C-side UBO
struct (vk_shadow_uniform_t), and the descriptor-set layout in vk_shadow.c stay
in sync. The ShadowPages uniform block layout and the set=2 bindings in
vk_world_shadow.frag/vk_entity.frag MUST match vk_shadow.c exactly; a mismatch
silently corrupts every shadow page matrix at runtime.

Usage:
    python tools/gen_vk_world_spv.py [--validate]
"""

from __future__ import annotations

import argparse
import glob
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SHADER_DIR = REPO_ROOT / "src" / "rend_vk" / "shaders"

SHADER_GROUPS = (
    (
        REPO_ROOT / "src" / "rend_vk" / "vk_world_spv.h",
        (
            ("vk_world.vert", "vk_world_animated_vert_spv"),
            ("vk_world_shadow.frag", "vk_world_animated_frag_spv",
             ("VK_WORLD_ANIMATED=1",)),
            ("vk_world_shadow.frag", "vk_world_bloom_extract_frag_spv",
             ("VK_WORLD_ANIMATED=1", "VK_WORLD_BLOOM_EXTRACT=1")),
            ("vk_world_shadow.frag", "vk_world_lightmap_debug_frag_spv",
             ("VK_WORLD_ANIMATED=1", "VK_WORLD_LIGHTMAP_DEBUG=1")),
            ("vk_world_shadow.frag", "vk_world_fast_lit_frag_spv",
             ("VK_WORLD_ANIMATED=1", "VK_WORLD_STATIC_FAST_LIT=1")),
            ("vk_world_shadow.frag", "vk_world_fast_lit_no_fog_frag_spv",
             ("VK_WORLD_ANIMATED=1", "VK_WORLD_STATIC_FAST_LIT=1",
              "VK_WORLD_STATIC_FAST_LIT_NO_FOG=1")),
            ("vk_world_shadow.frag", "vk_world_fast_lit_glowmap_frag_spv",
             ("VK_WORLD_ANIMATED=1", "VK_WORLD_STATIC_FAST_LIT_GLOWMAP=1")),
            ("vk_world_shadow.frag", "vk_world_fast_lit_glowmap_no_fog_frag_spv",
             ("VK_WORLD_ANIMATED=1", "VK_WORLD_STATIC_FAST_LIT_GLOWMAP=1",
              "VK_WORLD_STATIC_FAST_LIT_GLOWMAP_NO_FOG=1")),
            ("vk_world_shadow.frag", "vk_world_texture_replace_frag_spv",
             ("VK_WORLD_ANIMATED=1", "VK_WORLD_TEXTURE_REPLACE=1")),
            ("vk_world_shadow.frag",
             "vk_world_texture_replace_no_fog_frag_spv",
             ("VK_WORLD_ANIMATED=1", "VK_WORLD_TEXTURE_REPLACE=1",
              "VK_WORLD_TEXTURE_REPLACE_NO_FOG=1")),
            ("vk_world_texture_replace.vert", "vk_world_texture_replace_vert_spv"),
            ("vk_world_texture_replace.vert", "vk_world_texture_replace_no_fog_vert_spv",
             ("VK_WORLD_TEXTURE_REPLACE_NO_FOG=1",)),
            ("vk_world_texture_replace.frag", "vk_world_texture_replace_specialized_frag_spv"),
            ("vk_world_texture_replace.frag", "vk_world_texture_replace_specialized_no_fog_frag_spv",
             ("VK_WORLD_TEXTURE_REPLACE_NO_FOG=1",)),
            ("vk_world_shadow.vert", "vk_world_vert_spv"),
            ("vk_world_shadow.frag", "vk_world_frag_spv"),
            ("vk_world_sky.frag", "vk_world_sky_frag_spv"),
            ("vk_shadow_moment.frag", "vk_shadow_moment_frag_spv"),
            ("vk_shadow_alpha.frag", "vk_shadow_alpha_frag_spv"),
            ("vk_shadow_alpha.frag", "vk_shadow_alpha_moment_frag_spv",
             ("VK_SHADOW_MOMENT=1",)),
        ),
    ),
    (
        REPO_ROOT / "src" / "rend_vk" / "vk_entity_spv.h",
        (
            ("vk_entity.vert", "vk_entity_vert_spv"),
            ("vk_entity_gpu_md2.vert", "vk_entity_gpu_md2_vert_spv"),
            ("vk_entity_gpu_bmodel.vert", "vk_entity_gpu_bmodel_vert_spv"),
            ("vk_entity_gpu_particle.vert", "vk_entity_gpu_particle_vert_spv"),
            ("vk_entity_gpu_beam.vert", "vk_entity_gpu_beam_vert_spv"),
            ("vk_entity_gpu_bmodel_texture_replace.vert",
             "vk_entity_gpu_bmodel_texture_replace_vert_spv"),
            ("vk_entity_gpu_bmodel_texture_replace.vert",
             "vk_entity_gpu_bmodel_texture_replace_no_fog_vert_spv",
             ("VK_ENTITY_GPU_BMODEL_TEXTURE_REPLACE_NO_FOG=1",)),
            ("vk_entity_gpu_md5.vert", "vk_entity_gpu_md5_vert_spv"),
            ("vk_entity.frag", "vk_entity_frag_spv"),
            ("vk_entity.frag", "vk_entity_celshading_frag_spv",
             ("VK_ENTITY_CELSHADING=1",)),
            ("vk_entity.frag", "vk_entity_gpu_bmodel_fast_lit_frag_spv",
             ("VK_ENTITY_GPU_BMODEL_FAST_LIT=1",)),
            ("vk_entity.frag", "vk_entity_gpu_bmodel_fast_lit_no_fog_frag_spv",
             ("VK_ENTITY_GPU_BMODEL_FAST_LIT=1",
              "VK_ENTITY_GPU_BMODEL_FAST_LIT_NO_FOG=1")),
            ("vk_entity_gpu_bmodel_texture_replace.frag",
             "vk_entity_gpu_bmodel_texture_replace_frag_spv"),
            ("vk_entity_gpu_bmodel_texture_replace.frag",
             "vk_entity_gpu_bmodel_texture_replace_no_fog_frag_spv",
             ("VK_ENTITY_GPU_BMODEL_TEXTURE_REPLACE_NO_FOG=1",)),
            ("vk_entity.frag", "vk_entity_bloom_extract_frag_spv",
             ("VK_ENTITY_BLOOM_EXTRACT=1",)),
            ("vk_entity.frag", "vk_entity_bloom_extract_depth_frag_spv",
             ("VK_ENTITY_BLOOM_EXTRACT=1",
              "VK_ENTITY_BLOOM_EXTRACT_DEPTH_SAMPLE=1")),
        ),
    ),
    (
        REPO_ROOT / "src" / "rend_vk" / "vk_debug_spv.h",
        (
            ("vk_debug.vert", "vk_debug_vert_spv"),
            ("vk_debug.frag", "vk_debug_frag_spv"),
        ),
    ),
    (
        REPO_ROOT / "src" / "rend_vk" / "vk_postprocess_spv.h",
        (
            ("vk_postprocess.vert", "vk_postprocess_vert_spv"),
            ("vk_postprocess.frag", "vk_postprocess_frag_spv"),
        ),
    ),
    (
        REPO_ROOT / "src" / "rend_vk" / "vk_bloom_spv.h",
        (
            ("vk_bloom.frag", "vk_bloom_copy_frag_spv"),
            ("vk_bloom_prefilter.frag", "vk_bloom_prefilter_frag_spv"),
            ("vk_bloom_blur.frag", "vk_bloom_blur_x_frag_spv",
             ("VK_BLOOM_BLUR_HORIZONTAL=1",)),
            ("vk_bloom_blur.frag", "vk_bloom_blur_y_frag_spv"),
        ),
    ),
    (
        REPO_ROOT / "src" / "rend_vk" / "vk_crt_spv.h",
        (
            ("vk_crt.frag", "vk_crt_frag_spv"),
        ),
    ),
    (
        REPO_ROOT / "src" / "rend_vk" / "vk_dof_spv.h",
        (
            ("vk_dof.frag", "vk_dof_frag_spv"),
        ),
    ),
)

WORDS_PER_LINE = 8


def find_sdk_tool(name: str) -> str:
    candidates = []
    sdk = os.environ.get("VULKAN_SDK")
    if sdk:
        candidates.append(Path(sdk) / "Bin" / f"{name}.exe")
        candidates.append(Path(sdk) / "bin" / name)
    candidates.extend(
        Path(p) / f"{name}.exe"
        for p in sorted(glob.glob(r"C:\VulkanSDK\*\Bin"), reverse=True)
    )
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)
    # Fall back to PATH.
    return name


def compile_shader(compiler: str, source: Path,
                   defines: tuple[str, ...] = ()) -> bytes:
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / (source.name + ".spv")
        cmd = [compiler, "-V", *(f"-D{define}" for define in defines),
               str(source), "-o", str(out)]
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            sys.stderr.write(result.stdout)
            sys.stderr.write(result.stderr)
            raise SystemExit(f"glslangValidator failed for {source.name}")
        return out.read_bytes()


def validate_spirv(validator: str, blob: bytes, name: str) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / f"{name}.spv"
        path.write_bytes(blob)
        result = subprocess.run([validator, str(path)],
                                capture_output=True, text=True)
        if result.returncode != 0:
            sys.stderr.write(result.stdout)
            sys.stderr.write(result.stderr)
            raise SystemExit(f"spirv-val failed for {name}")


def format_array(symbol: str, blob: bytes) -> str:
    if len(blob) % 4 != 0:
        raise SystemExit(f"{symbol}: SPIR-V size {len(blob)} not word aligned")
    words = struct.unpack(f"<{len(blob) // 4}I", blob)
    lines = [f"static const uint32_t {symbol}[] = {{"]
    for i in range(0, len(words), WORDS_PER_LINE):
        chunk = ", ".join(f"0x{w:08x}u" for w in words[i:i + WORDS_PER_LINE])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append(f"static const size_t {symbol}_size = sizeof({symbol});")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--validate", action="store_true",
                        help="run spirv-val on each generated module")
    args = parser.parse_args()

    compiler = find_sdk_tool("glslangValidator")
    validator = find_sdk_tool("spirv-val") if args.validate else None

    for output_header, shaders in SHADER_GROUPS:
        sections = []
        for entry in shaders:
            source_name, symbol, *rest = entry
            defines = rest[0] if rest else ()
            source = SHADER_DIR / source_name
            if not source.is_file():
                raise SystemExit(f"missing shader source: {source}")
            blob = compile_shader(compiler, source, defines)
            if validator:
                validate_spirv(validator, blob, symbol)
            sections.append(format_array(symbol, blob))
            print(f"{source_name}: {len(blob)} bytes -> {symbol}")

        sources = ", ".join(entry[0] for entry in shaders)
        header = (
            f"/* Auto-generated by tools/gen_vk_world_spv.py from "
            f"src/rend_vk/shaders/{{{sources}}}. Do not edit. */\n"
            "#pragma once\n"
            "#include <stdint.h>\n"
            "#include <stddef.h>\n"
            "\n"
            + "\n\n".join(sections)
            + "\n"
        )
        output_header.write_text(header, newline="\n")
        print(f"wrote {output_header}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
