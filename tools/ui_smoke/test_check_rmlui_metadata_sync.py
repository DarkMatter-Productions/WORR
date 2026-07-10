from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_metadata_sync as metadata_sync  # noqa: E402


def write_json(path: Path, data: object) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    return path


def smoke_manifest(routes: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "schema": "worr.rmlui.smoke_manifest.v1",
        "routes": routes,
    }


def smoke_route(
    route_id: str,
    migration_phase: str,
    *,
    document: str | None = None,
) -> dict[str, str]:
    return {
        "id": route_id,
        "document": document or f"assets/ui/rml/shell/{route_id}.rml",
        "migration_phase": migration_phase,
    }


def route_metadata(routes: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "schema": "worr.rml.test.routes.v1",
        "routes": routes,
    }


def metadata_route(
    route_id: str,
    migration_phase: str,
    *,
    document: str | None = None,
) -> dict[str, str]:
    return {
        "id": route_id,
        "document": document or f"shell/{route_id}.rml",
        "migration_phase": migration_phase,
    }


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

    result = metadata_sync.main(
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


def test_valid_fixture_passes(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        manifest_routes=[
            smoke_route("main", "runtime_stub"),
            smoke_route("options", "controller_stub"),
        ],
        metadata_files={
            "assets/ui/rml/shell/routes.json": [
                metadata_route("main", "runtime_stub"),
                metadata_route("options", "controller_stub"),
            ],
        },
    )

    assert result == 0
    assert "Central routes: 2" in captured.out
    assert "Metadata files: 1" in captured.out
    assert "Matched routes: 2" in captured.out
    assert "Result: RmlUi metadata sync check passed." in captured.out


def test_metadata_route_missing_from_central_manifest_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        manifest_routes=[smoke_route("main", "runtime_stub")],
        metadata_files={
            "assets/ui/rml/shell/routes.json": [
                metadata_route("main", "runtime_stub"),
                metadata_route("extra", "starter"),
            ],
        },
    )

    assert result == 1
    assert "Unknown metadata routes: 1" in captured.out
    assert "metadata route 'extra' has no central smoke manifest route" in captured.out


def test_core_runtime_smoke_support_route_is_reported_not_failed(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        manifest_routes=[smoke_route("main", "runtime_stub")],
        metadata_files={
            "assets/ui/rml/shell/routes.json": [
                metadata_route("main", "runtime_stub"),
            ],
            "assets/ui/rml/core/routes.json": [
                metadata_route(
                    "core.runtime_smoke",
                    "starter",
                    document="core/runtime_smoke.rml",
                ),
            ],
        },
    )

    assert result == 0
    assert "Support metadata routes: 1" in captured.out
    assert "Support metadata route IDs: core.runtime_smoke" in captured.out
    assert "Unknown metadata routes: 0" in captured.out


def test_duplicate_metadata_route_id_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        manifest_routes=[smoke_route("main", "runtime_stub")],
        metadata_files={
            "assets/ui/rml/shell/routes.json": [
                metadata_route("main", "runtime_stub"),
            ],
            "assets/ui/rml/utility/routes.json": [
                metadata_route("main", "runtime_stub"),
            ],
        },
    )

    assert result == 1
    assert "Duplicate route IDs: 1" in captured.out
    assert "route metadata id 'main' is duplicated across feature metadata" in captured.out


def test_document_mismatch_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        manifest_routes=[
            smoke_route("main", "runtime_stub", document="assets/ui/rml/shell/main.rml"),
        ],
        metadata_files={
            "assets/ui/rml/shell/routes.json": [
                metadata_route("main", "runtime_stub", document="utility/main.rml"),
            ],
        },
    )

    assert result == 1
    assert "Document mismatches: 1" in captured.out
    assert (
        "route 'main' document mismatch: central 'assets/ui/rml/shell/main.rml', "
        "metadata 'assets/ui/rml/utility/main.rml'"
    ) in captured.out


def test_migration_phase_mismatch_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        manifest_routes=[smoke_route("options", "controller_stub")],
        metadata_files={
            "assets/ui/rml/shell/routes.json": [
                metadata_route("options", "starter"),
            ],
        },
    )

    assert result == 1
    assert "Phase mismatches: 1" in captured.out
    assert (
        "route 'options' migration_phase mismatch: central 'controller_stub', "
        "metadata 'starter'"
    ) in captured.out


def test_advanced_central_route_missing_metadata_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        manifest_routes=[
            smoke_route("options", "controller_stub"),
            smoke_route("credits", "starter"),
        ],
        metadata_files={},
    )

    assert result == 1
    assert "Central routes without feature metadata: 2" in captured.out
    assert "Advanced central routes without feature metadata: 1" in captured.out
    assert (
        "advanced central route 'options' with migration_phase 'controller_stub' "
        "is missing feature route metadata"
    ) in captured.out
    assert "advanced central route 'credits'" not in captured.out


def test_json_output_reports_counters_and_lists(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        manifest_routes=[
            smoke_route("main", "runtime_stub"),
            smoke_route("credits", "starter"),
        ],
        metadata_files={
            "assets/ui/rml/shell/routes.json": [
                metadata_route("main", "runtime_stub"),
            ],
        },
        output_format="json",
    )

    payload = json.loads(captured.out)
    assert result == 0
    assert payload["ok"] is True
    assert payload["central_route_count"] == 2
    assert payload["metadata_file_count"] == 1
    assert payload["metadata_route_count"] == 1
    assert payload["matched_route_count"] == 1
    assert payload["central_routes_without_feature_metadata"] == {
        "count": 1,
        "routes": ["credits"],
    }
    assert payload["advanced_central_routes_without_feature_metadata"] == {
        "count": 0,
        "routes": [],
    }
    assert payload["support_metadata_routes"] == {
        "count": 0,
        "routes": [],
    }
    assert payload["phase_mismatch_count"] == 0
    assert payload["document_mismatch_count"] == 0
    assert payload["duplicate_count"] == 0
    assert payload["errors"] == []
