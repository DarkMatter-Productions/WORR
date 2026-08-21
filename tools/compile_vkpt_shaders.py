#!/usr/bin/env python3
"""Compile the exact VKPT shader inventory and validate every SPIR-V module."""

from __future__ import annotations

import argparse
import glob
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from tools.atomic_directory import (  # noqa: E402
    publish_staged_directory_atomically,
    remove_tree_with_readonly_retry,
)


RT_EXTS = frozenset({".rgen", ".rchit", ".rahit", ".rmiss", ".rint"})
STAGE_EXTS = frozenset({".comp", ".vert", ".frag", *RT_EXTS})
VKPT_TARGET_ENV = "vulkan1.2"
OUTPUT_DIRECTORY_NAME = "shader_vkpt"
OWNERSHIP_MARKER = ".worr-vkpt-shader-output"
OWNERSHIP_MARKER_CONTENT = b"WORR owned VKPT shader output v1\n"
SHADER_SOURCES = (
    "animate_materials.comp",
    "asvgf_atrous.comp",
    "asvgf_gradient_atrous.comp",
    "asvgf_gradient_img.comp",
    "asvgf_gradient_reproject.comp",
    "asvgf_lf.comp",
    "asvgf_taau.comp",
    "asvgf_temporal.comp",
    "bloom_blur.comp",
    "bloom_composite.comp",
    "bloom_downscale.comp",
    "checkerboard_interleave.comp",
    "compositing.comp",
    "debug_line.frag",
    "debug_line.vert",
    "direct_lighting.rgen",
    "final_blit.frag",
    "final_blit.vert",
    "fsr_easu_fp16.comp",
    "fsr_easu_fp32.comp",
    "fsr_rcas_fp16.comp",
    "fsr_rcas_fp32.comp",
    "god_rays.comp",
    "god_rays_filter.comp",
    "indirect_lighting.rgen",
    "instance_geometry.comp",
    "normalize_normal_map.comp",
    "path_tracer.rchit",
    "path_tracer.rmiss",
    "path_tracer_beam.rahit",
    "path_tracer_beam.rint",
    "path_tracer_explosion.rahit",
    "path_tracer_masked.rahit",
    "path_tracer_particle.rahit",
    "path_tracer_sprite.rahit",
    "physical_sky.comp",
    "physical_sky_space.comp",
    "primary_rays.rgen",
    "reflect_refract.rgen",
    "rmlui.vert",
    "shadow_map.vert",
    "sky_buffer_resolve.comp",
    "stretch_pic.frag",
    "stretch_pic.vert",
    "tone_mapping_apply.comp",
    "tone_mapping_curve.comp",
    "tone_mapping_histogram.comp",
)
INCLUDE_DEPENDENCIES = (
    "fsr/ffx_a.h",
    "fsr/ffx_fsr1.h",
    "shader/asvgf.glsl",
    "shader/brdf.glsl",
    "shader/constants.h",
    "shader/fsr_easu.glsl",
    "shader/fsr_rcas.glsl",
    "shader/fsr_utils.glsl",
    "shader/global_textures.h",
    "shader/global_ubo.h",
    "shader/god_rays_shared.h",
    "shader/light_lists.h",
    "shader/path_tracer.h",
    "shader/path_tracer_hit_shaders.h",
    "shader/path_tracer_rgen.h",
    "shader/path_tracer_transparency.glsl",
    "shader/precomputed_sky.glsl",
    "shader/precomputed_sky_params.h",
    "shader/projection.glsl",
    "shader/shader_structs.h",
    "shader/sky.h",
    "shader/tiny_encryption_algorithm.h",
    "shader/tone_mapping_utils.glsl",
    "shader/utils.glsl",
    "shader/vertex_buffer.h",
    "shader/water.glsl",
)


def find_sdk_tool(name: str, explicit_path: str | None) -> Path:
    executable = f"{name}.exe" if os.name == "nt" else name
    candidates: list[Path] = []
    if explicit_path:
        candidates.append(Path(explicit_path))
    sdk = os.environ.get("VULKAN_SDK")
    if sdk:
        candidates.extend((Path(sdk) / "Bin" / executable, Path(sdk) / "bin" / executable))
    if os.name == "nt":
        candidates.extend(
            Path(path) / f"{name}.exe"
            for path in sorted(glob.glob(r"C:\VulkanSDK\*\Bin"), reverse=True)
        )
    discovered = shutil.which(executable) or shutil.which(name)
    if discovered:
        candidates.append(Path(discovered))
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    raise SystemExit(
        f"vkpt shaders: required tool {name} was not found; install the Vulkan SDK "
        f"or pass {'--glslang' if name == 'glslangValidator' else '--spirv-val'}"
    )


