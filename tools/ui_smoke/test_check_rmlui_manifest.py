from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import pytest

from tools.ui_smoke.check_rmlui_manifest import EXPECTED_SCHEMA, main


@pytest.fixture
def repo_root(tmp_path: Path) -> Path:
    root = tmp_path / "repo"
    root.mkdir()
    return root


def route(
    route_id: str,
    document: str,
    *,
    required_now: bool = True,
    migration_phase: str | None = None,
) -> dict[str, Any]:
    item = {
        "id": route_id,
        "wave": "A",
        "owner": "test-agent",
        "document": document,
        "required_now": required_now,
        "status": "test",
    }
    if migration_phase is not None:
        item["migration_phase"] = migration_phase
    return item


def write_file(repo_root: Path, relative_path: str, contents: str) -> None:
    path = repo_root / relative_path
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(contents, encoding="utf-8")


def run_checker(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
    routes: list[dict[str, Any]],
    *,
    manifest_overrides: dict[str, Any] | None = None,
) -> tuple[int, pytest.CaptureResult[str]]:
    manifest = {"schema": EXPECTED_SCHEMA, "routes": routes}
    if manifest_overrides:
        manifest.update(manifest_overrides)

    manifest_path = repo_root / "manifest.json"
    manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

    result = main(["--manifest", str(manifest_path), "--repo-root", str(repo_root)])
    return result, capsys.readouterr()


def test_required_missing_route_fails(repo_root: Path, capsys: pytest.CaptureFixture[str]) -> None:
    result, captured = run_checker(
        repo_root,
        capsys,
        [route("main", "ui/main.rml", required_now=True)],
    )

    assert result == 1
    assert "required route 'main' is missing document ui/main.rml" in captured.out


def test_pending_route_is_allowed(repo_root: Path, capsys: pytest.CaptureFixture[str]) -> None:
    result, captured = run_checker(
        repo_root,
        capsys,
        [route("pending", "ui/pending.rml", required_now=False)],
    )

    assert result == 0
    assert "Routes: 1 total, 0 required, 0 present, 1 pending" in captured.out
    assert "Result: RmlUi manifest check passed." in captured.out


def test_transition_metadata_is_optional_by_default(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    write_file(repo_root, "ui/main.rml", "<rml><head></head><body /></rml>")

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("main", "ui/main.rml")],
    )

    assert result == 0
    assert "Migration phases:" not in captured.out
    assert "Result: RmlUi manifest check passed." in captured.out


def test_transition_metadata_summary_counts_valid_phases(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    write_file(repo_root, "ui/main.rml", "<rml><head></head><body /></rml>")
    write_file(repo_root, "ui/runtime.rml", "<rml><head></head><body /></rml>")

    result, captured = run_checker(
        repo_root,
        capsys,
        [
            route("main", "ui/main.rml", migration_phase="starter"),
            route("runtime", "ui/runtime.rml", migration_phase="runtime_stub"),
        ],
        manifest_overrides={"migration_phase_required": True},
    )

    assert result == 0
    assert "Migration phases: starter=1, runtime_stub=1" in captured.out
    assert "Result: RmlUi manifest check passed." in captured.out


def test_transition_metadata_required_opt_in_fails_missing_phase(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    write_file(repo_root, "ui/main.rml", "<rml><head></head><body /></rml>")

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("main", "ui/main.rml")],
        manifest_overrides={"migration_phase_required": True},
    )

    assert result == 1
    assert (
        "route 'main' field 'migration_phase' is required when "
        "'migration_phase_required' is true"
    ) in captured.out


def test_transition_metadata_invalid_phase_fails(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    write_file(repo_root, "ui/main.rml", "<rml><head></head><body /></rml>")

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("main", "ui/main.rml", migration_phase="almost_done")],
    )

    assert result == 1
    assert (
        "route 'main' field 'migration_phase' must be one of starter, "
        "controller_stub, runtime_stub, parity_pending, parity_ready; "
        "got 'almost_done'"
    ) in captured.out


def test_malformed_rml_fails(repo_root: Path, capsys: pytest.CaptureFixture[str]) -> None:
    write_file(repo_root, "ui/broken.rml", "<rml><body></rml>")

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("broken", "ui/broken.rml")],
    )

    assert result == 1
    assert "route 'broken' has malformed RML ui/broken.rml" in captured.out


def test_missing_local_href_fails(repo_root: Path, capsys: pytest.CaptureFixture[str]) -> None:
    write_file(
        repo_root,
        "ui/main.rml",
        '<rml><head><link type="text/rcss" href="missing.rcss" /></head><body /></rml>',
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("main", "ui/main.rml")],
    )

    assert result == 1
    assert "route 'main' missing local href import missing.rcss referenced by ui/main.rml" in captured.out


def test_present_route_with_imports_passes(repo_root: Path, capsys: pytest.CaptureFixture[str]) -> None:
    write_file(repo_root, "ui/theme/base.rcss", "body { color: white; }\n")
    write_file(
        repo_root,
        "ui/components/menu.rml",
        '<templates><template name="menu"><div /></template></templates>',
    )
    write_file(
        repo_root,
        "ui/main.rml",
        (
            '<rml><head><link type="text/rcss" href="theme/base.rcss" />'
            '<link type="text/template" href="components/menu.rml" /></head><body /></rml>'
        ),
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("main", "ui/main.rml")],
    )

    assert result == 0
    assert "RML parsed: 2 files, href imports checked: 2" in captured.out
    assert "Result: RmlUi manifest check passed." in captured.out


def test_duplicate_route_ids_fail(repo_root: Path, capsys: pytest.CaptureFixture[str]) -> None:
    result, captured = run_checker(
        repo_root,
        capsys,
        [
            route("main", "ui/main.rml", required_now=False),
            route("main", "ui/other.rml", required_now=False),
        ],
    )

    assert result == 1
    assert "duplicate route id 'main'" in captured.out


def test_route_required_now_shape_fails(repo_root: Path, capsys: pytest.CaptureFixture[str]) -> None:
    write_file(repo_root, "ui/main.rml", "<rml><head></head><body /></rml>")
    bad_route = route("main", "ui/main.rml")
    bad_route["required_now"] = "yes"

    result, captured = run_checker(repo_root, capsys, [bad_route])

    assert result == 1
    assert "route 'main' field 'required_now' must be a boolean" in captured.out
