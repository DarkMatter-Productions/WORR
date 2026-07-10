from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_controller_stub_coverage as coverage  # noqa: E402


@pytest.fixture
def repo_root(tmp_path: Path) -> Path:
    root = tmp_path / "repo"
    root.mkdir()
    return root


def write_json(path: Path, data: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def manifest_route(route_id: str, document: str = "assets/ui/rml/shell/main.rml") -> dict[str, Any]:
    return {
        "id": route_id,
        "document": document,
        "required_now": True,
        "migration_phase": "controller_stub",
    }


def shell_route(route_id: str, contract_categories: list[str] | None) -> dict[str, Any]:
    route: dict[str, Any] = {
        "id": route_id,
        "document": "shell/main.rml",
        "migration_phase": "controller_stub",
    }
    if contract_categories is not None:
        route["controller_contracts"] = [
            {
                "category": category,
                "contract": f"worr.rml.controller.{category}.mock",
                "fixture": f"{category}.mock.json",
                "model": f"ui.{category}",
                "status": "mock_fixture",
            }
            for category in contract_categories
        ]
    return route


def utility_route(route_id: str, contract_categories: list[str] | None) -> dict[str, Any]:
    route: dict[str, Any] = {
        "id": route_id,
        "document": f"utility/{route_id}.rml",
        "migration_phase": "controller_stub",
    }
    if contract_categories is not None:
        route["controller_contracts"] = [
            {
                "category": category,
                "contract": f"worr.rml.controller.{category}.mock",
                "fixture": f"{category}.mock.json",
                "model": f"utility.{category}",
                "status": "mock_fixture",
            }
            for category in contract_categories
        ]
    return route


def write_manifests(
    repo_root: Path,
    manifest_routes: list[dict[str, Any]],
    shell_routes: list[dict[str, Any]],
) -> tuple[Path, Path]:
    manifest_path = repo_root / "tools/ui_smoke/rmlui_manifest.json"
    shell_routes_path = repo_root / "assets/ui/rml/shell/routes.json"
    write_json(
        manifest_path,
        {
            "schema": "worr.rmlui.smoke_manifest.v1",
            "routes": manifest_routes,
        },
    )
    write_json(
        shell_routes_path,
        {
            "schema": "worr.rml.agent4.routes.v1",
            "routes": shell_routes,
        },
    )
    return manifest_path, shell_routes_path


def run_checker(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
    manifest_routes: list[dict[str, Any]],
    shell_routes: list[dict[str, Any]],
) -> tuple[int, pytest.CaptureResult[str]]:
    manifest_path, shell_routes_path = write_manifests(repo_root, manifest_routes, shell_routes)
    result = coverage.main(
        [
            "--manifest",
            str(manifest_path),
            "--shell-routes",
            str(shell_routes_path),
            "--repo-root",
            str(repo_root),
        ]
    )
    return result, capsys.readouterr()


def test_valid_controller_stub_contract_coverage(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    write_text(
        repo_root / "assets/ui/rml/shell/main.rml",
        (
            '<rml><body><button id="options" data-route-target="options" '
            'data-command="pushmenu options" data-cvar="ui_menu" '
            'data-enable-if="ui_has_save">Options</button></body></rml>'
        ),
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [manifest_route("main")],
        [
            shell_route(
                "main",
                ["navigation", "command_action", "cvar_binding", "condition_state"],
            )
        ],
    )

    assert result == 0
    assert "controller_stub routes checked: 1" in captured.out
    assert "route metadata files checked: 1" in captured.out
    assert (
        "inferred categories: navigation=1, command_action=1, "
        "cvar_binding=1, condition_state=1"
    ) in captured.out
    assert "missing categories: none" in captured.out
    assert "coverage check passed" in captured.out


def test_missing_shell_route_fails(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    write_text(
        repo_root / "assets/ui/rml/shell/main.rml",
        '<rml><body><button id="options" data-route-target="options">Options</button></body></rml>',
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [manifest_route("main")],
        [],
    )

    assert result == 1
    assert "route 'main' is missing matching route metadata" in captured.out


def test_missing_controller_contracts_fails(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    write_text(
        repo_root / "assets/ui/rml/shell/main.rml",
        '<rml><body><button id="options" data-route-target="options">Options</button></body></rml>',
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [manifest_route("main")],
        [shell_route("main", None)],
    )

    assert result == 1
    assert "route 'main' route metadata is missing a non-empty controller_contracts list" in captured.out


def test_missing_inferred_category_fails(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    write_text(
        repo_root / "assets/ui/rml/shell/main.rml",
        '<rml><body><button id="close" data-command="ui.close">Close</button></body></rml>',
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [manifest_route("main")],
        [shell_route("main", ["navigation"])],
    )

    assert result == 1
    assert "missing categories: command_action=1" in captured.out
    assert "route 'main' infers controller category 'command_action'" in captured.out


def test_default_discovery_covers_shell_and_utility_route_metadata(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    write_text(
        repo_root / "assets/ui/rml/shell/main.rml",
        '<rml><body><button id="close" data-command="ui.close">Close</button></body></rml>',
    )
    write_text(
        repo_root / "assets/ui/rml/utility/keys.rml",
        (
            '<rml><body><button id="capture" data-bind-command="+attack" '
            'data-event-click="keybind.capture">Attack</button>'
            '<button id="legacy" data-command="pushmenu legacykeys" '
            'data-route-target="legacykeys">Legacy</button></body></rml>'
        ),
    )
    write_json(
        repo_root / "tools/ui_smoke/rmlui_manifest.json",
        {
            "schema": "worr.rmlui.smoke_manifest.v1",
            "routes": [
                manifest_route("main"),
                manifest_route("keys", "assets/ui/rml/utility/keys.rml"),
            ],
        },
    )
    write_json(
        repo_root / "assets/ui/rml/shell/routes.json",
        {
            "schema": "worr.rml.agent4.routes.v1",
            "routes": [
                shell_route("main", ["command_action"]),
            ],
        },
    )
    write_json(
        repo_root / "assets/ui/rml/utility/routes.json",
        {
            "schema": "worr.rml.agent5.routes.v1",
            "routes": [
                utility_route("keys", ["keybind", "command_action", "navigation"]),
            ],
        },
    )

    result = coverage.main(
        [
            "--manifest",
            str(repo_root / "tools/ui_smoke/rmlui_manifest.json"),
            "--repo-root",
            str(repo_root),
        ]
    )
    captured = capsys.readouterr()

    assert result == 0
    assert "route metadata files checked: 2" in captured.out
    assert "controller_stub routes checked: 2" in captured.out
    assert "inferred categories: navigation=1, command_action=2, keybind=1" in captured.out
    assert "covered categories: navigation=1, command_action=2, keybind=1" in captured.out