def expected_output_names() -> tuple[str, ...]:
    outputs: list[str] = []
    for source_name in SHADER_SOURCES:
        suffix = Path(source_name).suffix
        if suffix in RT_EXTS:
            outputs.append(f"{source_name}.pipeline.spv")
            if suffix == ".rgen":
                outputs.append(f"{source_name}.query.spv")
        else:
            outputs.append(f"{source_name}.spv")
    return tuple(outputs)


def parse_runtime_shader_lists(vkpt_header: Path) -> dict[str, tuple[str, ...]]:
    text = vkpt_header.read_text(encoding="utf-8", errors="strict")
    result = {}
    for macro in (
        "LIST_SHADER_MODULES",
        "LIST_RT_RGEN_SHADER_MODULES",
        "LIST_RT_PIPELINE_SHADER_MODULES",
    ):
        match = re.search(
            rf"#define\s+{macro}\s+\\\n(?P<body>(?:[^\n]*\\\n)+)",
            text,
        )
        if match is None:
            raise SystemExit(f"vkpt runtime shader inventory is missing {macro}")
        enum_names = re.findall(r"SHADER_MODULE_DO\(QVK_MOD_([A-Z0-9_]+)\)", match.group("body"))
        sources = []
        for enum_name in enum_names:
            base, separator, extension = enum_name.lower().rpartition("_")
            if not separator:
                raise SystemExit(f"vkpt runtime shader enum has no stage suffix: {enum_name}")
            sources.append(f"{base}.{extension}")
        result[macro] = tuple(sources)
    return result


def validate_runtime_inventory(vkpt_root: Path) -> None:
    runtime = parse_runtime_shader_lists(vkpt_root / "vkpt.h")
    expected = {
        "LIST_SHADER_MODULES": tuple(
            name for name in SHADER_SOURCES if Path(name).suffix not in RT_EXTS
        ),
        "LIST_RT_RGEN_SHADER_MODULES": tuple(
            name for name in SHADER_SOURCES if Path(name).suffix == ".rgen"
        ),
        "LIST_RT_PIPELINE_SHADER_MODULES": tuple(
            name for name in SHADER_SOURCES if Path(name).suffix in RT_EXTS - {".rgen"}
        ),
    }
    failures = []
    for macro, expected_sources in expected.items():
        actual_sources = runtime[macro]
        missing = sorted(set(expected_sources) - set(actual_sources))
        extra = sorted(set(actual_sources) - set(expected_sources))
        if missing:
            failures.append(f"{macro} missing: {', '.join(missing)}")
        if extra:
            failures.append(f"{macro} unlisted: {', '.join(extra)}")
        if not missing and not extra and len(actual_sources) != len(set(actual_sources)):
            failures.append(f"{macro} contains duplicate entries")
    if failures:
        raise SystemExit("vkpt runtime shader inventory mismatch; " + "; ".join(failures))


def validate_source_inventory(vkpt_root: Path) -> None:
    shader_dir = vkpt_root / "shader"
    fsr_dir = vkpt_root / "fsr"
    actual_sources = {
        path.name for path in shader_dir.iterdir() if path.is_file() and path.suffix in STAGE_EXTS
    }
    expected_sources = set(SHADER_SOURCES)
    actual_dependencies = {
        f"shader/{path.name}"
        for path in shader_dir.iterdir()
        if path.is_file() and path.suffix in {".glsl", ".h"}
    }
    actual_dependencies.update(
        f"fsr/{path.name}" for path in fsr_dir.iterdir() if path.is_file() and path.suffix == ".h"
    )
    expected_dependencies = set(INCLUDE_DEPENDENCIES)
    failures = []
    for label, expected, actual in (
        ("shader sources", expected_sources, actual_sources),
        ("shader includes", expected_dependencies, actual_dependencies),
    ):
        missing = sorted(expected - actual)
        extra = sorted(actual - expected)
        if missing:
            failures.append(f"missing {label}: {', '.join(missing)}")
        if extra:
            failures.append(f"unlisted {label}: {', '.join(extra)}")
    if failures:
        raise SystemExit("vkpt shader inventory mismatch; " + "; ".join(failures))
    validate_runtime_inventory(vkpt_root)


