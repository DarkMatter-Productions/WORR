from __future__ import annotations

import os
import pathlib
import re
import stat
import struct
import sys
from unittest import mock

import pytest

from tools import compile_vkpt_shaders as vkpt
from tools import gen_vk_world_spv as native_vk


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]


def meson_file_entries(text: str, variable: str) -> set[str]:
    match = re.search(
        rf"{re.escape(variable)}\s*=\s*files\((.*?)\n\s*\)",
        text,
        re.DOTALL,
    )
    assert match is not None, f"missing Meson inventory {variable}"
    return set(re.findall(r"'([^']+)'", match.group(1)))


def minimal_named_spirv() -> bytes:
    words = [
        native_vk.SPIRV_MAGIC,
        0x00010000,
        0,
        2,
        0,
        (3 << 16) | native_vk.OP_NAME,
        1,
        0,
    ]
    return struct.pack(f"<{len(words)}I", *words)


def create_native_fixture(tmp_path: pathlib.Path):
    shader_dir = tmp_path / "src" / "rend_vk" / "shaders"
    shader_dir.mkdir(parents=True)
    source = shader_dir / "one.vert"
    source.write_text("#version 450\nvoid main() {}\n", encoding="utf-8")
    header = tmp_path / "src" / "rend_vk" / "one_spv.h"
    groups = ((header, (("one.vert", "one_spv"),)),)
    entry = groups[0][1][0]
    with mock.patch.object(native_vk, "SHADER_DIR", shader_dir):
        blob = native_vk.inject_provenance(
            minimal_named_spirv(), native_vk.module_provenance(entry)
        )
    return shader_dir, source, header, groups, blob


def create_vkpt_source_fixture(tmp_path: pathlib.Path) -> pathlib.Path:
    vkpt_root = tmp_path / "src" / "rend_rtx" / "vkpt"
    shader_dir = vkpt_root / "shader"
    fsr_dir = vkpt_root / "fsr"
    shader_dir.mkdir(parents=True)
    fsr_dir.mkdir()
    for name in vkpt.SHADER_SOURCES:
        (shader_dir / name).write_text("shader\n", encoding="utf-8")
    for relative in vkpt.INCLUDE_DEPENDENCIES:
        path = vkpt_root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("include\n", encoding="utf-8")
    (vkpt_root / "vkpt.h").write_text(
        (REPO_ROOT / "src" / "rend_rtx" / "vkpt" / "vkpt.h").read_text(encoding="utf-8"),
        encoding="utf-8",
    )
    return vkpt_root


def test_shader_source_header_and_include_inventories_are_exact_and_shared_with_meson() -> None:
    native_vk.validate_inventory()
    vkpt.validate_source_inventory(REPO_ROOT / "src" / "rend_rtx" / "vkpt")
    meson = (REPO_ROOT / "meson.build").read_text(encoding="utf-8")

    expected_native = {
        f"src/rend_vk/shaders/{name}" for name in native_vk.expected_shader_sources()
    }
    expected_headers = {
        str(path.relative_to(REPO_ROOT)).replace("\\", "/")
        for path, _ in native_vk.SHADER_GROUPS
    }
    assert meson_file_entries(meson, "native_vk_shader_sources") == expected_native
    assert meson_file_entries(meson, "native_vk_shader_headers") == expected_headers
    assert len(expected_native) == 28
    assert sum(len(shaders) for _, shaders in native_vk.SHADER_GROUPS) == 48
    assert len(native_vk.SHADER_GROUPS) == 8

    ordinary = {name for name in vkpt.SHADER_SOURCES if pathlib.Path(name).suffix not in vkpt.RT_EXTS}
    ray_tracing = set(vkpt.SHADER_SOURCES) - ordinary
    assert meson_file_entries(meson, "vkpt_shader_sources") == {
        f"src/rend_rtx/vkpt/shader/{name}" for name in ordinary
    }
    assert meson_file_entries(meson, "vkpt_shader_rt_sources") == {
        f"src/rend_rtx/vkpt/shader/{name}" for name in ray_tracing
    }
    expected_includes = {
        f"src/rend_rtx/vkpt/{name}" for name in vkpt.INCLUDE_DEPENDENCIES
    }
    assert meson_file_entries(meson, "vkpt_shader_includes") == expected_includes
    assert len(vkpt.SHADER_SOURCES) == 47
    assert len(vkpt.expected_output_names()) == 51
    assert len(set(vkpt.expected_output_names())) == 51


