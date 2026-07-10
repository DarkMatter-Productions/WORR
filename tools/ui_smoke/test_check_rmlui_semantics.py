from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_semantics as semantics  # noqa: E402


@pytest.fixture
def repo_root(tmp_path: Path) -> Path:
    root = tmp_path / "repo"
    root.mkdir()
    return root


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def route(route_id: str, document: str) -> dict[str, Any]:
    return {
        "id": route_id,
        "document": document,
        "required_now": True,
    }


def run_checker(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
    routes: list[dict[str, Any]],
) -> tuple[int, pytest.CaptureResult[str]]:
    manifest_path = repo_root / "tools/ui_smoke/rmlui_manifest.json"
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        json.dumps({"schema": "worr.rmlui.smoke_manifest.v1", "routes": routes}),
        encoding="utf-8",
    )

    result = semantics.main(["--manifest", str(manifest_path), "--repo-root", str(repo_root)])
    return result, capsys.readouterr()


def test_valid_route_target_passes(repo_root: Path, capsys: pytest.CaptureFixture[str]) -> None:
    write_text(
        repo_root / "assets/ui/rml/main.rml",
        (
            '<rml><body><button id="open-options" type="button" '
            'data-command="pushmenu options" data-route-target="options">Options</button>'
            "</body></rml>"
        ),
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [
            route("main", "assets/ui/rml/main.rml"),
            route("options", "assets/ui/rml/options.rml"),
        ],
    )

    assert result == 0
    assert "Route targets checked: 1" in captured.out
    assert "Command elements checked: 1" in captured.out
    assert "Result: RmlUi semantics check passed." in captured.out


def test_unknown_route_target_fails(repo_root: Path, capsys: pytest.CaptureFixture[str]) -> None:
    write_text(
        repo_root / "assets/ui/rml/main.rml",
        (
            '<rml><body><button id="open-missing" type="button" '
            'data-command="pushmenu missing" data-route-target="missing">Missing</button>'
            "</body></rml>"
        ),
    )

    result, captured = run_checker(repo_root, capsys, [route("main", "assets/ui/rml/main.rml")])

    assert result == 1
    assert "data-route-target references unknown route 'missing'" in captured.out


def test_command_element_without_id_fails(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    write_text(
        repo_root / "assets/ui/rml/main.rml",
        '<rml><body><button type="button" data-command="ui.close">Close</button></body></rml>',
    )

    result, captured = run_checker(repo_root, capsys, [route("main", "assets/ui/rml/main.rml")])

    assert result == 1
    assert "uses data-command but is missing a non-empty id" in captured.out


def test_bad_direct_cvar_token_fails(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    write_text(
        repo_root / "assets/ui/rml/video.rml",
        '<rml><body><input id="video-gamma" data-cvar="BadCvar" /></body></rml>',
    )

    result, captured = run_checker(repo_root, capsys, [route("video", "assets/ui/rml/video.rml")])

    assert result == 1
    assert "data-cvar must use a lowercase snake_case-ish cvar token: 'BadCvar'" in captured.out
