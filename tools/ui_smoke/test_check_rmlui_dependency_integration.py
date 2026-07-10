from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_dependency_integration as dependency_integration  # noqa: E402


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def scaffold_source(runtime_default: str = "0") -> str:
    return f"""#include "ui_rml.h"

#ifndef UI_RML_HAS_RUNTIME
#define UI_RML_HAS_RUNTIME {runtime_default}
#endif

void UI_Rml_Init(void)
{{
    Cvar_Get("ui_rml_enable", "0", 0);
}}

bool UI_Rml_OpenMenu(void)
{{
#if UI_RML_HAS_RUNTIME
    return true;
#else
    return false;
#endif
}}
"""


def write_repo(
    repo_root: Path,
    *,
    meson_options: str = "",
    dependency_decl: str = "",
    define_decl: str = "",
    scaffold_exists: bool = True,
    scaffold_listed: bool = True,
    scaffold_runtime_default: str = "0",
    wrap_present: bool = False,
    source_dir_present: bool = False,
) -> None:
    client_src = ""
    if scaffold_listed:
        client_src = """
client_src = [
  'src/client/ui_rml/ui_rml.cpp',
]
"""
    write_text(
        repo_root / "meson.build",
        f"""{client_src}
{dependency_decl}
{define_decl}
""",
    )
    write_text(repo_root / "meson_options.txt", meson_options)
    (repo_root / "subprojects").mkdir(parents=True, exist_ok=True)

    if scaffold_exists:
        write_text(
            repo_root / "src/client/ui_rml/ui_rml.cpp",
            scaffold_source(scaffold_runtime_default),
        )
    if wrap_present:
        write_text(
            repo_root / "subprojects/rmlui.wrap",
            """[wrap-git]
url = https://github.com/mikke89/RmlUi.git
revision = test
""",
        )
    if source_dir_present:
        write_text(repo_root / "subprojects/rmlui/meson.build", "project('rmlui')\n")


def run_checker(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
    *,
    output_format: str = "text",
) -> tuple[int, pytest.CaptureResult[str]]:
    result = dependency_integration.main(
        [
            "--repo-root",
            str(repo_root),
            "--format",
            output_format,
        ]
    )
    return result, capsys.readouterr()