def test_vkpt_script_inventory_matches_runtime_shader_lists_exactly() -> None:
    runtime = vkpt.parse_runtime_shader_lists(REPO_ROOT / "src" / "rend_rtx" / "vkpt" / "vkpt.h")
    ordinary = {name for name in vkpt.SHADER_SOURCES if pathlib.Path(name).suffix not in vkpt.RT_EXTS}
    rgen = {name for name in vkpt.SHADER_SOURCES if pathlib.Path(name).suffix == ".rgen"}
    pipeline = set(vkpt.SHADER_SOURCES) - ordinary - rgen
    assert set(runtime["LIST_SHADER_MODULES"]) == ordinary
    assert set(runtime["LIST_RT_RGEN_SHADER_MODULES"]) == rgen
    assert set(runtime["LIST_RT_PIPELINE_SHADER_MODULES"]) == pipeline
    runtime_outputs = {f"{name}.spv" for name in ordinary}
    runtime_outputs |= {f"{name}.pipeline.spv" for name in rgen | pipeline}
    runtime_outputs |= {f"{name}.query.spv" for name in rgen}
    assert runtime_outputs == set(vkpt.expected_output_names())


def test_native_freshness_is_read_only_and_provenance_blocks_digest_forgery(
    tmp_path: pathlib.Path,
) -> None:
    shader_dir, source, header, groups, blob = create_native_fixture(tmp_path)
    with mock.patch.object(native_vk, "REPO_ROOT", tmp_path), mock.patch.object(
        native_vk, "SHADER_DIR", shader_dir
    ), mock.patch.object(native_vk, "SHADER_GROUPS", groups):
        header.write_text(
            native_vk.render_header(groups[0][1], {"one_spv": blob}),
            encoding="utf-8",
            newline="\n",
        )
        original = header.read_bytes()
        with mock.patch.object(sys, "argv", ["gen_vk_world_spv.py", "--check"]), mock.patch.object(
            native_vk, "find_sdk_tool", return_value="spirv-val"
        ), mock.patch.object(native_vk, "validate_spirv") as validator:
            assert native_vk.main() == 0
        assert header.read_bytes() == original
        validator.assert_called_once()

        source.write_text("#version 450\nvoid main() { gl_Position.x = 1; }\n", encoding="utf-8")
        forged = re.sub(
            r"Input-SHA256: [0-9a-f]{64}",
            f"Input-SHA256: {native_vk.input_digest(groups[0][1])}",
            header.read_text(encoding="utf-8"),
        )
        header.write_text(forged, encoding="utf-8", newline="\n")
        with mock.patch.object(sys, "argv", ["gen_vk_world_spv.py", "--check"]), mock.patch.object(
            native_vk, "find_sdk_tool", return_value="spirv-val"
        ), pytest.raises(SystemExit, match="provenance mismatch"):
            native_vk.main()