def is_reparse_point(metadata: os.stat_result) -> bool:
    attributes = getattr(metadata, "st_file_attributes", 0)
    reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
    return bool(attributes & reparse_flag)


def lexical_absolute(path: Path) -> Path:
    return Path(os.path.abspath(os.fspath(path)))


def validate_no_follow_directory_ancestors(path: Path) -> None:
    chain = tuple(reversed((path, *path.parents)))
    for ancestor in chain:
        try:
            metadata = ancestor.lstat()
        except FileNotFoundError:
            continue
        if (
            not stat.S_ISDIR(metadata.st_mode)
            or ancestor.is_symlink()
            or is_reparse_point(metadata)
        ):
            raise SystemExit(
                f"vkpt shaders: output ancestor is a link, reparse point, or special path: {ancestor}"
            )


def path_is_within(path: Path, root: Path) -> bool:
    try:
        return os.path.normcase(os.path.commonpath((path, root))) == os.path.normcase(str(root))
    except ValueError:
        return False


def read_regular_file_no_follow(path: Path) -> bytes:
    flags = os.O_RDONLY | getattr(os, "O_BINARY", 0) | getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(path, flags)
    try:
        metadata = os.fstat(descriptor)
        if (
            not stat.S_ISREG(metadata.st_mode)
            or is_reparse_point(metadata)
            or metadata.st_nlink != 1
        ):
            raise SystemExit(f"vkpt shader output contains a special file: {path.name}")
        chunks = []
        while True:
            chunk = os.read(descriptor, 65536)
            if not chunk:
                return b"".join(chunks)
            chunks.append(chunk)
    finally:
        os.close(descriptor)


def inspect_output_directory(output_dir: Path, *, require_complete: bool) -> set[str]:
    if output_dir.name != OUTPUT_DIRECTORY_NAME:
        raise SystemExit(
            f"vkpt shaders: --out-dir basename must be {OUTPUT_DIRECTORY_NAME}"
        )
    validate_no_follow_directory_ancestors(output_dir.parent)
    try:
        root_metadata = output_dir.lstat()
    except FileNotFoundError:
        if require_complete:
            raise SystemExit(f"vkpt shader output directory is missing: {output_dir}")
        return set()
    if (
        not stat.S_ISDIR(root_metadata.st_mode)
        or output_dir.is_symlink()
        or is_reparse_point(root_metadata)
    ):
        raise SystemExit(f"vkpt shaders: refusing link, reparse, or special output root: {output_dir}")

    entries: set[str] = set()
    with os.scandir(output_dir) as iterator:
        for entry in iterator:
            entry_path = Path(entry.path)
            metadata = entry_path.lstat()
            if (
                entry_path.is_symlink()
                or is_reparse_point(metadata)
                or not stat.S_ISREG(metadata.st_mode)
                or metadata.st_nlink != 1
            ):
                raise SystemExit(
                    f"vkpt shader output must be a flat regular-file tree: {entry.name}"
                )
            entries.add(entry.name)

    if not entries:
        if require_complete:
            raise SystemExit(f"vkpt shader output directory is empty: {output_dir}")
        return entries
    if OWNERSHIP_MARKER not in entries:
        raise SystemExit(
            f"vkpt shaders: refusing unowned non-empty output directory: {output_dir}"
        )
    if read_regular_file_no_follow(output_dir / OWNERSHIP_MARKER) != OWNERSHIP_MARKER_CONTENT:
        raise SystemExit(f"vkpt shaders: output ownership marker is invalid: {output_dir}")

    if require_complete:
        expected_entries = set(expected_output_names()) | {OWNERSHIP_MARKER}
        missing = sorted(expected_entries - entries)
        extra = sorted(entries - expected_entries)
        if missing or extra:
            details = []
            if missing:
                details.append("missing outputs: " + ", ".join(missing))
            if extra:
                details.append("extra outputs: " + ", ".join(extra))
            raise SystemExit("vkpt shader output inventory mismatch; " + "; ".join(details))
    return entries


def run_checked(command: list[str], description: str) -> None:
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise SystemExit(f"{description} failed with exit code {result.returncode}")