def test_scaffold_only_state_passes_without_dependency(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_repo(repo_root)

    result, captured = run_checker(repo_root, capsys)

    assert result == 0
    assert "State: scaffold-only" in captured.out
    assert "Components present: 1/4" in captured.out
    assert "Dependency source/wrap:" in captured.out
    assert "Status: absent" in captured.out
    assert "Runtime compiled: no" in captured.out
    assert "Result: RmlUi dependency integration check passed." in captured.out


def test_optional_meson_declaration_does_not_require_downloaded_source(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_repo(
        repo_root,
        meson_options="""option('rmlui',
  type: 'feature',
  value: 'disabled',
  description: 'RmlUi runtime support')
""",
        dependency_decl="rmlui_dep = dependency('rmlui', required: get_option('rmlui'))",
        define_decl="config.set10('UI_RML_HAS_RUNTIME', rmlui_dep.found())",
    )

    result, captured = run_checker(repo_root, capsys, output_format="json")

    payload = json.loads(captured.out)
    assert result == 0
    assert payload["ok"] is True
    assert payload["state"] == "optional"
    assert payload["dependency"]["status"] == "optional"
    assert payload["dependency"]["has_source_or_wrap"] is False
    assert payload["dependency"]["meson_declared"] is True
    assert payload["option"]["status"] == "optional"
    assert payload["define"]["status"] == "optional"
    assert payload["define"]["runtime_compiled"] is False
    assert payload["scaffold"]["status"] == "compiled-stub"
    assert payload["counts"]["components_present"] == 4
    assert payload["errors"] == []


def test_feature_option_variable_required_argument_is_optional(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_repo(
        repo_root,
        meson_options="""option('rmlui',
  type: 'feature',
  value: 'disabled',
  description: 'RmlUi runtime support')
""",
        dependency_decl="""rmlui_opt = get_option('rmlui')
rmlui_dep = dependency('rmlui', required: rmlui_opt)
""",
        define_decl="config.set10('UI_RML_HAS_RUNTIME', rmlui_dep.found())",
    )

    result, captured = run_checker(repo_root, capsys, output_format="json")

    payload = json.loads(captured.out)
    assert result == 0
    assert payload["dependency"]["status"] == "optional"
    assert payload["dependency"]["optional_declaration"] is True
    assert payload["dependency"]["enabled_declaration"] is False
    assert payload["errors"] == []


def test_rmlui_runtime_guarded_compile_args_are_optional(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_repo(
        repo_root,
        meson_options="""option('rmlui',
  type: 'feature',
  value: 'disabled',
  description: 'RmlUi runtime support')
""",
        dependency_decl="""rmlui_opt = get_option('rmlui')
rmlui_runtime = false
if rmlui_opt.allowed()
  rmlui_dep = dependency('rmlui', required: false)
  if rmlui_dep.found()
    rmlui_runtime = true
  endif
endif
""",
        define_decl="""if rmlui_runtime
  client_cpp_args += '-DUI_RML_HAS_RUNTIME=1'
  renderer_gl_cpp_args += '-DUI_RML_HAS_RUNTIME=1'
endif
""",
        wrap_present=True,
        source_dir_present=True,
    )

    result, captured = run_checker(repo_root, capsys, output_format="json")

    payload = json.loads(captured.out)
    assert result == 0
    assert payload["state"] == "optional"
    assert payload["define"]["status"] == "optional"
    assert payload["define"]["runtime_compiled"] is False
    assert payload["define"]["snippets"] == [
        "client_cpp_args += '-DUI_RML_HAS_RUNTIME=1'",
        "renderer_gl_cpp_args += '-DUI_RML_HAS_RUNTIME=1'",
    ]
    assert payload["errors"] == []


def test_declared_wrap_without_build_wiring_reports_warning(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_repo(repo_root, wrap_present=True)

    result, captured = run_checker(repo_root, capsys, output_format="json")

    payload = json.loads(captured.out)
    assert result == 0
    assert payload["ok"] is True
    assert payload["state"] == "declared"
    assert payload["dependency"]["status"] == "declared"
    assert payload["counts"]["components_present"] == 2
    assert payload["counts"]["wrap_files"] == 1
    assert payload["counts"]["warnings"] == 1
    assert "source/wrap exists" in payload["warnings"][0]
    assert payload["errors"] == []


def test_runtime_compiled_state_reports_enabled_define_and_source(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_repo(
        repo_root,
        meson_options="""option('rmlui',
  type: 'feature',
  value: 'enabled',
  description: 'RmlUi runtime support')
""",
        dependency_decl="rmlui_dep = dependency('rmlui', required: get_option('rmlui'))",
        define_decl="config.set10('UI_RML_HAS_RUNTIME', true)",
        wrap_present=True,
        source_dir_present=True,
    )

    result, captured = run_checker(repo_root, capsys)

    assert result == 0
    assert "State: runtime-compiled" in captured.out
    assert "Wrap files: subprojects/rmlui.wrap" in captured.out
    assert "Source dirs: subprojects/rmlui" in captured.out
    assert "Status: runtime-compiled" in captured.out
    assert "Runtime compiled: yes" in captured.out


def test_rmlui_option_without_dependency_declaration_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_repo(
        repo_root,
        meson_options="""option('rmlui',
  type: 'feature',
  value: 'disabled',
  description: 'RmlUi runtime support')
""",
    )

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "State: optional" in captured.out
    assert "Malformed findings: 1" in captured.out
    assert (
        "RmlUi Meson option is declared but no RmlUi dependency/subproject call uses it"
        in captured.out
    )


def test_optional_option_with_required_dependency_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_repo(
        repo_root,
        meson_options="""option('rmlui',
  type: 'feature',
  value: 'disabled',
  description: 'RmlUi runtime support')
""",
        dependency_decl="rmlui_dep = dependency('rmlui', required: true)",
        define_decl="config.set10('UI_RML_HAS_RUNTIME', rmlui_dep.found())",
    )

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "Malformed findings: 1" in captured.out
    assert "optional RmlUi Meson option is not reflected" in captured.out


def test_runtime_define_without_dependency_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_repo(
        repo_root,
        define_decl="config.set10('UI_RML_HAS_RUNTIME', true)",
    )

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "State: runtime-compiled" in captured.out
    assert "RmlUi runtime is compiled but no RmlUi dependency/subproject is declared" in captured.out


def test_missing_scaffold_source_listed_in_meson_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_repo(repo_root, scaffold_exists=False, scaffold_listed=True)

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "source is listed in meson.build but the file is missing" in captured.out
    assert "Result: RmlUi dependency integration check failed." in captured.out
