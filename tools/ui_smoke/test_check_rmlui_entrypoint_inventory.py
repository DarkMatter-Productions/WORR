from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_entrypoint_inventory as entrypoint_inventory  # noqa: E402


def write_json(path: Path, data: object) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    return path


def smoke_manifest(routes: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "schema": entrypoint_inventory.EXPECTED_SCHEMA,
        "routes": routes,
    }


def smoke_route(route_id: str) -> dict[str, str]:
    return {
        "id": route_id,
        "document": f"assets/ui/rml/shell/{route_id}.rml",
        "migration_phase": "starter",
    }


def route_metadata(routes: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "schema": "worr.rml.test.routes.v1",
        "routes": routes,
    }


def metadata_route(
    route_id: str,
    *,
    entry_points: list[Any] | None = None,
    include_entry_points: bool = True,
) -> dict[str, Any]:
    route: dict[str, Any] = {
        "id": route_id,
        "document": f"shell/{route_id}.rml",
        "migration_phase": "starter",
    }
    if include_entry_points:
        route["entry_points"] = entry_points if entry_points is not None else [f"pushmenu {route_id}"]
    return route


def run_checker(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
    *,
    manifest_routes: list[dict[str, Any]],
    metadata_files: dict[str, list[dict[str, Any]]],
    output_format: str = "text",
) -> tuple[int, pytest.CaptureResult[str]]:
    manifest_path = write_json(
        repo_root / "tools/ui_smoke/rmlui_manifest.json",
        smoke_manifest(manifest_routes),
    )
    for relative_path, routes in metadata_files.items():
        write_json(repo_root / relative_path, route_metadata(routes))

    result = entrypoint_inventory.main(
        [
            "--manifest",
            str(manifest_path),
            "--repo-root",
            str(repo_root),
            "--format",
            output_format,
        ]
    )
    return result, capsys.readouterr()


def test_entrypoints_are_inventoried_for_central_and_support_routes(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        manifest_routes=[smoke_route("main"), smoke_route("credits")],
        metadata_files={
            "assets/ui/rml/shell/routes.json": [
                metadata_route("main", entry_points=["pushmenu main", "main menu fallback"]),
                metadata_route("credits", entry_points=[]),
            ],
            "assets/ui/rml/core/routes.json": [
                metadata_route(
                    "core.runtime_smoke",
                    entry_points=["runtime_smoke"],
                ),
            ],
        },
    )

    assert result == 0
    assert "Central routes: 2" in captured.out
    assert "Metadata files: 2" in captured.out
    assert "Metadata routes: 3" in captured.out
    assert "Routes with entrypoints: 2" in captured.out
    assert "Routes without entrypoints: 1" in captured.out
    assert "Total entrypoint refs: 3" in captured.out
    assert "Unique entrypoints: 3" in captured.out
    assert "Duplicate entrypoints: 0" in captured.out
    assert "Support metadata routes: 1" in captured.out
    assert "Central routes without metadata: 0" in captured.out
    assert "Malformed entrypoints: 0" in captured.out
    assert "Support metadata route IDs: core.runtime_smoke" in captured.out
    assert "Result: RmlUi route entrypoint inventory check passed." in captured.out


def test_missing_central_metadata_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        manifest_routes=[smoke_route("main"), smoke_route("options")],
        metadata_files={
            "assets/ui/rml/shell/routes.json": [
                metadata_route("main"),
            ],
        },
    )

    assert result == 1
    assert "Central routes without metadata: 1" in captured.out
    assert "central route 'options' is missing route metadata" in captured.out
    assert "Result: RmlUi route entrypoint inventory check failed." in captured.out


