from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_runtime_stub_eligibility as eligibility  # noqa: E402


def write_json(path: Path, data: object) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    return path


def write_cpp(
    path: Path,
    registered_routes: list[tuple[str, str]],
    menu_returns: dict[str, list[str]],
    *,
    include_probe_fallback: bool = True,
    include_guarded_open_internal: bool = False,
) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    route_entries = "\n".join(
        f'    {{ "{route_id}", "{document}" }},'
        for route_id, document in registered_routes
    )
    menu_cases: list[str] = []
    for route_id, cases in menu_returns.items():
        for case_name in cases:
            menu_cases.append(f"    case {case_name}:")
        menu_cases.append(f'        return "{route_id}";')

    open_menu_lines = [
        "bool UI_Rml_OpenMenu(uiMenu_t menu)",
        "{",
        "    const char *route = UI_Rml_RouteForMenu(menu);",
        "    if (!route || !UI_Rml_IsEnabled()) {",
        "        return false;",
        "    }",
    ]
    if include_probe_fallback:
        open_menu_lines.extend(
            [
                "    UI_Rml_ProbeRoute(route);",
                '    Com_Printf("falling back to legacy UI.");',
                "    return false;",
            ]
        )
    elif include_guarded_open_internal:
        open_menu_lines.append("    return UI_Rml_OpenRouteInternal(route);")
    open_menu_lines.append("}")

    open_internal_lines: list[str] = []
    if include_guarded_open_internal:
        open_internal_lines = [
            "static bool UI_Rml_OpenRouteInternal(const char *route_id)",
            "{",
            "    const ui_rml_route_t *route = UI_Rml_FindRoute(route_id);",
            "    bool document_found = UI_Rml_ProbeRoute(route->id);",
            "    if (!document_found) {",
            "        return false;",
            "    }",
            "    return ui_rml_runtime.OpenRoute(route->id, UI_Rml_DocumentForRoute(route->id));",
            "}",
            "",
        ]

    path.write_text(
        "\n".join(
            [
                "typedef struct {",
                "    const char *id;",
                "    const char *document;",
                "} ui_rml_route_t;",
                "",
                "static const ui_rml_route_t ui_rml_routes[] = {",
                route_entries,
                "};",
                "static const ui_rml_route_t *UI_Rml_FindRoute(const char *route_id) { return ui_rml_routes; }",
                "static const char *UI_Rml_DocumentForRoute(const char *route_id) { return \"ui/rml/shell/main.rml\"; }",
                "",
                "const char *UI_Rml_RouteForMenu(uiMenu_t menu)",
                "{",
                "    switch (menu) {",
                *menu_cases,
                "    default:",
                "        return NULL;",
                "    }",
                "}",
                "",
                *open_internal_lines,
                *open_menu_lines,
                "",
            ]
        ),
        encoding="utf-8",
    )
    return path


def smoke_manifest(routes: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "schema": "worr.rmlui.smoke_manifest.v1",
        "routes": routes,
    }


def shell_manifest(routes: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "schema": "worr.rml.agent4.routes.v1",
        "routes": routes,
    }


def manifest_route(
    route_id: str,
    migration_phase: str,
    document: str = "assets/ui/rml/shell/main.rml",
) -> dict[str, Any]:
    return {
        "id": route_id,
        "document": document,
        "migration_phase": migration_phase,
    }


def shell_route(
    route_id: str,
    *,
    migration_phase: str = "runtime_stub",
    controller_contracts: object | None = None,
    document: str = "shell/main.rml",
) -> dict[str, Any]:
    route: dict[str, Any] = {
        "id": route_id,
        "document": document,
        "migration_phase": migration_phase,
    }
    if controller_contracts is None:
        controller_contracts = [
            {
                "category": "navigation",
                "contract": "worr.rml.controller.navigation.mock",
                "fixture": "navigation.mock.json",
                "model": "ui.navigation",
                "status": "mock_fixture",
            }
        ]
    route["controller_contracts"] = controller_contracts
    return route


