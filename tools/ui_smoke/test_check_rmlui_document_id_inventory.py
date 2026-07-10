from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_document_id_inventory as document_id_inventory  # noqa: E402


def write_text(path: Path, text: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def write_json(path: Path, data: object) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    return path


def rml_document(*, body_id: str | None, route_id: str | None) -> str:
    attrs: list[str] = []
    if body_id is not None:
        attrs.append(f'id="{body_id}"')
    if route_id is not None:
        attrs.append(f'data-route-id="{route_id}"')
    return f"""
<rml>
  <body {' '.join(attrs)}>
    <button id="back" data-command="ui.back">Back</button>
  </body>
</rml>
"""


def smoke_manifest(routes: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "schema": document_id_inventory.EXPECTED_SCHEMA,
        "routes": routes,
    }


def smoke_route(route_id: str, document: str | None = None) -> dict[str, Any]:
    return {
        "id": route_id,
        "document": document or f"assets/ui/rml/shell/{route_id}.rml",
        "required_now": True,
        "migration_phase": "controller_stub",
    }


def route_metadata(routes: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "schema": "worr.rml.test.routes.v1",
        "routes": routes,
    }


def metadata_route(
    route_id: str,
    *,
    document: str | None = None,
    document_id: str | None = None,
) -> dict[str, Any]:
    route: dict[str, Any] = {
        "id": route_id,
        "document": document or f"shell/{route_id}.rml",
        "migration_phase": "controller_stub",
    }
    if document_id is not None:
        route["document_id"] = document_id
    return route


def run_checker(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
    *,
    manifest_routes: list[dict[str, Any]],
    metadata_routes: list[dict[str, Any]],
    output_format: str = "text",
) -> tuple[int, pytest.CaptureResult[str]]:
    manifest_path = write_json(
        repo_root / "tools/ui_smoke/rmlui_manifest.json",
        smoke_manifest(manifest_routes),
    )
    write_json(
        repo_root / "assets/ui/rml/shell/routes.json",
        route_metadata(metadata_routes),
    )

    result = document_id_inventory.main(
        [
            "--manifest",
            str(manifest_path),
            "--repo-root",
            str(repo_root),
            "--route-metadata-root",
            str(repo_root / "assets/ui/rml"),
            "--format",
            output_format,
        ]
    )
    return result, capsys.readouterr()


def test_document_id_inventory_success_counts_json(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/shell/main.rml",
        rml_document(body_id="shell-main", route_id="main"),
    )
    write_text(
        repo_root / "assets/ui/rml/shell/options.rml",
        rml_document(body_id="shell-options", route_id="options"),
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        manifest_routes=[
            smoke_route("main"),
            smoke_route("options"),
        ],
        metadata_routes=[
            metadata_route("main", document_id="shell-main"),
            metadata_route("options", document_id="shell-options"),
        ],
        output_format="json",
    )

    payload = json.loads(captured.out)
    assert result == 0
    assert captured.err == ""
    assert payload["ok"] is True
    assert payload["route_count"] == 2
    assert payload["route_metadata_file_count"] == 1
    assert payload["route_metadata_count"] == 2
    assert payload["documents_checked"] == 2
    assert payload["documents_missing"] == 0
    assert payload["body_ids"]["count"] == 2
    assert payload["unique_body_ids"]["count"] == 2
    assert payload["unique_body_ids"]["ids"] == ["shell-main", "shell-options"]
    assert payload["metadata_document_ids"]["count"] == 2
    assert payload["matched_document_ids"] == {
        "count": 2,
        "routes": ["main", "options"],
    }
    assert payload["mismatched_document_ids"]["count"] == 0
    assert payload["duplicate_body_ids"]["count"] == 0
    assert payload["missing_body_ids"]["count"] == 0
    assert payload["route_id_mismatches"]["count"] == 0
    assert payload["malformed_documents"]["count"] == 0
    assert payload["errors"] == []


def test_missing_body_id_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/shell/options.rml",
        rml_document(body_id=None, route_id="options"),
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        manifest_routes=[smoke_route("options")],
        metadata_routes=[metadata_route("options", document_id="shell-options")],
    )

    assert result == 1
    assert "Routes known: 1" in captured.out
    assert "Body IDs: 0" in captured.out
    assert "Missing body IDs: 1" in captured.out
    assert "<body> id attribute is missing or empty" in captured.out
    assert "Result: RmlUi document ID/body route identity check failed." in captured.out


def test_duplicate_body_ids_fail(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/shell/main.rml",
        rml_document(body_id="shell-shared", route_id="main"),
    )
    write_text(
        repo_root / "assets/ui/rml/shell/game.rml",
        rml_document(body_id="shell-shared", route_id="game"),
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        manifest_routes=[
            smoke_route("main"),
            smoke_route("game"),
        ],
        metadata_routes=[
            metadata_route("main", document_id="shell-shared"),
            metadata_route("game", document_id="shell-shared"),
        ],
    )

    assert result == 1
    assert "Body IDs: 2" in captured.out
    assert "Unique body IDs: 1" in captured.out
    assert "Duplicate body IDs: 1" in captured.out
    assert "body id 'shell-shared' is duplicated across routes: 'main', 'game'" in captured.out


def test_metadata_body_document_id_mismatch_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/shell/options.rml",
        rml_document(body_id="shell-options", route_id="options"),
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        manifest_routes=[smoke_route("options")],
        metadata_routes=[metadata_route("options", document_id="settings-options")],
    )

    assert result == 1
    assert "Mismatched metadata/body document IDs: 1" in captured.out
    assert "metadata document_id 'settings-options' does not match body id 'shell-options'" in captured.out
    assert "feature metadata document_id differs from the document body id" in captured.out


def test_body_route_id_mismatch_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/shell/options.rml",
        rml_document(body_id="shell-options", route_id="video"),
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        manifest_routes=[smoke_route("options")],
        metadata_routes=[metadata_route("options", document_id="shell-options")],
    )

    assert result == 1
    assert "Route-id mismatches: 1" in captured.out
    assert (
        "body data-route-id 'video' does not match manifest route id 'options'"
    ) in captured.out


def test_current_repository_document_id_inventory_is_stable() -> None:
    repo_root = Path(__file__).resolve().parents[2]
    manifest_path = repo_root / "tools/ui_smoke/rmlui_manifest.json"
    metadata_paths = document_id_inventory.discover_route_metadata_paths(
        repo_root,
        document_id_inventory.DEFAULT_ROUTE_METADATA_ROOT,
    )
    route_metadata_sets = [
        (path, document_id_inventory.read_json_object(path, "route metadata"))
        for path in metadata_paths
    ]
    report = document_id_inventory.build_document_id_inventory(
        document_id_inventory.read_json_object(manifest_path, "RmlUi smoke manifest"),
        route_metadata_sets,
        repo_root,
    )

    assert report.ok()
    assert report.stats.route_count >= 57
    assert report.stats.documents_checked >= 57
    assert len(report.body_ids) == report.stats.documents_checked
    assert len(report.unique_body_ids) == len(report.body_ids)
    assert len(report.matched_document_ids) >= 57
