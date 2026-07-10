from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_command_inventory as command_inventory  # noqa: E402


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
                "schema": command_inventory.EXPECTED_SCHEMA,
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
    output_format: str = "text",
) -> tuple[int, pytest.CaptureResult[str]]:
    manifest_path = write_manifest(repo_root, routes)
    result = command_inventory.main(
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


def test_direct_commands_are_inventoried(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/shell/main.rml",
        """
<rml>
  <body>
    <button id="main-options" data-command="pushmenu options">Options</button>
    <button id="main-close" data-command="ui.close">Close</button>
  </body>
</rml>
""",
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("main", "assets/ui/rml/shell/main.rml")],
    )

    assert result == 0
    assert "Routes known: 1" in captured.out
    assert "Documents checked: present=1, missing=0" in captured.out
    assert "Direct command refs: 2" in captured.out
    assert "Unique command tokens: 2" in captured.out
    assert "Routes with command hooks: 1" in captured.out
    assert "Result: RmlUi command inventory check passed." in captured.out


def test_semicolon_command_chains_preserve_quoted_semicolons(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/singleplayer/startserver.rml",
        """
<rml>
  <body>
    <button id="begin"
            data-command='forcemenuoff; if $coop == 1 then "deathmatch 0; coop 1"; map base1'>
      Begin
    </button>
  </body>
</rml>
""",
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("startserver", "assets/ui/rml/singleplayer/startserver.rml")],
    )

    assert result == 0
    assert "Direct command refs: 1" in captured.out
    assert "Unique command tokens: 3" in captured.out
    assert "Malformed/empty command attributes: 0" in captured.out
    assert "Command tokens: forcemenuoff, if, map" in captured.out


def test_empty_and_malformed_command_attributes_fail(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/shell/game.rml",
        """
<rml>
  <body>
    <button id="empty" data-command="   ">Empty</button>
    <button id="bad-chain" data-command="foo;;bar;">Bad Chain</button>
  </body>
</rml>
""",
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("game", "assets/ui/rml/shell/game.rml")],
    )

    assert result == 1
    assert "Direct command refs: 2" in captured.out
    assert "Unique command tokens: 2" in captured.out
    assert "Malformed/empty command attributes: 2" in captured.out
    assert "empty command attribute" in captured.out
    assert "empty command chain segment(s): 2, 4" in captured.out
    assert "Result: RmlUi command inventory check failed." in captured.out


def test_cvar_command_refs_are_validated(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/utility/ui_list.rml",
        """
<rml>
  <body>
    <button id="item-0" data-command-cvar="ui_list_item_cmd_0">Item</button>
    <button id="item-1" data-command-cvar="BadCommandCvar">Bad</button>
  </body>
</rml>
""",
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("ui_list", "assets/ui/rml/utility/ui_list.rml")],
    )

    assert result == 1
    assert "Cvar-command refs: 2" in captured.out
    assert "Unique cvar-command refs: 1" in captured.out
    assert "Malformed/empty command attributes: 1" in captured.out
    assert "command-cvar must use lowercase snake_case cvar token style" in captured.out


def test_json_output_reports_inventory_facts(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/session/tourney_replay_confirm.rml",
        """
<rml>
  <body>
    <button id="yes" data-command-cvar="ui_tourney_replay_yes_cmd">YES</button>
    <button id="no" data-command="popmenu">NO</button>
  </body>
</rml>
""",
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("tourney_replay_confirm", "assets/ui/rml/session/tourney_replay_confirm.rml")],
        output_format="json",
    )

    payload = json.loads(captured.out)
    assert result == 0
    assert captured.err == ""
    assert payload["ok"] is True
    assert payload["route_count"] == 1
    assert payload["documents_checked"] == 1
    assert payload["direct_command_refs"] == 1
    assert payload["cvar_command_refs"] == 1
    assert payload["unique_command_tokens"] == 1
    assert payload["unique_cvar_command_refs"] == 1
    assert payload["malformed_command_attributes"] == 0
    assert payload["routes_with_command_hooks"] == ["tourney_replay_confirm"]
    assert payload["command_tokens"] == ["popmenu"]
    assert payload["command_cvars"] == ["ui_tourney_replay_yes_cmd"]
    assert payload["problems"] == []
    assert payload["errors"] == []
