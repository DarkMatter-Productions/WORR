from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_dependency_decision as dependency_decision  # noqa: E402


def valid_record(
    status_line: str = "Decision status: active; native renderer/runtime route ownership not implemented.",
) -> str:
    return f"""# RmlUi Dependency Decision Record

Task IDs: `FR-09-T02`, `FR-09-T03`, `DV-06-T01`, `DV-03-T07`, `DV-07-T04`

{status_line}

Round 17 landed a compiled RmlUi Core adapter and optional RmlUi Core
compile/link evidence through a default-disabled enabled scratch build. Round
18 landed a WORR-backed RmlUi SystemInterface and FileInterface system/file
bridge. Runtime file probes use FS_OpenFile, FS_Read, FS_Seek, FS_Tell,
FS_Length, and FS_CloseFile through WORR's filesystem/package search path.
The explicit ui_rml_runtime_probe command validates runtime-facing file probe
behavior.

No native renderer bridge is implemented by this record. No route opens or
draws through RmlUi at runtime. No runtime switch is enabled by default by this
record. No Vulkan renderer path is redirected to OpenGL. No legacy JSON path is
removed or deprecated by this record.

## Required Gate G1 Interfaces

| Interface | Required proof before Gate G1 |
| --- | --- |
| System | RmlUi time, logging, allocation/error reporting, and shutdown order use WORR services. |
| File | RmlUi document, style, image, and font loads resolve through WORR filesystem paths. |
| Input | Keyboard, mouse, focus, and gamepad navigation translate through WORR input ownership. |
| Font/text | Font source, fallback policy, localization strings, and text overflow are validated. |
| Runtime route | A runtime route handles a command button, a cvar-backed control, and a conditional element. |

## Native Renderer Requirement

OpenGL requires OpenGL-native renderer bridge proof.
Vulkan requires Vulkan-native renderer bridge proof.
RTX/vkpt requires renderer proof through the Vulkan RTX/path-tracing path.
There must be no Vulkan-to-OpenGL fallback.

## Validation Checklist

```text
python tools\\ui_smoke\\check_rmlui_manifest.py
python tools\\ui_smoke\\report_rmlui_progress.py --format json
python tools\\package_assets.py --assets-dir assets --install-dir .tmp\\rmlui\\dependency-validation --base-game basew --archive-name pak0.pkz
python tools\\ui_smoke\\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\\rmlui\\dependency-validation --base-game basew --format json
```

## Build and runtime checks

Capture evidence for OpenGL, Vulkan, RTX/vkpt, escape/back behavior, command
buttons, cvar-backed controls, conditional elements, and clean fallback.
"""


def write_record(repo_root: Path, text: str) -> Path:
    path = repo_root / dependency_decision.DEFAULT_RECORD
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def run_checker(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
    *,
    output_format: str = "text",
    record_path: Path | None = None,
) -> tuple[int, pytest.CaptureResult[str]]:
    args = [
        "--repo-root",
        str(repo_root),
        "--format",
        output_format,
    ]
    if record_path is not None:
        args.extend(["--record", str(record_path)])
    result = dependency_decision.main(args)
    return result, capsys.readouterr()


