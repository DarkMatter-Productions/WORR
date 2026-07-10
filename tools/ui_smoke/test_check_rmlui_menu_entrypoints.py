from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_menu_entrypoints as menu_entrypoints  # noqa: E402


@pytest.fixture
def repo_root(tmp_path: Path) -> Path:
    root = tmp_path / "repo"
    root.mkdir()
    return root


def write_manifest(repo_root: Path, routes: list[dict[str, Any]]) -> Path:
    manifest_path = repo_root / "tools/ui_smoke/rmlui_manifest.json"
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        json.dumps(
            {
                "schema": "worr.rmlui.smoke_manifest.v1",
                "routes": routes,
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    return manifest_path


def write_cpp(
    repo_root: Path,
    registered_routes: list[tuple[str, str]],
    route_for_menu_body: str,
) -> Path:
    cpp_path = repo_root / "src/client/ui_rml/ui_rml.cpp"
    cpp_path.parent.mkdir(parents=True, exist_ok=True)
    entries = "\n".join(
        f'    {{ "{route_id}", "{document}" }},'
        for route_id, document in registered_routes
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
                "const char *UI_Rml_RouteForMenu(uiMenu_t menu)",
                "{",
                route_for_menu_body,
                "}",
                "",
            ]
        ),
        encoding="utf-8",
    )
    return cpp_path


def manifest_route(route_id: str, document: str | None = None) -> dict[str, Any]:
    if document is None:
        document = f"assets/ui/rml/shell/{route_id}.rml"
    return {
        "id": route_id,
        "document": document,
    }


def menu_switch(*cases: str) -> str:
    return "\n".join(["    switch (menu) {", *cases, "    }"])


def run_checker(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
    manifest_routes: list[dict[str, Any]],
    registered_routes: list[tuple[str, str]],
    route_for_menu_body: str,
) -> tuple[int, pytest.CaptureResult[str]]:
    manifest_path = write_manifest(repo_root, manifest_routes)
    cpp_path = write_cpp(repo_root, registered_routes, route_for_menu_body)

    result = menu_entrypoints.main(
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


def test_valid_menu_entrypoints_accept_manifest_and_registry_matches(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    result, captured = run_checker(
        repo_root,
        capsys,
        [
            manifest_route("main"),
            manifest_route("game"),
            manifest_route("download_status"),
        ],
        [
            ("main", "shell/main.rml"),
            ("game", "shell/game.rml"),
            ("download_status", "shell/download_status.rml"),
        ],
        menu_switch(
            "    case UIMENU_DEFAULT:",
            "    case UIMENU_MAIN:",
            '        return "main";',
            "    case UIMENU_GAME:",
            '        return "game";',
            "    case UIMENU_DOWNLOAD:",
            '        return "download_status";',
            "    case UIMENU_NONE:",
            "    default:",
            "        return NULL;",
        ),
    )

    assert result == 0
    assert "Menu cases checked: 5" in captured.out
    assert "Mapped routes: 4" in captured.out
    assert "Unique mapped routes: 3" in captured.out
    assert "Manifest matches: 3" in captured.out
    assert "Registry matches: 3" in captured.out
    assert "Result: RmlUi menu entrypoint check passed." in captured.out


def test_missing_manifest_route_fails(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    result, captured = run_checker(
        repo_root,
        capsys,
        [manifest_route("main")],
        [
            ("main", "shell/main.rml"),
            ("game", "shell/game.rml"),
        ],
        menu_switch(
            "    case UIMENU_MAIN:",
            '        return "main";',
            "    case UIMENU_GAME:",
            '        return "game";',
        ),
    )

    assert result == 1
    assert "Manifest matches: 1" in captured.out
    assert "mapped route 'game' is missing from RmlUi smoke manifest" in captured.out


def test_missing_registry_route_fails(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    result, captured = run_checker(
        repo_root,
        capsys,
        [
            manifest_route("main"),
            manifest_route("game"),
        ],
        [("main", "shell/main.rml")],
        menu_switch(
            "    case UIMENU_MAIN:",
            '        return "main";',
            "    case UIMENU_GAME:",
            '        return "game";',
        ),
    )

    assert result == 1
    assert "Registry matches: 1" in captured.out
    assert "mapped route 'game' is missing from ui_rml_routes" in captured.out


def test_fallthrough_cases_share_the_next_return_route(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    result, captured = run_checker(
        repo_root,
        capsys,
        [manifest_route("main")],
        [("main", "shell/main.rml")],
        menu_switch(
            "    case UIMENU_DEFAULT:",
            "    case UIMENU_MAIN:",
            "    case UIMENU_GAME:",
            '        return "main";',
            "    case UIMENU_NONE:",
            "    default:",
            "        return NULL;",
        ),
    )

    assert result == 0
    assert "Menu cases checked: 4" in captured.out
    assert "Mapped routes: 3" in captured.out
    assert "Unique mapped routes: 1" in captured.out
    assert "Manifest matches: 1" in captured.out
    assert "Registry matches: 1" in captured.out


def test_path_mismatch_fails(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    result, captured = run_checker(
        repo_root,
        capsys,
        [manifest_route("main", "assets/ui/rml/shell/main.rml")],
        [("main", "settings/main.rml")],
        menu_switch(
            "    case UIMENU_MAIN:",
            '        return "main";',
        ),
    )

    assert result == 1
    assert "mapped route 'main' runtime path mismatch: " in captured.out
    assert "ui/rml/settings/main.rml != ui/rml/shell/main.rml" in captured.out


def test_duplicate_contradictory_menu_mapping_fails(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    result, captured = run_checker(
        repo_root,
        capsys,
        [
            manifest_route("main"),
            manifest_route("game"),
        ],
        [
            ("main", "shell/main.rml"),
            ("game", "shell/game.rml"),
        ],
        menu_switch(
            "    case UIMENU_MAIN:",
            '        return "main";',
            "    case UIMENU_MAIN:",
            '        return "game";',
        ),
    )

    assert result == 1
    assert "duplicate contradictory mapping for UIMENU_MAIN" in captured.out
