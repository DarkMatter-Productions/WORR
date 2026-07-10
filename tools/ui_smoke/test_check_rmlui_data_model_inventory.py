from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_data_model_inventory as data_model_inventory  # noqa: E402


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
                "schema": data_model_inventory.EXPECTED_SCHEMA,
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
    result = data_model_inventory.main(
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


def test_data_model_bind_and_options_are_inventoried(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/utility/players.rml",
        """
<rml>
  <body>
    <section id="players" data-model="player_config">
      <input id="name" data-bind="player_config.name" />
      <select id="model" data-bind="player_config.model" data-options="player_config.models" />
    </section>
  </body>
</rml>
""",
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("players", "assets/ui/rml/utility/players.rml")],
    )

    assert result == 0
    assert "Routes known: 1" in captured.out
    assert "Documents checked: present=1, missing=0" in captured.out
    assert "Total model/data-binding refs: 4" in captured.out
    assert "Unique model tokens: 4" in captured.out
    assert "Routes with data-model hooks: 1" in captured.out
    assert "Result: RmlUi data-model inventory check passed." in captured.out


def test_components_and_controllers_are_counted_separately(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/session/map_selector.rml",
        """
<rml>
  <body>
    <section id="options"
             data-model="session.map_selector.options"
             data-component="common.components.selector_list"
             data-controller="session.map_selector">
    </section>
  </body>
</rml>
""",
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("map_selector", "assets/ui/rml/session/map_selector.rml")],
    )

    assert result == 0
    assert "Total model/data-binding refs: 1" in captured.out
    assert "Component refs: 1" in captured.out
    assert "Controller refs: 1" in captured.out
    assert "Action-type refs: 0" in captured.out
    assert "Slot refs: 0" in captured.out


def test_action_slot_group_and_bind_command_refs_are_counted(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/utility/keys.rml",
        """
<rml>
  <body>
    <section id="keys" data-model="utility.keybinds" data-action-type="capture">
      <section id="combat" data-bind-group="combat">
        <button id="attack" data-bind-command="+attack" data-slot="primary">Attack</button>
        <button id="shotgun" data-bind-command="use Super Shotgun" data-slot="alternate">Shotgun</button>
      </section>
    </section>
  </body>
</rml>
""",
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("keys", "assets/ui/rml/utility/keys.rml")],
    )

    assert result == 0
    assert "Total model/data-binding refs: 4" in captured.out
    assert "Action-type refs: 1" in captured.out
    assert "Slot refs: 2" in captured.out
    assert "Malformed tokens: 0" in captured.out


def test_malformed_tokens_fail_with_inventory_context(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/bad.rml",
        """
<rml>
  <body>
    <section id="bad-model" data-model="Bad Model!"></section>
    <section id="bad-controller" data-controller=""></section>
    <button id="bad-command" data-bind-command="use Blaster; quit"></button>
  </body>
</rml>
""",
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("bad", "assets/ui/rml/bad.rml")],
    )

    assert result == 1
    assert "Malformed tokens: 3" in captured.out
    assert "data-model has malformed data-model token 'Bad Model!'" in captured.out
    assert "data-controller has malformed data-model token ''" in captured.out
    assert "data-bind-command has malformed data-model token 'use Blaster; quit'" in captured.out
    assert "Result: RmlUi data-model inventory check failed." in captured.out


def test_json_output_reports_inventory_facts(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/singleplayer/savegame.rml",
        """
<rml>
  <body>
    <main id="savegame"
          data-component="common.components.save_load"
          data-model="singleplayer.savegame_slots"
          data-action-type="savegame">
      <button id="slot-0" data-bind="singleplayer.savegame_slots.0" data-slot="primary">Slot</button>
    </main>
  </body>
</rml>
""",
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("savegame", "assets/ui/rml/singleplayer/savegame.rml")],
        output_format="json",
    )

    payload = json.loads(captured.out)
    assert result == 0
    assert captured.err == ""
    assert payload["ok"] is True
    assert payload["route_count"] == 1
    assert payload["documents_checked"] == 1
    assert payload["total_model_binding_refs"] == 2
    assert payload["unique_model_tokens"]["tokens"] == [
        "singleplayer.savegame_slots",
        "singleplayer.savegame_slots.0",
    ]
    assert payload["component_refs"] == 1
    assert payload["controller_refs"] == 0
    assert payload["action_type_refs"] == 1
    assert payload["slot_refs"] == 1
    assert payload["routes_with_data_model_hooks"]["routes"] == ["savegame"]
    assert payload["malformed_tokens"] == []
    assert payload["errors"] == []
