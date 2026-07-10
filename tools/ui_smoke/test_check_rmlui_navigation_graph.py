from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_navigation_graph as navigation_graph  # noqa: E402


def write_text(path: Path, text: str = "<rml></rml>\n") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def route(route_id: str, document: str) -> dict[str, Any]:
    return {
        "id": route_id,
        "document": document,
        "required_now": True,
    }


def write_manifest(repo_root: Path, routes: list[dict[str, Any]]) -> Path:
    manifest_path = repo_root / "tools/ui_smoke/rmlui_manifest.json"
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        json.dumps(
            {
                "schema": navigation_graph.EXPECTED_SCHEMA,
                "routes": routes,
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    return manifest_path


def run_checker(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
    routes: list[dict[str, Any]],
    *,
    roots: tuple[str, ...] = ("main",),
    output_format: str = "text",
) -> tuple[int, pytest.CaptureResult[str]]:
    manifest_path = write_manifest(repo_root, routes)
    result = navigation_graph.main(
        [
            "--manifest",
            str(manifest_path),
            "--repo-root",
            str(repo_root),
            "--roots",
            ",".join(roots),
            "--format",
            output_format,
        ]
    )
    return result, capsys.readouterr()


def test_known_route_target_builds_edge(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/shell/main.rml",
        (
            '<rml><body><button id="main-options" type="button" '
            'data-route-target="options">Options</button></body></rml>'
        ),
    )
    write_text(repo_root / "assets/ui/rml/shell/options.rml")

    result, captured = run_checker(
        repo_root,
        capsys,
        [
            route("main", "assets/ui/rml/shell/main.rml"),
            route("options", "assets/ui/rml/shell/options.rml"),
        ],
    )

    assert result == 0
    assert "Routes known: 2" in captured.out
    assert "Route-target references: 1" in captured.out
    assert "Edges: 1" in captured.out
    assert "Unknown targets: 0" in captured.out
    assert "Unreachable from guarded roots ['main']: 0" in captured.out
    assert "Result: RmlUi navigation graph check passed." in captured.out


def test_unknown_route_target_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/shell/main.rml",
        (
            '<rml><body><button id="main-missing" type="button" '
            'data-route-target="missing">Missing</button></body></rml>'
        ),
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("main", "assets/ui/rml/shell/main.rml")],
    )

    assert result == 1
    assert "Unknown targets: 1" in captured.out
    assert "data-route-target references unknown route 'missing'" in captured.out


def test_dead_end_routes_are_reported_without_failing(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/shell/main.rml",
        (
            '<rml><body><button id="main-options" type="button" '
            'data-route-target="options">Options</button></body></rml>'
        ),
    )
    write_text(repo_root / "assets/ui/rml/shell/options.rml")
    write_text(repo_root / "assets/ui/rml/shell/help.rml")

    result, captured = run_checker(
        repo_root,
        capsys,
        [
            route("main", "assets/ui/rml/shell/main.rml"),
            route("options", "assets/ui/rml/shell/options.rml"),
            route("help", "assets/ui/rml/shell/help.rml"),
        ],
    )

    assert result == 0
    assert "Dead-end routes: 2" in captured.out
    assert "Dead-end route IDs: help, options" in captured.out


def test_unreachable_routes_are_reported_from_roots(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/shell/main.rml",
        '<rml><body><button id="main-options" data-route-target="options" /></body></rml>',
    )
    write_text(repo_root / "assets/ui/rml/shell/options.rml")
    write_text(repo_root / "assets/ui/rml/shell/credits.rml")

    result, captured = run_checker(
        repo_root,
        capsys,
        [
            route("main", "assets/ui/rml/shell/main.rml"),
            route("options", "assets/ui/rml/shell/options.rml"),
            route("credits", "assets/ui/rml/shell/credits.rml"),
        ],
    )

    assert result == 0
    assert "Unreachable from guarded roots ['main']: 1" in captured.out
    assert "Unreachable route IDs: credits" in captured.out


def test_json_output_reports_navigation_facts(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/shell/main.rml",
        """
<rml>
  <body>
    <button id="main-options" data-route-target="options" />
    <button id="main-options-again" data-route-target="options" />
  </body>
</rml>
""",
    )
    write_text(repo_root / "assets/ui/rml/shell/options.rml")

    result, captured = run_checker(
        repo_root,
        capsys,
        [
            route("main", "assets/ui/rml/shell/main.rml"),
            route("options", "assets/ui/rml/shell/options.rml"),
        ],
        output_format="json",
    )

    payload = json.loads(captured.out)
    assert result == 0
    assert captured.err == ""
    assert payload == {
        "ok": True,
        "roots": ["main"],
        "route_count": 2,
        "documents_checked": 2,
        "documents_missing": 0,
        "route_target_references": 2,
        "edge_count": 1,
        "unknown_targets": [],
        "dead_end_routes": ["options"],
        "unreachable_routes": [],
        "errors": [],
    }