def test_valid_minimal_record_passes(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_record(repo_root, valid_record())

    result, captured = run_checker(repo_root, capsys)

    assert result == 0
    assert "Required task IDs: 5/5" in captured.out
    assert "Implementation boundary: 5/5" in captured.out
    assert "Remaining guardrails: 5/5" in captured.out
    assert "Native renderer obligations: 4/4" in captured.out
    assert "Gate G1 interfaces: 5/5" in captured.out
    assert "Validation evidence: 3/3" in captured.out
    assert "Result: RmlUi dependency decision check passed." in captured.out


def test_missing_record_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "Exists: no" in captured.out
    assert "dependency decision record does not exist" in captured.out
    assert "Result: RmlUi dependency decision check failed." in captured.out


def test_status_overclaim_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_record(repo_root, valid_record("Decision status: completed and implemented."))

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "Active status: no" in captured.out
    assert "Proposed status: no" in captured.out
    assert "Not-implemented status: no" in captured.out
    assert "Status overclaims: 1" in captured.out
    assert "decision status overclaims completion/default enablement" in captured.out


def test_missing_boundary_and_guardrail_wording_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    text = valid_record().replace(
        "Round 17 landed a compiled RmlUi Core adapter and optional RmlUi Core\n"
        "compile/link evidence through a default-disabled enabled scratch build. Round\n"
        "18 landed a WORR-backed RmlUi SystemInterface and FileInterface system/file\n"
        "bridge. Runtime file probes use FS_OpenFile, FS_Read, FS_Seek, FS_Tell,\n"
        "FS_Length, and FS_CloseFile through WORR's filesystem/package search path.\n"
        "The explicit ui_rml_runtime_probe command validates runtime-facing file probe\n"
        "behavior.\n\n"
        "No native renderer bridge is implemented by this record. No route opens or\n"
        "draws through RmlUi at runtime. No runtime switch is enabled by default by this\n"
        "record. No Vulkan renderer path is redirected to OpenGL. No legacy JSON path is\n"
        "removed or deprecated by this record.\n",
        "This record describes a future integration path.\n",
    )
    write_record(repo_root, text)

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "Implementation boundary: 0/5" in captured.out
    assert "Remaining guardrails:" in captured.out
    assert "implementation boundary evidence: missing compiled RmlUi Core adapter evidence" in captured.out
    assert "implementation boundary evidence: missing runtime file-probe command evidence" in captured.out
    assert "remaining guardrails: missing legacy JSON path remains intact" in captured.out


def test_missing_native_renderer_obligations_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    text = valid_record().replace(
        "OpenGL requires OpenGL-native renderer bridge proof.\n"
        "Vulkan requires Vulkan-native renderer bridge proof.\n"
        "RTX/vkpt requires renderer proof through the Vulkan RTX/path-tracing path.\n"
        "There must be no Vulkan-to-OpenGL fallback.\n",
        "Renderer work will be handled later.\n",
    )
    write_record(repo_root, text)

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "Native renderer obligations: 0/4" in captured.out
    assert "native renderer obligations: missing OpenGL-native renderer proof" in captured.out
    assert "native renderer obligations: missing Vulkan-native renderer proof" in captured.out
    assert "native renderer obligations: missing RTX/vkpt renderer proof" in captured.out
    assert "native renderer obligations: missing Vulkan must not fall back to OpenGL" in captured.out


def test_missing_gate_g1_areas_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    text = valid_record().replace(
        """| Interface | Required proof before Gate G1 |
| --- | --- |
| System | RmlUi time, logging, allocation/error reporting, and shutdown order use WORR services. |
| File | RmlUi document, style, image, and font loads resolve through WORR filesystem paths. |
| Input | Keyboard, mouse, focus, and gamepad navigation translate through WORR input ownership. |
| Font/text | Font source, fallback policy, localization strings, and text overflow are validated. |
| Runtime route | A runtime route handles a command button, a cvar-backed control, and a conditional element. |
""",
        "Gate G1 needs integration proof before landing.\n",
    )
    write_record(repo_root, text)

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "Gate G1 interfaces: 0/5" in captured.out
    assert "Gate G1 interfaces: missing Gate G1 system interface" in captured.out
    assert "Gate G1 interfaces: missing Gate G1 file interface" in captured.out
    assert "Gate G1 interfaces: missing Gate G1 input interface" in captured.out
    assert "Gate G1 interfaces: missing Gate G1 font/text interface" in captured.out
    assert "Gate G1 interfaces: missing Gate G1 runtime route/controller integration" in captured.out


def test_json_output_reports_decision_facts(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_record(repo_root, valid_record())

    result, captured = run_checker(repo_root, capsys, output_format="json")

    payload = json.loads(captured.out)
    assert result == 0
    assert captured.err == ""
    assert payload["ok"] is True
    assert payload["exists"] is True
    assert payload["required_task_ids"] == list(dependency_decision.REQUIRED_TASK_IDS)
    assert payload["missing_task_ids"] == []
    assert payload["has_active_status"] is True
    assert payload["has_proposed_status"] is False
    assert payload["has_not_implemented_status"] is True
    assert payload["status_overclaim_count"] == 0
    assert payload["implementation_boundary_passed"] == 5
    assert payload["remaining_guardrails_passed"] == 5
    assert payload["native_renderer_passed"] == 4
    assert payload["gate_g1_interfaces_passed"] == 5
    assert payload["validation_evidence_passed"] == 3
    assert payload["errors"] == []