def test_native_check_rejects_noncanonical_header_content(tmp_path: pathlib.Path) -> None:
    shader_dir, _source, header, groups, blob = create_native_fixture(tmp_path)
    with mock.patch.object(native_vk, "REPO_ROOT", tmp_path), mock.patch.object(
        native_vk, "SHADER_DIR", shader_dir
    ), mock.patch.object(native_vk, "SHADER_GROUPS", groups):
        canonical = native_vk.render_header(groups[0][1], {"one_spv": blob})
        header.write_text(canonical + "/* injected */\n", encoding="utf-8", newline="\n")
        with mock.patch.object(sys, "argv", ["gen_vk_world_spv.py", "--check"]), mock.patch.object(
            native_vk, "find_sdk_tool", return_value="spirv-val"
        ), mock.patch.object(native_vk, "validate_spirv"), pytest.raises(
            SystemExit, match="stale or noncanonical"
        ):
            native_vk.main()

        header.write_text(
            canonical.replace("0x07230203u", "0X07230203u", 1),
            encoding="utf-8",
            newline="\n",
        )
        with mock.patch.object(sys, "argv", ["gen_vk_world_spv.py", "--check"]), mock.patch.object(
            native_vk, "find_sdk_tool", return_value="spirv-val"
        ), pytest.raises(SystemExit):
            native_vk.main()

        header.write_text(
            canonical.replace("_size = sizeof(one_spv)", "_size = 4"),
            encoding="utf-8",
            newline="\n",
        )
        with mock.patch.object(sys, "argv", ["gen_vk_world_spv.py", "--check"]), mock.patch.object(
            native_vk, "find_sdk_tool", return_value="spirv-val"
        ), mock.patch.object(native_vk, "validate_spirv"), pytest.raises(
            SystemExit, match="stale or noncanonical"
        ):
            native_vk.main()


def test_native_hash_canonicalizes_crlf_and_provenance_is_line_aligned(
    tmp_path: pathlib.Path,
) -> None:
    shader_dir = tmp_path / "shaders"
    shader_dir.mkdir()
    source = shader_dir / "one.vert"
    entry = ("one.vert", "one_spv")
    with mock.patch.object(native_vk, "SHADER_DIR", shader_dir):
        source.write_bytes(b"first\nsecond\n")
        lf_digest = native_vk.input_digest((entry,))
        lf_provenance = native_vk.module_provenance(entry)
        source.write_bytes(b"first\r\nsecond\r\n")
        assert native_vk.input_digest((entry,)) == lf_digest
        assert native_vk.module_provenance(entry) == lf_provenance
    blob = native_vk.inject_provenance(minimal_named_spirv(), lf_provenance)
    provenance_instructions = [
        word_count
        for _, word_count, opcode, _ in native_vk.spirv_instructions(blob)
        if opcode == native_vk.OP_STRING
    ]
    assert provenance_instructions == [32]


def test_legacy_native_validate_alias_still_regenerates(tmp_path: pathlib.Path) -> None:
    shader_dir = tmp_path / "src" / "rend_vk" / "shaders"
    shader_dir.mkdir(parents=True)
    (shader_dir / "one.vert").write_text("shader\n", encoding="utf-8")
    header = tmp_path / "src" / "rend_vk" / "one_spv.h"
    groups = ((header, (("one.vert", "one_spv"),)),)
    with mock.patch.object(native_vk, "REPO_ROOT", tmp_path), mock.patch.object(
        native_vk, "SHADER_DIR", shader_dir
    ), mock.patch.object(native_vk, "SHADER_GROUPS", groups), mock.patch.object(
        native_vk, "find_sdk_tool", return_value="tool"
    ), mock.patch.object(
        native_vk, "compile_shader", return_value=minimal_named_spirv()
    ), mock.patch.object(native_vk, "validate_spirv"), mock.patch.object(
        sys, "argv", ["gen_vk_world_spv.py", "--validate"]
    ):
        assert native_vk.main() == 0
    text = header.read_text(encoding="utf-8")
    assert "Input-SHA256:" in text
    assert "static const uint32_t one_spv[]" in text


def test_native_header_set_publication_rolls_back_and_preserves_modes(
    tmp_path: pathlib.Path,
) -> None:
    first = tmp_path / "first.h"
    second = tmp_path / "second.h"
    first.write_text("old first\n", encoding="utf-8")
    second.write_text("old second\n", encoding="utf-8")
    first.chmod(0o644)
    second.chmod(0o644)
    real_replace = native_vk.os.replace
    failed = False

    def fail_second_publish(source, destination):
        nonlocal failed
        source_path = pathlib.Path(source)
        destination_path = pathlib.Path(destination)
        if destination_path == second and ".rollback." not in source_path.name and not failed:
            failed = True
            raise OSError("injected publication failure")
        return real_replace(source, destination)

    with mock.patch.object(native_vk.os, "replace", side_effect=fail_second_publish), pytest.raises(
        OSError, match="injected publication failure"
    ):
        native_vk.publish_headers_atomically({first: "new first\n", second: "new second\n"})
    assert first.read_text(encoding="utf-8") == "old first\n"
    assert second.read_text(encoding="utf-8") == "old second\n"
    if os.name != "nt":
        assert stat.S_IMODE(first.stat().st_mode) == 0o644
        assert stat.S_IMODE(second.stat().st_mode) == 0o644