def compile_module(
    glslang: Path,
    validator: Path,
    source: Path,
    output: Path,
    includes: tuple[Path, ...],
    defines: tuple[str, ...] = (),
    stage: str | None = None,
) -> None:
    command = [
        str(glslang),
        "--target-env",
        VKPT_TARGET_ENV,
        "--quiet",
        "-DVKPT_SHADER",
        "-V",
    ]
    if stage:
        command.extend(("-S", stage))
    command.extend(f"-I{include}" for include in includes)
    command.extend(defines)
    command.extend((str(source), "-o", str(output)))
    run_checked(command, f"glslangValidator for {output.name}")
    run_checked(
        [str(validator), "--target-env", VKPT_TARGET_ENV, str(output)],
        f"spirv-val for {output.name}",
    )


def atomic_write_stamp(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="ascii", newline="\n") as handle:
            handle.write("ok\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary_name, path)
    finally:
        Path(temporary_name).unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", required=True)
    parser.add_argument("--glslang", help="Path to glslangValidator")
    parser.add_argument("--spirv-val", help="Path to spirv-val")
    parser.add_argument("--stamp")
    parser.add_argument(
        "--check",
        action="store_true",
        help="read-only exact output inventory and SPIR-V validation",
    )
    args = parser.parse_args()

    vkpt_root = REPO_ROOT / "src" / "rend_rtx" / "vkpt"
    validate_source_inventory(vkpt_root)
    requested_output = Path(args.out_dir)
    output_dir = lexical_absolute(requested_output)
    stamp_path = lexical_absolute(Path(args.stamp)) if args.stamp else None
    if stamp_path is not None and path_is_within(stamp_path, output_dir):
        raise SystemExit("vkpt shaders: --stamp must not be inside --out-dir")
    inspect_output_directory(output_dir, require_complete=args.check)
    validator = find_sdk_tool("spirv-val", args.spirv_val)
    if args.check:
        for output_name in expected_output_names():
            output = output_dir / output_name
            run_checked(
                [str(validator), "--target-env", VKPT_TARGET_ENV, str(output)],
                f"spirv-val for {output_name}",
            )
        if stamp_path is not None:
            atomic_write_stamp(stamp_path)
        print(f"vkpt shaders: exact owned output; validated {len(expected_output_names())} modules")
        return 0

    glslang = find_sdk_tool("glslangValidator", args.glslang)
    shader_dir = vkpt_root / "shader"
    includes = (shader_dir, vkpt_root / "fsr")
    output_dir.parent.mkdir(parents=True, exist_ok=True)
    staged_dir = Path(tempfile.mkdtemp(prefix=f".{output_dir.name}.stage.", dir=output_dir.parent))
    completed = False
    try:
        for source_name in SHADER_SOURCES:
            source = shader_dir / source_name
            suffix = source.suffix
            if suffix in RT_EXTS:
                compile_module(
                    glslang,
                    validator,
                    source,
                    staged_dir / f"{source_name}.pipeline.spv",
                    includes,
                )
                if suffix == ".rgen":
                    compile_module(
                        glslang,
                        validator,
                        source,
                        staged_dir / f"{source_name}.query.spv",
                        includes,
                        ("-DKHR_RAY_QUERY",),
                        "comp",
                    )
            else:
                compile_module(
                    glslang,
                    validator,
                    source,
                    staged_dir / f"{source_name}.spv",
                    includes,
                )
        (staged_dir / OWNERSHIP_MARKER).write_bytes(OWNERSHIP_MARKER_CONTENT)
        actual_outputs = tuple(sorted(path.name for path in staged_dir.iterdir()))
        expected_outputs = tuple(sorted((*expected_output_names(), OWNERSHIP_MARKER)))
        if actual_outputs != expected_outputs:
            raise SystemExit("vkpt shader output inventory mismatch")
        inspect_output_directory(output_dir, require_complete=False)
        publish_staged_directory_atomically(staged_dir, output_dir)
        completed = True
    finally:
        if not completed and staged_dir.exists():
            remove_tree_with_readonly_retry(staged_dir)

    if stamp_path is not None:
        atomic_write_stamp(stamp_path)
    print(
        f"vkpt shaders: compiled and validated {len(expected_output_names())} modules "
        f"from {len(SHADER_SOURCES)} sources"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
