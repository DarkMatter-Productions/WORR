from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_controller_fixtures as fixtures  # noqa: E402


@pytest.fixture
def repo_root(tmp_path: Path) -> Path:
    root = tmp_path / "repo"
    root.mkdir()
    return root


def write_json(path: Path, data: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def write_routes(repo_root: Path, owner: str, routes: list[dict[str, Any]]) -> Path:
    path = repo_root / "assets/ui/rml" / owner / "routes.json"
    write_json(path, {"schema": f"worr.rml.{owner}.routes.v1", "routes": routes})
    return path


def write_fixture(
    repo_root: Path,
    name: str = "navigation.mock.json",
    *,
    contract: str = "worr.rml.controller.navigation.mock",
    category: str = "navigation",
) -> Path:
    path = repo_root / "assets/ui/rml/contracts" / name
    write_json(
        path,
        {
            "contract": contract,
            "version": 1,
            "mock": True,
            "category": category,
            "model": "ui.navigation",
        },
    )
    return path


def contract_ref(**overrides: object) -> dict[str, object]:
    ref: dict[str, object] = {
        "category": "navigation",
        "contract": "worr.rml.controller.navigation.mock",
        "fixture": "navigation.mock.json",
        "model": "ui.navigation",
        "status": "mock_fixture",
    }
    ref.update(overrides)
    return ref


def route(route_id: str, refs: object) -> dict[str, object]:
    return {
        "id": route_id,
        "document": f"shell/{route_id}.rml",
        "controller_contracts": refs,
    }


def run_checker(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
    *,
    output_format: str = "text",
) -> tuple[int, pytest.CaptureResult[str]]:
    result = fixtures.main(
        [
            "--repo-root",
            str(repo_root),
            "--format",
            output_format,
        ]
    )
    return result, capsys.readouterr()


def test_valid_controller_fixture_refs_pass(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    write_fixture(repo_root)
    write_routes(repo_root, "shell", [route("main", [contract_ref()])])

    result, captured = run_checker(repo_root, capsys)

    assert result == 0
    assert "Route metadata files: 1" in captured.out
    assert "Route contract refs: 1" in captured.out
    assert "Fixtures referenced: 1" in captured.out
    assert "Missing fixtures: 0" in captured.out
    assert "Malformed fixtures: 0" in captured.out
    assert "Malformed contract refs: 0" in captured.out
    assert "Result: RmlUi controller fixture check passed." in captured.out


def test_missing_controller_fixture_fails(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    write_routes(repo_root, "shell", [route("main", [contract_ref(fixture="missing.mock.json")])])

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "Missing fixtures: 1" in captured.out
    assert "fixture file does not exist: missing.mock.json" in captured.out
    assert "Result: RmlUi controller fixture check failed." in captured.out


def test_malformed_json_controller_fixture_fails(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    fixture_path = repo_root / "assets/ui/rml/contracts/navigation.mock.json"
    fixture_path.parent.mkdir(parents=True, exist_ok=True)
    fixture_path.write_text("{ invalid json\n", encoding="utf-8")
    write_routes(repo_root, "shell", [route("main", [contract_ref()])])

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "Malformed fixtures: 1" in captured.out
    assert "assets/ui/rml/contracts/navigation.mock.json is not valid JSON" in captured.out


def test_malformed_contract_entry_fails(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    write_fixture(repo_root)
    write_routes(
        repo_root,
        "shell",
        [
            route(
                "main",
                [
                    "navigation.mock.json",
                    contract_ref(category="Navigation", model="", status="Mock Fixture"),
                ],
            )
        ],
    )

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "Malformed contract refs: 2" in captured.out
    assert "controller_contracts[0] must be an object" in captured.out
    assert "field 'category' must use lowercase token characters" in captured.out
    assert "field 'model' must be a non-empty string" in captured.out
    assert "field 'status' must use lowercase token characters" in captured.out


def test_optional_metadata_files_are_discovered_and_reported_as_json(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    write_fixture(repo_root)
    write_fixture(
        repo_root,
        "command-action.mock.json",
        contract="worr.rml.controller.command_action.mock",
        category="command_action",
    )
    write_routes(repo_root, "core", [])
    write_routes(repo_root, "shell", [route("main", [contract_ref()])])
    write_routes(
        repo_root,
        "multiplayer",
        [
            route(
                "server_browser",
                [
                    contract_ref(
                        category="command_action",
                        contract="worr.rml.controller.command_action.mock",
                        fixture="command-action.mock.json",
                        model="ui.commands",
                    )
                ],
            )
        ],
    )

    result, captured = run_checker(repo_root, capsys, output_format="json")

    payload = json.loads(captured.out)
    assert result == 0
    assert payload["ok"] is True
    assert payload["metadata_files"] == [
        "assets/ui/rml/core/routes.json",
        "assets/ui/rml/multiplayer/routes.json",
        "assets/ui/rml/shell/routes.json",
    ]
    assert payload["counts"]["metadata_files_checked"] == 3
    assert payload["counts"]["routes_checked"] == 2
    assert payload["counts"]["route_contract_refs"] == 2
    assert payload["counts"]["unique_fixtures_referenced"] == 2
    assert payload["errors"] == []