def run_checker(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
    *,
    manifest_routes: list[dict[str, Any]],
    shell_routes: list[dict[str, Any]],
    registered_routes: list[tuple[str, str]],
    menu_returns: dict[str, list[str]],
    include_probe_fallback: bool = True,
    include_guarded_open_internal: bool = False,
) -> tuple[int, pytest.CaptureResult[str]]:
    manifest_path = write_json(
        repo_root / "tools/ui_smoke/rmlui_manifest.json",
        smoke_manifest(manifest_routes),
    )
    shell_path = write_json(
        repo_root / "assets/ui/rml/shell/routes.json",
        shell_manifest(shell_routes),
    )
    cpp_path = write_cpp(
        repo_root / "src/client/ui_rml/ui_rml.cpp",
        registered_routes,
        menu_returns,
        include_probe_fallback=include_probe_fallback,
        include_guarded_open_internal=include_guarded_open_internal,
    )

    result = eligibility.main(
        [
            "--manifest",
            str(manifest_path),
            "--shell-routes",
            str(shell_path),
            "--cpp",
            str(cpp_path),
            "--repo-root",
            str(repo_root),
        ]
    )
    return result, capsys.readouterr()


def test_zero_runtime_stub_routes_passes(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        manifest_routes=[
            manifest_route("main", "controller_stub"),
        ],
        shell_routes=[],
        registered_routes=[],
        menu_returns={},
    )

    assert result == 0
    assert "Runtime_stub routes checked: 0" in captured.out
    assert "Menu-mapped routes: 0" in captured.out
    assert "Registry matches: 0" in captured.out
    assert "Controller contract matches: 0" in captured.out
    assert "Result: RmlUi runtime_stub eligibility check passed." in captured.out


def test_valid_runtime_stub_route_passes(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        manifest_routes=[
            manifest_route("main", "runtime_stub"),
        ],
        shell_routes=[
            shell_route("main"),
        ],
        registered_routes=[
            ("main", "shell/main.rml"),
        ],
        menu_returns={
            "main": ["UIMENU_DEFAULT", "UIMENU_MAIN"],
        },
    )

    assert result == 0
    assert "Runtime_stub routes checked: 1" in captured.out
    assert "Menu-mapped routes: 1" in captured.out
    assert "Registry matches: 1" in captured.out
    assert "Controller contract matches: 1" in captured.out


def test_valid_runtime_stub_route_can_delegate_to_guarded_open_internal(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        manifest_routes=[
            manifest_route("main", "runtime_stub"),
        ],
        shell_routes=[
            shell_route("main"),
        ],
        registered_routes=[
            ("main", "shell/main.rml"),
        ],
        menu_returns={
            "main": ["UIMENU_DEFAULT", "UIMENU_MAIN"],
        },
        include_probe_fallback=False,
        include_guarded_open_internal=True,
    )

    assert result == 0
    assert "Runtime_stub routes checked: 1" in captured.out
    assert "Menu-mapped routes: 1" in captured.out
    assert "Registry matches: 1" in captured.out
    assert "Controller contract matches: 1" in captured.out


def test_missing_menu_mapping_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        manifest_routes=[
            manifest_route("main", "runtime_stub"),
        ],
        shell_routes=[
            shell_route("main"),
        ],
        registered_routes=[
            ("main", "shell/main.rml"),
        ],
        menu_returns={},
    )

    assert result == 1
    assert "Menu-mapped routes: 0" in captured.out
    assert (
        "runtime_stub route 'main' is not returned by UI_Rml_RouteForMenu "
        "for any UIMENU_* case"
    ) in captured.out


def test_missing_controller_contracts_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        manifest_routes=[
            manifest_route("main", "runtime_stub"),
        ],
        shell_routes=[
            shell_route("main", controller_contracts=[]),
        ],
        registered_routes=[
            ("main", "shell/main.rml"),
        ],
        menu_returns={
            "main": ["UIMENU_MAIN"],
        },
    )

    assert result == 1
    assert "Controller contract matches: 0" in captured.out
    assert "runtime_stub route 'main' shell metadata must keep non-empty controller_contracts" in captured.out


def test_registry_path_mismatch_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        manifest_routes=[
            manifest_route("main", "runtime_stub"),
        ],
        shell_routes=[
            shell_route("main"),
        ],
        registered_routes=[
            ("main", "settings/main.rml"),
        ],
        menu_returns={
            "main": ["UIMENU_MAIN"],
        },
    )

    assert result == 1
    assert "Registry matches: 0" in captured.out
    assert (
        "registered route 'main' runtime path mismatch: "
        "ui/rml/settings/main.rml != ui/rml/shell/main.rml"
    ) in captured.out
