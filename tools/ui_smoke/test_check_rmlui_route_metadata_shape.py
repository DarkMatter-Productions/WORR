from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_route_metadata_shape as metadata_shape  # noqa: E402


def write_json(path: Path, data: object) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    return path


def migration_phase_values() -> dict[str, str]:
    return {
        "starter": "Starter metadata only.",
        "controller_stub": "Controller contract metadata exists.",
        "runtime_stub": "Runtime stub metadata exists.",
        "parity_pending": "Parity evidence is incomplete.",
        "parity_ready": "Parity evidence is complete.",
    }


def route_metadata(routes: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "schema": "worr.rml.test.routes.v1",
        "owner": "test-worker",
        "tasks": ["FR-09-T08"],
        "status_values": {
            "starter": "Starter RML document exists.",
            "controller": "Static controller contract metadata exists.",
        },
        "migration_phase_values": migration_phase_values(),
        "routes": routes,
    }


def contract_ref(**overrides: Any) -> dict[str, Any]:
    data: dict[str, Any] = {
        "category": "command_action",
        "contract": "worr.rml.controller.command_action.mock",
        "fixture": "command-action.mock.json",
        "model": "ui.commands",
        "status": "mock_fixture",
    }
    data.update(overrides)
    return data


def metadata_route(
    route_id: str = "options",
    migration_phase: str = "starter",
    *,
    document: str | None = None,
    task_ids: list[str] | None = None,
    contracts: list[dict[str, Any]] | None = None,
) -> dict[str, Any]:
    route: dict[str, Any] = {
        "id": route_id,
        "wave": "A",
        "group": "shell",
        "document": document or f"shell/{route_id}.rml",
        "document_id": f"shell-{route_id}",
        "status": "starter" if migration_phase == "starter" else "controller",
        "source_menu": f"src/game/cgame/ui/worr.json:{route_id}",
        "legacy_surface": f"legacy {route_id}",
        "current_surface": f"RmlUi {route_id}",
        "source_owner": "cgame_ui_json",
        "migration_phase": migration_phase,
        "task_ids": task_ids or ["FR-09-T08", "DV-03-T07"],
        "controller_scope": f"shell.{route_id}",
        "entry_points": [f"pushmenu {route_id}"],
        "data_models": [f"shell.{route_id}"],
        "notes": "Static route metadata only.",
    }
    if contracts is not None:
        route["controller_contracts"] = contracts
    return route


def run_checker(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
    data: dict[str, Any],
    *,
    output_format: str = "text",
) -> tuple[int, pytest.CaptureResult[str]]:
    metadata_path = write_json(repo_root / "assets/ui/rml/shell/routes.json", data)
    result = metadata_shape.main(
        [
            "--route-metadata",
            str(metadata_path),
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
        route_metadata(
            [
                metadata_route("main"),
                metadata_route(
                    "options",
                    "controller_stub",
                    contracts=[contract_ref()],
                ),
            ]
        ),
    )

    assert result == 0
    assert "Metadata files: 1" in captured.out
    assert "Metadata routes: 2" in captured.out
    assert "starter=1, controller_stub=1" in captured.out
    assert "Routes with controller contracts: 1" in captured.out
    assert "Controller contract refs: 1" in captured.out
    assert "Result: RmlUi route metadata shape check passed." in captured.out


def test_missing_root_routes_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    data = route_metadata([metadata_route()])
    del data["routes"]

    result, captured = run_checker(repo_root, capsys, data)

    assert result == 1
    assert "missing required root field 'routes'" in captured.out
    assert "field 'routes' must be a list" in captured.out


def test_unsafe_document_path_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        route_metadata([metadata_route(document="../evil.rml")]),
    )

    assert result == 1
    assert "field 'document' must not contain empty, '.', or '..' segments" in captured.out
    assert "Malformed routes: 1" in captured.out


def test_bad_task_id_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        route_metadata([metadata_route(task_ids=["FR-9-T8"])]),
    )

    assert result == 1
    assert "field 'task_ids' [0] must look like FR-09-T08 or DV-03-T07" in captured.out


def test_advanced_route_missing_contracts_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        route_metadata([metadata_route("options", "controller_stub")]),
    )

    assert result == 1
    assert "advanced route must include non-empty controller_contracts" in captured.out


def test_malformed_contract_entry_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    malformed_contract = contract_ref()
    del malformed_contract["status"]

    result, captured = run_checker(
        repo_root,
        capsys,
        route_metadata(
            [
                metadata_route(
                    "options",
                    "controller_stub",
                    contracts=[malformed_contract],
                )
            ]
        ),
    )

    assert result == 1
    assert "controller_contracts[0] field 'status' must be a non-empty string" in captured.out


def test_json_output_reports_deterministic_counts(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        route_metadata(
            [
                metadata_route("main"),
                metadata_route(
                    "options",
                    "controller_stub",
                    contracts=[contract_ref(), contract_ref(category="navigation")],
                ),
            ]
        ),
        output_format="json",
    )

    payload = json.loads(captured.out)
    assert result == 0
    assert payload["ok"] is True
    assert payload["metadata_files"] == 1
    assert payload["metadata_routes"] == 2
    assert payload["routes_by_phase"] == {
        "starter": 1,
        "controller_stub": 1,
        "runtime_stub": 0,
        "parity_pending": 0,
        "parity_ready": 0,
    }
    assert payload["routes_with_controller_contracts"] == 1
    assert payload["controller_contract_refs"] == 2
    assert payload["malformed_route_count"] == 0
    assert payload["malformed_routes"] == []
    assert payload["errors"] == []
