import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path
from unittest import mock

import pytest

from tools import run_q2proto_compatibility as compatibility


def _target(name: str, build_dir: Path, **overrides: object) -> dict[str, object]:
    target: dict[str, object] = {
        "name": name,
        "type": "executable",
        "build_by_default": True,
        "filename": [str(build_dir / name)],
    }
    target.update(overrides)
    return target


def test_commands_enable_fail_closed_configuration_and_build_every_target(
    tmp_path: Path,
) -> None:
    source = tmp_path / "q2proto" / "tests"
    build = tmp_path / "build"

    configure = compatibility._configure_command("meson", source, build)
    compile_command = compatibility._compile_command(
        "meson", build, 2, compatibility.EXPECTED_TARGETS
    )

    assert "--fatal-meson-warnings" in configure
    assert "--wrap-mode=nodownload" in configure
    assert "-Dwerror=true" in configure
    assert configure[-2:] == [str(build), str(source)]
    assert compile_command[:7] == [
        "meson",
        "compile",
        "-C",
        str(build),
        "--jobs",
        "2",
        *compatibility.EXPECTED_TARGETS[:1],
    ]
    assert compile_command[6:] == list(compatibility.EXPECTED_TARGETS)


def test_target_inventory_requires_all_nine_default_executables(
    tmp_path: Path,
) -> None:
    build = tmp_path / "build"
    targets = [_target(name, build) for name in compatibility.EXPECTED_TARGETS]

    observed, by_name = compatibility._validate_targets(targets, build)

    assert observed == list(compatibility.EXPECTED_TARGETS)
    assert set(by_name) == set(compatibility.EXPECTED_TARGETS)

    with pytest.raises(compatibility.CompatibilityError, match="missing"):
        compatibility._validate_targets(targets[:-1], build)

    with pytest.raises(compatibility.CompatibilityError, match="unexpected"):
        compatibility._validate_targets(
            [*targets, _target("partial_or_unreviewed_fixture", build)], build
        )

    non_default = list(targets)
    non_default[0] = _target(
        compatibility.EXPECTED_TARGETS[0], build, build_by_default=False
    )
    with pytest.raises(compatibility.CompatibilityError, match="not built by default"):
        compatibility._validate_targets(non_default, build)


def test_target_outputs_must_remain_inside_build_directory(tmp_path: Path) -> None:
    build = tmp_path / "build"
    targets = [_target(name, build) for name in compatibility.EXPECTED_TARGETS]
    targets[0] = _target(
        compatibility.EXPECTED_TARGETS[0],
        build,
        filename=[str(tmp_path / "escaped")],
    )

    with pytest.raises(compatibility.CompatibilityError, match="escapes"):
        compatibility._validate_targets(targets, build)


def test_vendor_snapshot_detects_content_and_link_target_changes(
    tmp_path: Path,
) -> None:
    vendor = tmp_path / "q2proto"
    vendor.mkdir()
    source = vendor / "fixture.c"
    source.write_text("first\n", encoding="utf-8")

    before = compatibility._snapshot_tree(vendor)
    source.write_text("second\n", encoding="utf-8")
    after = compatibility._snapshot_tree(vendor)

    assert before["sha256"] != after["sha256"]
    assert before["regular_files"] == after["regular_files"] == 1


def test_collect_products_records_exact_size_and_digest(tmp_path: Path) -> None:
    build = tmp_path / "build"
    build.mkdir()
    targets = {
        name: _target(name, build) for name in compatibility.EXPECTED_TARGETS
    }
    for index, name in enumerate(compatibility.EXPECTED_TARGETS):
        (build / name).write_bytes(f"fixture-{index}".encode("ascii"))

    products = compatibility._collect_products(targets, build)

    assert [product["target"] for product in products] == list(
        compatibility.EXPECTED_TARGETS
    )
    first_bytes = b"fixture-0"
    assert products[0]["size"] == len(first_bytes)
    assert products[0]["sha256"] == hashlib.sha256(first_bytes).hexdigest()