def test_missing_empty_and_non_string_entrypoints_fail(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        manifest_routes=[
            smoke_route("missing"),
            smoke_route("empty"),
            smoke_route("non_string"),
        ],
        metadata_files={
            "assets/ui/rml/shell/routes.json": [
                metadata_route("missing", include_entry_points=False),
                metadata_route("empty", entry_points=["   "]),
                metadata_route("non_string", entry_points=[123]),
            ],
        },
    )

    assert result == 1
    assert "Routes without entrypoints: 3" in captured.out
    assert "Malformed entrypoints: 3" in captured.out
    assert "entry_points is malformed: missing entry_points list" in captured.out
    assert "entry_points[0] is malformed: entry point must not be empty" in captured.out
    assert "entry_points[0] is malformed: entry point must be a string" in captured.out
    assert "Result: RmlUi route entrypoint inventory check failed." in captured.out


def test_duplicate_entrypoints_within_a_route_fail(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        manifest_routes=[smoke_route("main")],
        metadata_files={
            "assets/ui/rml/shell/routes.json": [
                metadata_route("main", entry_points=["pushmenu main", "pushmenu main"]),
            ],
        },
    )

    assert result == 1
    assert "Total entrypoint refs: 2" in captured.out
    assert "Unique entrypoints: 1" in captured.out
    assert "Duplicate entrypoints: 1" in captured.out
    assert "entry_points duplicates entry point 'pushmenu main'" in captured.out
    assert "Result: RmlUi route entrypoint inventory check failed." in captured.out


def test_json_output_reports_entrypoint_inventory_facts(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        manifest_routes=[smoke_route("main")],
        metadata_files={
            "assets/ui/rml/shell/routes.json": [
                metadata_route("main", entry_points=["pushmenu main"]),
            ],
            "assets/ui/rml/core/routes.json": [
                metadata_route("core.runtime_smoke", entry_points=["runtime_smoke"]),
            ],
        },
        output_format="json",
    )

    payload = json.loads(captured.out)
    assert result == 0
    assert captured.err == ""
    assert payload["ok"] is True
    assert payload["central_routes"] == 1
    assert payload["metadata_files"] == 2
    assert payload["metadata_routes"] == 2
    assert payload["routes_with_entrypoints"] == 2
    assert payload["routes_without_entrypoints"] == 0
    assert payload["total_entrypoint_refs"] == 2
    assert payload["unique_entrypoints"] == 2
    assert payload["duplicate_entrypoints"] == 0
    assert payload["support_metadata_routes"] == 1
    assert payload["central_routes_without_metadata"] == 0
    assert payload["malformed_entrypoints"] == 0
    assert payload["entrypoints_by_route"] == {
        "core.runtime_smoke": ["runtime_smoke"],
        "main": ["pushmenu main"],
    }
    assert payload["unique_entrypoint_values"] == ["pushmenu main", "runtime_smoke"]
    assert payload["support_metadata_route_ids"] == ["core.runtime_smoke"]
    assert payload["central_route_ids_without_metadata"] == []
    assert payload["malformed_entrypoint_details"] == []
    assert payload["errors"] == []


def test_current_repository_entrypoint_inventory_is_broadly_stable() -> None:
    repo_root = Path(__file__).resolve().parents[2]
    manifest_path = repo_root / "tools/ui_smoke/rmlui_manifest.json"
    metadata_sets = [
        (
            path,
            entrypoint_inventory.read_json_object(
                path,
                entrypoint_inventory.display_path(path, repo_root),
            ),
        )
        for path in entrypoint_inventory.discover_route_metadata_paths(repo_root)
    ]
    report = entrypoint_inventory.audit_entrypoint_inventory(
        entrypoint_inventory.read_json_object(manifest_path, "RmlUi smoke manifest"),
        metadata_sets,
        repo_root,
    )

    assert report.ok()
    assert report.central_routes >= 57
    assert report.metadata_files >= 5
    assert report.metadata_routes >= 58
    assert report.total_entrypoint_refs > 0
    assert report.unique_entrypoints > 0
    assert report.support_metadata_routes >= 1
    assert report.central_routes_without_metadata == 0