def test_native_header_staging_failure_cleans_earlier_temporary_files(
    tmp_path: pathlib.Path,
) -> None:
    first = tmp_path / "first.h"
    second = tmp_path / "second.h"
    first.write_text("old first\n", encoding="utf-8")
    second.write_text("old second\n", encoding="utf-8")
    real_stage = native_vk.stage_text_file
    calls = 0

    def fail_second_stage(path, contents):
        nonlocal calls
        calls += 1
        if calls == 2:
            raise OSError("injected staging failure")
        return real_stage(path, contents)

    with mock.patch.object(native_vk, "stage_text_file", side_effect=fail_second_stage), pytest.raises(
        OSError, match="injected staging failure"
    ):
        native_vk.publish_headers_atomically({first: "new first\n", second: "new second\n"})
    assert first.read_text(encoding="utf-8") == "old first\n"
    assert second.read_text(encoding="utf-8") == "old second\n"
    assert not tuple(tmp_path.glob(".first.h.*"))


def test_vkpt_generation_replaces_stale_owned_output_atomically(tmp_path: pathlib.Path) -> None:
    create_vkpt_source_fixture(tmp_path)
    output_dir = tmp_path / "out" / "shader_vkpt"
    output_dir.mkdir(parents=True)
    (output_dir / vkpt.OWNERSHIP_MARKER).write_bytes(vkpt.OWNERSHIP_MARKER_CONTENT)
    (output_dir / "removed_shader.frag.spv").write_bytes(b"stale")
    stamp = tmp_path / "out" / "vkpt.stamp"

    def compile_fixture(*args, **kwargs) -> None:
        del kwargs
        args[3].write_bytes(b"validated-spv")

    argv = ["compile_vkpt_shaders.py", "--out-dir", str(output_dir), "--stamp", str(stamp)]
    with mock.patch.object(vkpt, "REPO_ROOT", tmp_path), mock.patch.object(
        sys, "argv", argv
    ), mock.patch.object(vkpt, "find_sdk_tool", return_value=tmp_path / "tool"), mock.patch.object(
        vkpt, "compile_module", side_effect=compile_fixture
    ) as compiler:
        assert vkpt.main() == 0

    assert compiler.call_count == 51
    assert {path.name for path in output_dir.iterdir()} == set(vkpt.expected_output_names()) | {
        vkpt.OWNERSHIP_MARKER
    }
    assert stamp.read_text(encoding="ascii") == "ok\n"


def test_vkpt_refuses_unowned_nonempty_output_without_altering_it(tmp_path: pathlib.Path) -> None:
    output_dir = tmp_path / "out" / "shader_vkpt"
    output_dir.mkdir(parents=True)
    sentinel = output_dir / "do-not-delete.txt"
    sentinel.write_bytes(b"user data")
    argv = ["compile_vkpt_shaders.py", "--out-dir", str(output_dir)]
    with mock.patch.object(vkpt, "validate_source_inventory"), mock.patch.object(
        sys, "argv", argv
    ), pytest.raises(SystemExit, match="refusing unowned non-empty"):
        vkpt.main()
    assert sentinel.read_bytes() == b"user data"
    assert {path.name for path in output_dir.iterdir()} == {sentinel.name}


