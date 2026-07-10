from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_cvar_inventory as cvar_inventory  # noqa: E402


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
                "schema": cvar_inventory.EXPECTED_SCHEMA,
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
    result = cvar_inventory.main(
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


def test_direct_cvar_refs_are_inventoried(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/settings/video.rml",
        """
<rml>
  <body>
    <input id="fullscreen" data-cvar="r_fullscreen" />
    <span id="gamma-value" data-bind-cvar="r_gamma"></span>
  </body>
</rml>
""",
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("video", "assets/ui/rml/settings/video.rml")],
    )

    assert result == 0
    assert "Routes known: 1" in captured.out
    assert "Direct cvar refs: 2" in captured.out
    assert "Unique cvars: 2" in captured.out
    assert "Routes with cvar hooks: 1" in captured.out
    assert "Result: RmlUi cvar inventory check passed." in captured.out


def test_label_and_command_cvar_refs_are_counted_separately(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/session/replay.rml",
        """
<rml>
  <body>
    <button id="replay-yes" data-label-cvar="ui_replay_yes" data-command-cvar="ui_replay_yes_cmd">Yes</button>
  </body>
</rml>
""",
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("replay", "assets/ui/rml/session/replay.rml")],
    )

    assert result == 0
    assert "Label cvar refs: 1" in captured.out
    assert "Command cvar refs: 1" in captured.out
    assert "Unique cvar tokens: ui_replay_yes, ui_replay_yes_cmd" in captured.out


def test_condition_expressions_extract_conservative_tokens(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/utility/ui_list.rml",
        """
<rml>
  <body>
    <button id="item-0" data-visible-if="ui_list_item_show_0=1">Item</button>
    <button id="save" data-enable-if="ingame;deathmatch=0;coop=0">Save</button>
  </body>
</rml>
""",
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("ui_list", "assets/ui/rml/utility/ui_list.rml")],
    )

    assert result == 0
    assert "Condition cvar refs: 4" in captured.out
    assert "Unknown/bad tokens: 0" in captured.out
    assert "coop" in captured.out
    assert "ui_list_item_show_0" in captured.out


def test_malformed_tokens_fail_with_inventory_context(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/settings/bad.rml",
        """
<rml>
  <body>
    <input id="bad-cvar" data-cvar="BadCvar" />
    <button id="bad-condition" data-visible-if="ui-good=1">Bad</button>
  </body>
</rml>
""",
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("bad", "assets/ui/rml/settings/bad.rml")],
    )

    assert result == 1
    assert "Unknown/bad tokens: 2" in captured.out
    assert "data-cvar has unsupported cvar token 'BadCvar'" in captured.out
    assert "data-visible-if has unsupported cvar token 'ui-good=1'" in captured.out


def test_json_output_reports_inventory_facts(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/shell/download_status.rml",
        """
<rml>
  <body>
    <p id="file" data-label-cvar="ui_download_file" data-show-if="ui_download_file"></p>
    <button id="cancel" data-command-cvar="ui_download_cancel_cmd"></button>
  </body>
</rml>
""",
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("download_status", "assets/ui/rml/shell/download_status.rml")],
        output_format="json",
    )

    payload = json.loads(captured.out)
    assert result == 0
    assert captured.err == ""
    assert payload["ok"] is True
    assert payload["route_count"] == 1
    assert payload["documents_checked"] == 1
    assert payload["references"] == {
        "direct": 0,
        "label": 1,
        "command": 1,
        "condition": 1,
        "total": 3,
    }
    assert payload["unique_cvars"]["tokens"] == [
        "ui_download_cancel_cmd",
        "ui_download_file",
    ]
    assert payload["routes_with_cvar_hooks"]["routes"] == ["download_status"]
    assert payload["unknown_or_bad_tokens"] == []


def test_dynamic_placeholders_are_skipped_without_counting_as_cvars(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/components/field.rml",
        """
<rml>
  <body>
    <input id="template-field" data-cvar="{{cvar}}" data-visible-if="{{visible_if}}" />
  </body>
</rml>
""",
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("field", "assets/ui/rml/components/field.rml")],
    )

    assert result == 0
    assert "Direct cvar refs: 0" in captured.out
    assert "Condition cvar refs: 0" in captured.out
    assert "Dynamic values skipped: 2" in captured.out
