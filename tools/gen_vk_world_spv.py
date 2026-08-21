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
    python tools/gen_vk_world_spv.py --check
    python tools/gen_vk_world_spv.py --write

The historical ``--validate`` spelling remains a deprecated alias for
``--write`` so old engineering records and local scripts remain usable.
"""

from __future__ import annotations

import argparse
import glob
import hashlib
import os
import re
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SHADER_DIR = REPO_ROOT / "src" / "rend_vk" / "shaders"
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from tools.atomic_directory import replace_path_with_transient_retry  # noqa: E402

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
        REPO_ROOT / "src" / "rend_vk" / "vk_ui2d_spv.h",
        (
            ("vk_ui2d.vert", "vk_ui2d_vert_spv"),
            ("vk_ui2d.frag", "vk_ui2d_frag_spv"),
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
NATIVE_TARGET_ENV = "vulkan1.0"
GENERATOR_SCHEMA = b"worr-native-vulkan-embedded-shader-v3\0vulkan1.0\0provenance-opstring-v1\0"
PROVENANCE_PREFIX = "worr-native-vulkan-input-sha256:"
PROVENANCE_STRING_LENGTH = 119
SPIRV_MAGIC = 0x07230203
OP_STRING = 7
OP_NAME = 5
OP_MEMBER_NAME = 6
ARRAY_PATTERN = re.compile(
    r"static const uint32_t ([A-Za-z_][A-Za-z0-9_]*)\[\] = \{(.*?)\};",
    re.DOTALL,
)
WORD_PATTERN = re.compile(r"0x([0-9a-fA-F]{8})u")


def find_sdk_tool(name: str, explicit_path: str | None = None) -> str:
    candidates = []
    if explicit_path:
        candidates.append(Path(explicit_path))
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
    discovered = shutil.which(f"{name}.exe") or shutil.which(name)
    if discovered:
        candidates.append(Path(discovered))
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate.resolve())
    raise SystemExit(
        f"native Vulkan shaders: required tool {name} was not found; "
        f"install the Vulkan SDK or pass "
        f"{'--glslang' if name == 'glslangValidator' else '--spirv-val'}"
    )


def compile_shader(compiler: str, source: Path,
                   defines: tuple[str, ...] = ()) -> bytes:
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / (source.name + ".spv")
        cmd = [compiler, "--target-env", NATIVE_TARGET_ENV, "-V",
               *(f"-D{define}" for define in defines),
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
        result = subprocess.run([validator, "--target-env", NATIVE_TARGET_ENV, str(path)],
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


def canonical_source_bytes(source_name: str) -> bytes:
    payload = (SHADER_DIR / source_name).read_bytes()
    return payload.replace(b"\r\n", b"\n").replace(b"\r", b"\n")


def update_digest_for_entry(
    digest: "hashlib._Hash",
    entry: tuple[object, ...],
) -> None:
    source_name, symbol, *rest = entry
    defines = rest[0] if rest else ()
    source_payload = canonical_source_bytes(str(source_name))
    for value in (str(source_name), str(symbol), *defines):
        encoded = value.encode("utf-8")
        digest.update(len(encoded).to_bytes(4, "big"))
        digest.update(encoded)
    digest.update(len(source_payload).to_bytes(8, "big"))
    digest.update(source_payload)


def input_digest(shaders: tuple[tuple[object, ...], ...]) -> str:
    digest = hashlib.sha256(GENERATOR_SCHEMA)
    for entry in shaders:
        update_digest_for_entry(digest, entry)
    return digest.hexdigest()


def module_provenance(entry: tuple[object, ...]) -> str:
    digest = hashlib.sha256(GENERATOR_SCHEMA)
    update_digest_for_entry(digest, entry)
    value = PROVENANCE_PREFIX + digest.hexdigest()
    if len(value) > PROVENANCE_STRING_LENGTH:
        raise SystemExit("native Vulkan shader provenance string exceeds fixed encoding")
    value = value.ljust(PROVENANCE_STRING_LENGTH, ".")
    encoded_words = 2 + ((len(value.encode("ascii")) + 1 + 3) // 4)
    if encoded_words % WORDS_PER_LINE != 0:
        raise SystemExit("native Vulkan shader provenance instruction lost line alignment")
    return value


def spirv_instructions(blob: bytes) -> list[tuple[int, int, int, tuple[int, ...]]]:
    if len(blob) < 20 or len(blob) % 4 != 0:
        raise SystemExit("embedded SPIR-V has an invalid byte length")
    words = struct.unpack(f"<{len(blob) // 4}I", blob)
    if words[0] != SPIRV_MAGIC:
        raise SystemExit("embedded SPIR-V has an invalid magic number")
    instructions = []
    index = 5
    while index < len(words):
        word_count = words[index] >> 16
        opcode = words[index] & 0xFFFF
        if word_count == 0 or index + word_count > len(words):
            raise SystemExit("embedded SPIR-V has a malformed instruction stream")
        instructions.append(
            (index, word_count, opcode, tuple(words[index + 1:index + word_count]))
        )
        index += word_count
    return instructions


def encode_spirv_string(value: str) -> tuple[int, ...]:
    payload = value.encode("utf-8") + b"\0"
    payload += b"\0" * (-len(payload) % 4)
    return struct.unpack(f"<{len(payload) // 4}I", payload)


def decode_spirv_string(words: tuple[int, ...]) -> str:
    payload = struct.pack(f"<{len(words)}I", *words)
    terminator = payload.find(b"\0")
    if terminator < 0 or any(payload[terminator + 1:]):
        raise SystemExit("embedded SPIR-V contains a malformed OpString")
    try:
        return payload[:terminator].decode("utf-8", errors="strict")
    except UnicodeDecodeError as error:
        raise SystemExit("embedded SPIR-V contains a non-UTF-8 OpString") from error


def inject_provenance(blob: bytes, provenance: str) -> bytes:
    instructions = spirv_instructions(blob)
    insertion_word = next(
        (index for index, _, opcode, _ in instructions if opcode in {OP_NAME, OP_MEMBER_NAME}),
        None,
    )
    if insertion_word is None:
        raise SystemExit("compiled SPIR-V has no debug-name section for provenance insertion")
    words = list(struct.unpack(f"<{len(blob) // 4}I", blob))
    result_id = words[3]
    if result_id == 0 or result_id == 0xFFFFFFFF:
        raise SystemExit("compiled SPIR-V has an invalid ID bound")
    string_words = encode_spirv_string(provenance)
    instruction = ((2 + len(string_words)) << 16) | OP_STRING
    words[3] = result_id + 1
    words[insertion_word:insertion_word] = [instruction, result_id, *string_words]
    return struct.pack(f"<{len(words)}I", *words)


def verify_provenance(blob: bytes, expected: str, symbol: str) -> None:
    markers = []
    for _, _, opcode, operands in spirv_instructions(blob):
        if opcode == OP_STRING and len(operands) >= 2:
            value = decode_spirv_string(operands[1:])
            if value.startswith(PROVENANCE_PREFIX):
                markers.append(value)
    if markers != [expected]:
        raise SystemExit(f"native Vulkan embedded shader provenance mismatch: {symbol}")


def embedded_modules(header: str, expected_symbols: tuple[str, ...]) -> dict[str, bytes]:
    modules: dict[str, bytes] = {}
    for match in ARRAY_PATTERN.finditer(header):
        symbol = match.group(1)
        if symbol in modules:
            raise SystemExit(f"duplicate embedded SPIR-V symbol: {symbol}")
        words = WORD_PATTERN.findall(match.group(2))
        if not words:
            raise SystemExit(f"embedded SPIR-V symbol has no words: {symbol}")
        modules[symbol] = struct.pack(f"<{len(words)}I", *(int(word, 16) for word in words))
    expected = set(expected_symbols)
    actual = set(modules)
    if actual != expected:
        missing = sorted(expected - actual)
        extra = sorted(actual - expected)
        details = []
        if missing:
            details.append("missing symbols: " + ", ".join(missing))
        if extra:
            details.append("extra symbols: " + ", ".join(extra))
        raise SystemExit("embedded SPIR-V inventory mismatch; " + "; ".join(details))
    return modules


def render_header(
    shaders: tuple[tuple[object, ...], ...],
    modules: dict[str, bytes],
) -> str:
    sources = ", ".join(str(entry[0]) for entry in shaders)
    sections = [format_array(str(entry[1]), modules[str(entry[1])]) for entry in shaders]
    return (
        "/* Auto-generated by tools/gen_vk_world_spv.py from "
        f"src/rend_vk/shaders/{{{sources}}}. Do not edit.\n"
        f" * Input-SHA256: {input_digest(shaders)} */\n"
        "#pragma once\n"
        "#include <stdint.h>\n"
        "#include <stddef.h>\n"
        "\n"
        + "\n\n".join(sections)
        + "\n"
    )


def expected_shader_sources() -> frozenset[str]:
    return frozenset(entry[0] for _, shaders in SHADER_GROUPS for entry in shaders)


def validate_inventory() -> None:
    expected_sources = expected_shader_sources()
    actual_sources = frozenset(
        path.name
        for path in SHADER_DIR.iterdir()
        if path.is_file() and path.suffix in {".vert", ".frag"}
    )
    missing_sources = sorted(expected_sources - actual_sources)
    extra_sources = sorted(actual_sources - expected_sources)
    if missing_sources or extra_sources:
        details = []
        if missing_sources:
            details.append("missing sources: " + ", ".join(missing_sources))
        if extra_sources:
            details.append("unlisted sources: " + ", ".join(extra_sources))
        raise SystemExit("native Vulkan shader inventory mismatch; " + "; ".join(details))

    expected_headers = frozenset(path.resolve() for path, _ in SHADER_GROUPS)
    generated_headers = frozenset(
        path.resolve() for path in (REPO_ROOT / "src" / "rend_vk").glob("*_spv.h")
    )
    extra_headers = sorted(str(path.relative_to(REPO_ROOT)) for path in generated_headers - expected_headers)
    if extra_headers:
        raise SystemExit(
            "native Vulkan shader output inventory mismatch; extra generated headers: "
            + ", ".join(extra_headers)
        )


def stage_text_file(path: Path, contents: str) -> Path:
    mode = stat.S_IMODE(path.stat().st_mode) if path.exists() else 0o644
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(contents)
            handle.flush()
            os.fsync(handle.fileno())
        os.chmod(temporary_name, mode)
        return Path(temporary_name)
    except BaseException:
        Path(temporary_name).unlink(missing_ok=True)
        raise


def atomic_write(path: Path, contents: str) -> None:
    staged = stage_text_file(path, contents)
    try:
        replace_path_with_transient_retry(staged, path)
    finally:
        staged.unlink(missing_ok=True)


def publish_headers_atomically(rendered_headers: dict[Path, str]) -> None:
    staged: dict[Path, Path] = {}
    backups: dict[Path, Path | None] = {}
    published: set[Path] = set()
    committed = False
    try:
        for path, text in rendered_headers.items():
            staged[path] = stage_text_file(path, text)
        for path in rendered_headers:
            backup: Path | None = None
            if path.exists():
                descriptor, backup_name = tempfile.mkstemp(
                    prefix=f".{path.name}.rollback.", dir=path.parent
                )
                os.close(descriptor)
                backup = Path(backup_name)
                backup.unlink()
            backups[path] = backup
            if backup is not None:
                replace_path_with_transient_retry(path, backup)
            replace_path_with_transient_retry(staged[path], path)
            published.add(path)
        committed = True
    except BaseException as publish_error:
        rollback_errors = []
        for path in reversed(tuple(rendered_headers)):
            backup = backups.get(path)
            try:
                if path in published:
                    path.unlink(missing_ok=True)
                if backup is not None and backup.exists():
                    replace_path_with_transient_retry(backup, path)
                    backups[path] = None
            except OSError as rollback_error:
                rollback_errors.append(f"{path} (recover from {backup}): {rollback_error}")
        if rollback_errors:
            raise RuntimeError(
                "native Vulkan header publication failed and rollback was incomplete: "
                + "; ".join(rollback_errors)
            ) from publish_error
        raise
    finally:
        for path in staged.values():
            path.unlink(missing_ok=True)
        for backup in backups.values():
            if committed and backup is not None:
                backup.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true",
                      help="verify tracked headers are fresh without modifying them")
    mode.add_argument("--write", action="store_true",
                      help="regenerate tracked headers")
    mode.add_argument("--validate", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--glslang", help="path to glslangValidator")
    parser.add_argument("--spirv-val", help="path to spirv-val")
    parser.add_argument("--stamp", help="optional build stamp written after success")
    args = parser.parse_args()

    validate_inventory()
    validator = find_sdk_tool("spirv-val", args.spirv_val)

    if args.check:
        module_count = 0
        for output_header, shaders in SHADER_GROUPS:
            if not output_header.is_file():
                raise SystemExit(f"missing native Vulkan embedded shader header: {output_header}")
            header = output_header.read_text(encoding="utf-8", errors="strict")
            symbols = tuple(str(entry[1]) for entry in shaders)
            modules = embedded_modules(header, symbols)
            for entry in shaders:
                symbol = str(entry[1])
                blob = modules[symbol]
                verify_provenance(blob, module_provenance(entry), symbol)
                validate_spirv(validator, blob, symbol)
                module_count += 1
            if header != render_header(shaders, modules):
                raise SystemExit(
                    "native Vulkan embedded shader header is stale or noncanonical: "
                    f"{output_header.relative_to(REPO_ROOT)}; "
                    "run tools/gen_vk_world_spv.py --write"
                )
        if args.stamp:
            atomic_write(Path(args.stamp).resolve(), "ok\n")
        print(
            f"native Vulkan shaders: fresh; validated {module_count} embedded modules "
            f"across {len(SHADER_GROUPS)} headers"
        )
        return 0

    compiler = find_sdk_tool("glslangValidator", args.glslang)

    rendered_headers: dict[Path, str] = {}
    module_count = 0
    for output_header, shaders in SHADER_GROUPS:
        modules: dict[str, bytes] = {}
        for entry in shaders:
            source_name, symbol, *rest = entry
            defines = rest[0] if rest else ()
            source = SHADER_DIR / source_name
            if not source.is_file():
                raise SystemExit(f"missing shader source: {source}")
            blob = compile_shader(compiler, source, defines)
            blob = inject_provenance(blob, module_provenance(entry))
            validate_spirv(validator, blob, symbol)
            module_count += 1
            modules[str(symbol)] = blob
            print(f"{source_name}: {len(blob)} bytes -> {symbol}")
        rendered_headers[output_header] = render_header(shaders, modules)

    publish_headers_atomically(rendered_headers)
    for output_header in rendered_headers:
        print(f"wrote {output_header}")
    if args.stamp:
        atomic_write(Path(args.stamp).resolve(), "ok\n")
    print(
        f"native Vulkan shaders: regenerated; validated {module_count} modules "
        f"across {len(rendered_headers)} headers"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