def test_vkpt_rejects_wrong_basename_and_stamp_inside_output(tmp_path: pathlib.Path) -> None:
    wrong_output = tmp_path / "not-the-owned-name"
    with mock.patch.object(vkpt, "validate_source_inventory"), mock.patch.object(
        sys, "argv", ["compile_vkpt_shaders.py", "--out-dir", str(wrong_output)]
    ), pytest.raises(SystemExit, match="basename must be shader_vkpt"):
        vkpt.main()

    output_dir = tmp_path / "shader_vkpt"
    with mock.patch.object(vkpt, "validate_source_inventory"), mock.patch.object(
        sys,
        "argv",
        [
            "compile_vkpt_shaders.py",
            "--out-dir",
            str(output_dir),
            "--stamp",
            str(output_dir / "unsafe.stamp"),
        ],
    ), pytest.raises(SystemExit, match="must not be inside"):
        vkpt.main()


def test_vkpt_refuses_symbolic_link_output_root(tmp_path: pathlib.Path) -> None:
    real_output = tmp_path / "real-output"
    real_output.mkdir()
    linked_output = tmp_path / "shader_vkpt"
    try:
        linked_output.symlink_to(real_output, target_is_directory=True)
    except OSError as error:
        pytest.skip(f"directory symlinks unavailable: {error}")
    with pytest.raises(SystemExit, match="refusing link, reparse, or special output root"):
        vkpt.inspect_output_directory(linked_output, require_complete=False)


def test_vkpt_refuses_symbolic_link_output_ancestor(tmp_path: pathlib.Path) -> None:
    real_parent = tmp_path / "real-parent"
    real_parent.mkdir()
    linked_parent = tmp_path / "linked-parent"
    try:
        linked_parent.symlink_to(real_parent, target_is_directory=True)
    except OSError as error:
        pytest.skip(f"directory symlinks unavailable: {error}")
    with pytest.raises(SystemExit, match="output ancestor is a link"):
        vkpt.inspect_output_directory(linked_parent / "shader_vkpt", require_complete=False)


def test_vkpt_refuses_nested_special_entry_in_owned_output(tmp_path: pathlib.Path) -> None:
    output_dir = tmp_path / "shader_vkpt"
    output_dir.mkdir()
    (output_dir / vkpt.OWNERSHIP_MARKER).write_bytes(vkpt.OWNERSHIP_MARKER_CONTENT)
    (output_dir / "nested").mkdir()
    with pytest.raises(SystemExit, match="flat regular-file tree"):
        vkpt.inspect_output_directory(output_dir, require_complete=False)


def test_vkpt_refuses_hardlinked_entry_in_owned_output(tmp_path: pathlib.Path) -> None:
    output_dir = tmp_path / "shader_vkpt"
    output_dir.mkdir()
    marker = output_dir / vkpt.OWNERSHIP_MARKER
    marker.write_bytes(vkpt.OWNERSHIP_MARKER_CONTENT)
    try:
        os.link(marker, output_dir / "hardlink")
    except OSError as error:
        pytest.skip(f"hardlinks unavailable: {error}")
    with pytest.raises(SystemExit, match="flat regular-file tree"):
        vkpt.inspect_output_directory(output_dir, require_complete=False)


def test_vkpt_read_only_check_validates_every_exact_owned_output(tmp_path: pathlib.Path) -> None:
    output_dir = tmp_path / "shader_vkpt"
    output_dir.mkdir()
    (output_dir / vkpt.OWNERSHIP_MARKER).write_bytes(vkpt.OWNERSHIP_MARKER_CONTENT)
    for output_name in vkpt.expected_output_names():
        (output_dir / output_name).write_bytes(b"spirv")
    stamp = tmp_path / "check.stamp"
    argv = [
        "compile_vkpt_shaders.py",
        "--out-dir",
        str(output_dir),
        "--check",
        "--stamp",
        str(stamp),
    ]
    commands = []
    with mock.patch.object(vkpt, "validate_source_inventory"), mock.patch.object(
        vkpt, "find_sdk_tool", return_value=pathlib.Path("spirv-val")
    ), mock.patch.object(vkpt, "run_checked", side_effect=lambda command, _description: commands.append(command)), mock.patch.object(
        sys, "argv", argv
    ):
        assert vkpt.main() == 0
    assert len(commands) == 51
    assert all(command[1:3] == ["--target-env", vkpt.VKPT_TARGET_ENV] for command in commands)
    assert stamp.read_text(encoding="ascii") == "ok\n"

    (output_dir / vkpt.expected_output_names()[0]).unlink()
    with mock.patch.object(vkpt, "validate_source_inventory"), mock.patch.object(
        sys, "argv", argv
    ), pytest.raises(SystemExit, match="missing outputs"):
        vkpt.main()