def test_timed_out_command_writes_an_uploadable_failure_log(
    tmp_path: Path,
) -> None:
    process = mock.Mock()
    process.pid = 1234
    process.returncode = -9
    process.communicate.side_effect = [
        subprocess.TimeoutExpired(cmd=["meson", "compile"], timeout=1),
        ("partial output\n", None),
    ]
    with (
        mock.patch.object(compatibility, "_start_process", return_value=process),
        mock.patch.object(compatibility, "_terminate_process_tree") as terminate,
    ):
        step, output = compatibility._run_command(
            "compile",
            ["meson", "compile"],
            tmp_path,
            1,
            echo_output=False,
            env={},
        )

    terminate.assert_called_once_with(process)
    assert step["timed_out"] is True
    assert step["returncode"] == -9
    assert "partial output" in output
    assert "shared compatibility timeout" in output
    assert (tmp_path / "compile.log").read_text(encoding="utf-8") == output


def test_posix_cleanup_terminates_and_then_kills_the_whole_session() -> None:
    process = mock.Mock()
    process.pid = 4321
    process.wait.return_value = 0
    sigkill = getattr(compatibility.signal, "SIGKILL", 9)
    with (
        mock.patch.object(compatibility.os, "killpg", create=True) as kill_group,
        mock.patch.object(
            compatibility.signal, "SIGKILL", sigkill, create=True
        ),
        mock.patch.object(
            compatibility, "_posix_group_exists", side_effect=[True, False]
        ),
    ):
        compatibility._terminate_posix_process_tree(process)

    assert kill_group.call_args_list == [
        mock.call(4321, compatibility.signal.SIGTERM),
        mock.call(4321, sigkill),
    ]
    assert process.wait.call_count == 2


def test_posix_launch_uses_a_new_session_and_detached_input() -> None:
    process = mock.Mock()
    with (
        mock.patch.object(compatibility.os, "name", "posix"),
        mock.patch.object(
            compatibility.subprocess, "Popen", return_value=process
        ) as popen,
    ):
        assert compatibility._start_process(["meson", "compile"], {}) is process

    kwargs = popen.call_args.kwargs
    assert kwargs["start_new_session"] is True
    assert kwargs["stdin"] is subprocess.DEVNULL


def test_windows_launch_uses_kill_on_close_job_tracking() -> None:
    process = mock.Mock()
    with (
        mock.patch.object(compatibility.os, "name", "nt"),
        mock.patch.object(
            compatibility.headless_process,
            "start_headless_process",
            return_value=process,
        ) as start,
    ):
        assert compatibility._start_process(["meson", "compile"], {}) is process

    assert start.call_args.kwargs["stdin"] is subprocess.DEVNULL


@pytest.mark.skipif(os.name == "nt", reason="POSIX process groups are unavailable")
def test_timeout_integration_leaves_no_posix_descendant_group(tmp_path: Path) -> None:
    child_script = tmp_path / "spawn_descendant.py"
    identity_path = tmp_path / "process-identity.json"
    child_script.write_text(
        "\n".join(
            (
                "import json",
                "import os",
                "import pathlib",
                "import subprocess",
                "import sys",
                "import time",
                "child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)'])",
                "pathlib.Path(sys.argv[1]).write_text(json.dumps({'group': os.getpgrp(), 'child': child.pid}))",
                "time.sleep(60)",
            )
        )
        + "\n",
        encoding="utf-8",
    )

    step, _ = compatibility._run_command(
        "timeout-tree",
        [sys.executable, str(child_script), str(identity_path)],
        tmp_path,
        1,
        echo_output=False,
        env=os.environ,
    )

    identity = json.loads(identity_path.read_text(encoding="utf-8"))
    assert step["timed_out"] is True
    assert compatibility._posix_group_exists(identity["group"]) is False


def test_paths_reject_writes_inside_vendor_or_overlapping_results(
    tmp_path: Path,
) -> None:
    vendor = tmp_path / "q2proto"
    source = vendor / "tests"
    source.mkdir(parents=True)
    (source / "meson.build").write_text("project('fixture', 'c')\n")

    assert (
        compatibility.main(
            [
                "--vendor-dir",
                str(vendor),
                "--source-dir",
                str(source),
                "--build-dir",
                str(vendor / "build"),
                "--results-dir",
                str(tmp_path / "results"),
            ]
        )
        == 1
    )
    assert (
        compatibility.main(
            [
                "--vendor-dir",
                str(vendor),
                "--source-dir",
                str(source),
                "--build-dir",
                str(tmp_path / "output"),
                "--results-dir",
                str(tmp_path / "output" / "results"),
            ]
        )
        == 1
    )


def test_report_schema_is_json_serializable() -> None:
    report = {
        "schema_version": 1,
        "status": "passed",
        "expected_targets": list(compatibility.EXPECTED_TARGETS),
    }
    assert json.loads(json.dumps(report)) == report
