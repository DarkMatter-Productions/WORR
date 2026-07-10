from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_runtime_registry as runtime_registry  # noqa: E402


def write_manifest(repo_root: Path, routes: list[dict[str, Any]]) -> Path:
    manifest_path = repo_root / "tools/ui_smoke/rmlui_manifest.json"
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        json.dumps(
            {
                "schema": runtime_registry.EXPECTED_SCHEMA,
                "routes": routes,
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    return manifest_path


def write_cpp(repo_root: Path, routes: list[tuple[str, str]]) -> Path:
    cpp_path = repo_root / "src/client/ui_rml/ui_rml.cpp"
    cpp_path.parent.mkdir(parents=True, exist_ok=True)
    entries = "\n".join(
        f'    {{ "{route_id}", "{document}" }},'
        for route_id, document in routes
    )
    cpp_path.write_text(
        "\n".join(
            [
                "typedef struct {",
                "    const char *id;",
                "    const char *document;",
                "} ui_rml_route_t;",
                "",
                "static const ui_rml_route_t ui_rml_routes[] = {",
                entries,
                "};",
                "",
            ]
        ),
        encoding="utf-8",
    )
    return cpp_path


def manifest_route(route_id: str, document: str) -> dict[str, Any]:
    return {
        "id": route_id,
        "document": document,
    }


def run_checker(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
    manifest_routes: list[dict[str, Any]],
    registered_routes: list[tuple[str, str]],
) -> tuple[int, pytest.CaptureResult[str]]:
    manifest_path = write_manifest(repo_root, manifest_routes)
    cpp_path = write_cpp(repo_root, registered_routes)

    result = runtime_registry.main(
        [
            "--manifest",
            str(manifest_path),
            "--cpp",
            str(cpp_path),
            "--repo-root",
            str(repo_root),
        ]
    )
    return result, capsys.readouterr()


def test_valid_registry_accepts_manifest_routes_and_default_extra(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        [
            manifest_route("main", "assets/ui/rml/shell/main.rml"),
            manifest_route("options", "assets/ui/rml/shell/options.rml"),
        ],
        [
            ("main", "shell/main.rml"),
            ("options", "shell/options.rml"),
            ("core.runtime_smoke", "core/runtime_smoke.rml"),
        ],
    )

    assert result == 0
    assert "Manifest routes: 2" in captured.out
    assert "Registered routes: 3" in captured.out
    assert "Missing: 0" in captured.out
    assert "Unexpected: 0" in captured.out
    assert "Duplicates: 0" in captured.out
    assert "Matched runtime paths: 2" in captured.out
    assert "Result: RmlUi runtime route registry check passed." in captured.out


def test_missing_manifest_route_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        [
            manifest_route("main", "assets/ui/rml/shell/main.rml"),
            manifest_route("options", "assets/ui/rml/shell/options.rml"),
        ],
        [
            ("main", "shell/main.rml"),
        ],
    )

    assert result == 1
    assert "Missing: 1" in captured.out
    assert "manifest route 'options' is missing from ui_rml_routes" in captured.out


def test_unexpected_registered_route_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        [
            manifest_route("main", "assets/ui/rml/shell/main.rml"),
        ],
        [
            ("main", "shell/main.rml"),
            ("console", "shell/console.rml"),
        ],
    )

    assert result == 1
    assert "Unexpected: 1" in captured.out
    assert "unexpected registered route 'console'" in captured.out


def test_duplicate_registered_route_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        [
            manifest_route("main", "assets/ui/rml/shell/main.rml"),
        ],
        [
            ("main", "shell/main.rml"),
            ("main", "shell/main.rml"),
        ],
    )

    assert result == 1
    assert "Duplicates: 1" in captured.out
    assert "duplicate registered route 'main'" in captured.out


def test_mismatched_document_path_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        [
            manifest_route("main", "assets/ui/rml/shell/main.rml"),
        ],
        [
            ("main", "settings/main.rml"),
        ],
    )

    assert result == 1
    assert "Matched runtime paths: 0" in captured.out
    assert (
        "registered route 'main' runtime path mismatch: "
        "ui/rml/settings/main.rml != ui/rml/shell/main.rml"
    ) in captured.out