def test_spirv_validation_uses_the_compilation_target_environment(tmp_path: pathlib.Path) -> None:
    source = tmp_path / "shader.comp"
    source.write_text("shader\n", encoding="utf-8")
    output = tmp_path / "shader.spv"
    commands: list[list[str]] = []
    with mock.patch.object(vkpt, "run_checked", side_effect=lambda command, _description: commands.append(command)):
        vkpt.compile_module(
            pathlib.Path("glslangValidator"),
            pathlib.Path("spirv-val"),
            source,
            output,
            (tmp_path,),
        )
    assert commands[0][1:3] == ["--target-env", vkpt.VKPT_TARGET_ENV]
    assert commands[1][1:3] == ["--target-env", vkpt.VKPT_TARGET_ENV]

    with mock.patch.object(native_vk.subprocess, "run", return_value=mock.Mock(returncode=0)) as run:
        native_vk.validate_spirv("spirv-val", b"\x03\x02\x23\x07", "module")
    assert run.call_args.args[0][1:3] == ["--target-env", native_vk.NATIVE_TARGET_ENV]


def test_meson_and_pr_ci_make_vulkan_shader_checks_mandatory() -> None:
    meson = (REPO_ROOT / "meson.build").read_text(encoding="utf-8")
    workflow = (REPO_ROOT / ".github" / "workflows" / "pull-request.yml").read_text(
        encoding="utf-8"
    )
    assert "'native_vk_shader_freshness'" in meson
    assert "'vkpt_shaders'" in meson
    assert "'vkpt_shader_validation'" in meson
    assert "build_always_stale: true" in meson
    assert "'src/rend_rtx/vkpt/vkpt.h'" in meson
    native_target = meson[meson.index("'native_vk_shader_freshness'"):meson.index("vkpt_shader_sources = files(")]
    assert "'tools/atomic_directory.py'" in native_target
    assert "link_depends:          native_vk_shader_target" in meson
    assert "link_depends:          vkpt_shader_validation_target" in meson

    assert "  renderer-vulkan:" in workflow
    assert "  renderer-rtx:" in workflow
    assert "renderer-library:" not in workflow
    assert "meson compile -C builddir-vulkan native_vk_shader_freshness" in workflow
    assert "meson compile -C builddir-rtx vkpt_shaders vkpt_shader_validation" in workflow
    assert "glslang-tools spirv-tools" in workflow
    for workflow_name in ("pull-request.yml", "release.yml", "nightly.yml"):
        text = (REPO_ROOT / ".github" / "workflows" / workflow_name).read_text(
            encoding="utf-8"
        )
        assert "spirv-tools" in text


def test_shader_extensions_are_forced_to_lf_in_git_attributes() -> None:
    attributes = (REPO_ROOT / ".gitattributes").read_text(encoding="utf-8")
    for extension in ("vert", "frag", "comp", "glsl", "rgen", "rchit", "rahit", "rmiss", "rint"):
        assert f"*.{extension} text eol=lf" in attributes


def test_missing_shader_tools_fail_closed_with_actionable_flags() -> None:
    with mock.patch.dict(vkpt.os.environ, {}, clear=True), mock.patch.object(
        vkpt.glob, "glob", return_value=[]
    ), mock.patch.object(vkpt.shutil, "which", return_value=None), pytest.raises(
        SystemExit, match="--glslang"
    ):
        vkpt.find_sdk_tool("glslangValidator", None)
    with mock.patch.dict(native_vk.os.environ, {}, clear=True), mock.patch.object(
        native_vk.glob, "glob", return_value=[]
    ), mock.patch.object(native_vk.shutil, "which", return_value=None), pytest.raises(
        SystemExit, match="--spirv-val"
    ):
        native_vk.find_sdk_tool("spirv-val", None)
