#!/usr/bin/env python3
"""Validate static RmlUi dependency/build integration wiring."""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable


DEFAULT_MESON_BUILD = Path("meson.build")
DEFAULT_MESON_OPTIONS = Path("meson_options.txt")
DEFAULT_SUBPROJECTS_DIR = Path("subprojects")
DEFAULT_SCAFFOLD_SOURCE = Path("src/client/ui_rml/ui_rml.cpp")
RML_RUNTIME_DEFINE = "UI_RML_HAS_RUNTIME"
RML_DEFINE_NAMES = (
    RML_RUNTIME_DEFINE,
    "USE_RMLUI",
    "USE_RML_UI",
    "USE_RMLUI_RUNTIME",
)
RML_NAME_RE = re.compile(r"rml[-_]?ui|ui[-_]?rml|rml", re.IGNORECASE)
PATH_SEP_RE = re.compile(r"[\\/]+")


@dataclass(frozen=True)
class MesonCall:
    function: str
    text: str
    first_arg: str
    line: int
    start: int


@dataclass(frozen=True)
class MesonOptionFact:
    name: str
    option_type: str
    default: str
    status: str
    line: int


@dataclass
class DependencySourceFacts:
    status: str = "absent"
    wrap_files: list[str] = field(default_factory=list)
    source_dirs: list[str] = field(default_factory=list)
    meson_declarations: list[str] = field(default_factory=list)
    declaration_lines: list[int] = field(default_factory=list)
    meson_declared: bool = False
    optional_declaration: bool = False
    enabled_declaration: bool = False
    has_source_or_wrap: bool = False

    def present(self) -> bool:
        return self.status != "absent"


@dataclass
class MesonOptionFacts:
    status: str = "absent"
    options: list[MesonOptionFact] = field(default_factory=list)

    def present(self) -> bool:
        return self.status != "absent"


@dataclass
class CompileDefineFacts:
    status: str = "absent"
    macros: list[str] = field(default_factory=list)
    snippets: list[str] = field(default_factory=list)
    runtime_compiled: bool = False

    def present(self) -> bool:
        return self.status != "absent"


@dataclass
class ScaffoldSourceFacts:
    status: str = "absent"
    source_path: str = ""
    source_exists: bool = False
    listed_in_meson: bool = False
    runtime_guard_present: bool = False
    source_runtime_default: str = ""
    default_disabled_cvar: bool = False
    runtime_compiled: bool = False

    def present(self) -> bool:
        return self.status != "absent"


@dataclass
class DependencyIntegrationReport:
    repo_root: Path
    meson_build_path: Path
    meson_options_path: Path
    subprojects_dir: Path
    scaffold_source_path: Path
    state: str = "absent"
    meson_build_exists: bool = False
    meson_options_exists: bool = False
    subprojects_exists: bool = False
    dependency: DependencySourceFacts = field(default_factory=DependencySourceFacts)
    option: MesonOptionFacts = field(default_factory=MesonOptionFacts)
    define: CompileDefineFacts = field(default_factory=CompileDefineFacts)
    scaffold: ScaffoldSourceFacts = field(default_factory=ScaffoldSourceFacts)
    warnings: list[str] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)

    def ok(self) -> bool:
        return not self.errors


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def display_path(path: Path, repo_root: Path) -> str:
    try:
        return path.resolve(strict=False).relative_to(repo_root.resolve()).as_posix()
    except ValueError:
        return str(path)


def resolve_path(path: Path, repo_root: Path) -> Path:
    if path.is_absolute():
        return path
    return repo_root / path


def normalize_repo_path(path: str) -> str:
    return PATH_SEP_RE.sub("/", path).strip("./")


def read_text_if_file(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8")


def find_call_end(text: str, open_paren_index: int) -> int:
    depth = 0
    quote = ""
    escaped = False
    for index in range(open_paren_index, len(text)):
        char = text[index]
        if quote:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = ""
            continue
        if char in ("'", '"'):
            quote = char
        elif char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return index
    return -1


def iter_meson_calls(text: str, functions: Iterable[str]) -> Iterable[MesonCall]:
    function_pattern = "|".join(re.escape(function) for function in functions)
    pattern = re.compile(rf"\b(?P<function>{function_pattern})\s*\(", re.IGNORECASE)
    for match in pattern.finditer(text):
        open_paren = match.end() - 1
        end = find_call_end(text, open_paren)
        if end < 0:
            continue
        call_text = text[match.start() : end + 1]
        first_arg = extract_first_string_arg(call_text)
        line = text.count("\n", 0, match.start()) + 1
        yield MesonCall(match.group("function").lower(), call_text, first_arg, line, match.start())


def extract_first_string_arg(call_text: str) -> str:
    match = re.search(r"\(\s*(['\"])(?P<value>.*?)\1", call_text, re.DOTALL)
    return match.group("value") if match else ""


def extract_string_keyword(call_text: str, keyword: str) -> str:
    match = re.search(
        rf"\b{re.escape(keyword)}\s*:\s*(['\"])(?P<value>.*?)\1",
        call_text,
        re.IGNORECASE | re.DOTALL,
    )
    return match.group("value").strip() if match else ""


def extract_bool_keyword(call_text: str, keyword: str) -> str:
    match = re.search(
        rf"\b{re.escape(keyword)}\s*:\s*(?P<value>true|false)\b",
        call_text,
        re.IGNORECASE,
    )
    return match.group("value").lower() if match else ""


def split_top_level_args(text: str) -> list[str]:
    args: list[str] = []
    current: list[str] = []
    depth = 0
    quote = ""
    escaped = False
    for char in text:
        if quote:
            current.append(char)
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = ""
            continue
        if char in ("'", '"'):
            quote = char
            current.append(char)
        elif char in "([{":
            depth += 1
            current.append(char)
        elif char in ")]}":
            depth -= 1
            current.append(char)
        elif char == "," and depth == 0:
            args.append("".join(current).strip())
            current = []
        else:
            current.append(char)
    if current:
        args.append("".join(current).strip())
    return args


def option_status(option_type: str, default: str) -> str:
    option_type = option_type.lower()
    default = default.lower()
    if option_type == "feature":
        if default == "enabled":
            return "enabled"
        if default in {"auto", "disabled"}:
            return "optional"
        return "declared"
    if option_type == "boolean":
        if default == "true":
            return "enabled"
        if default == "false":
            return "optional"
        return "declared"
    if default in {"enabled", "true"}:
        return "enabled"
    if default in {"auto", "disabled", "false"}:
        return "optional"
    return "declared"


def aggregate_status(statuses: Iterable[str]) -> str:
    values = list(statuses)
    if not values:
        return "absent"
    for status in ("enabled", "optional", "declared", "disabled"):
        if status in values:
            return status
    return "declared"


def discover_options(meson_options_text: str) -> MesonOptionFacts:
    options: list[MesonOptionFact] = []
    for call in iter_meson_calls(meson_options_text, ("option",)):
        if not RML_NAME_RE.search(call.first_arg):
            continue
        option_type = extract_string_keyword(call.text, "type")
        default = extract_string_keyword(call.text, "value") or extract_bool_keyword(
            call.text, "value"
        )
        status = option_status(option_type, default)
        options.append(
            MesonOptionFact(
                name=call.first_arg,
                option_type=option_type or "-",
                default=default or "-",
                status=status,
                line=call.line,
            )
        )
    return MesonOptionFacts(
        status=aggregate_status(option.status for option in options),
        options=options,
    )


def discover_wrap_files(subprojects_dir: Path, repo_root: Path) -> list[str]:
    if not subprojects_dir.is_dir():
        return []
    wrap_files: list[str] = []
    for path in sorted(subprojects_dir.glob("*.wrap")):
        name_matches = RML_NAME_RE.search(path.name) is not None
        text_matches = False
        try:
            text_matches = RML_NAME_RE.search(path.read_text(encoding="utf-8")) is not None
        except OSError:
            text_matches = False
        if name_matches or text_matches:
            wrap_files.append(display_path(path, repo_root))
    return wrap_files


def discover_source_dirs(subprojects_dir: Path, repo_root: Path) -> list[str]:
    if not subprojects_dir.is_dir():
        return []
    source_dirs: list[str] = []
    for path in sorted(subprojects_dir.iterdir()):
        if path.is_dir() and RML_NAME_RE.search(path.name):
            source_dirs.append(display_path(path, repo_root))
    return source_dirs


def call_required_status(call_text: str, option_names: set[str]) -> str:
    lowered = call_text.lower()
    if re.search(r"\brequired\s*:\s*true\b", lowered):
        return "enabled"
    if re.search(r"\brequired\s*:\s*false\b", lowered):
        return "optional"
    if re.search(r"\brequired\s*:\s*get_option\s*\(", lowered):
        return "optional"
    for name in option_names:
        if re.search(r"\brequired\s*:\s*get_option\s*\(\s*['\"]" + re.escape(name.lower()), lowered):
            return "optional"
    if re.search(r"\brequired\s*:\s*(?:\w*option\w*|\w+_opt)\b", lowered):
        return "optional"
    if "dependency(" in lowered:
        return "enabled"
    return "declared"


def discover_dependency(
    meson_build_text: str,
    subprojects_dir: Path,
    repo_root: Path,
    option_facts: MesonOptionFacts,
) -> DependencySourceFacts:
    option_names = {option.name.lower() for option in option_facts.options}
    declarations: list[str] = []
    declaration_lines: list[int] = []
    declaration_statuses: list[str] = []
    for call in iter_meson_calls(meson_build_text, ("dependency", "subproject")):
        if not RML_NAME_RE.search(call.first_arg):
            continue
        declarations.append(f"{call.function}('{call.first_arg}')")
        declaration_lines.append(call.line)
        status = call_required_status(call.text, option_names)
        if status == "enabled":
            status = rml_optional_context_status(meson_build_text, call.start) or status
        declaration_statuses.append(status)

    wrap_files = discover_wrap_files(subprojects_dir, repo_root)
    source_dirs = discover_source_dirs(subprojects_dir, repo_root)
    optional_declaration = "optional" in declaration_statuses
    enabled_declaration = "enabled" in declaration_statuses
    status = "absent"
    if enabled_declaration:
        status = "enabled"
    elif optional_declaration:
        status = "optional"
    elif declarations or wrap_files or source_dirs:
        status = "declared"

    return DependencySourceFacts(
        status=status,
        wrap_files=wrap_files,
        source_dirs=source_dirs,
        meson_declarations=declarations,
        declaration_lines=declaration_lines,
        meson_declared=bool(declarations),
        optional_declaration=optional_declaration,
        enabled_declaration=enabled_declaration,
        has_source_or_wrap=bool(wrap_files or source_dirs),
    )


def classify_define_expression(expression: str) -> str:
    lowered = expression.lower().strip()
    if re.fullmatch(r"(true|1)", lowered):
        return "enabled"
    if re.fullmatch(r"(false|0)", lowered):
        return "disabled"
    if any(token in lowered for token in ("get_option", ".found()", ".allowed()")):
        return "optional"
    if "rml" in lowered:
        return "declared"
    return "declared"


def rml_optional_context_status(text: str, index: int) -> str:
    context = "\n".join(text[:index].splitlines()[-12:]).lower()
    if re.search(r"\bif\s+rmlui_runtime\b", context):
        return "optional"
    if re.search(r"\bif\b[^\n]*\brml\w*[^\n]*(?:\.found\(\)|\.allowed\(\)|get_option\()", context):
        return "optional"
    if re.search(r"\bif\b[^\n]*\brmlui_dep\s*\.\s*found\(\)", context):
        return "optional"
    if re.search(r"\bif\b[^\n]*\brmlui_opt\s*\.\s*allowed\(\)", context):
        return "optional"
    return ""


def apply_define_context(status: str, text: str, index: int) -> str:
    if status != "enabled":
        return status
    return rml_optional_context_status(text, index) or status


def discover_defines(meson_build_text: str) -> CompileDefineFacts:
    macro_statuses: list[str] = []
    macros: list[str] = []
    snippets: list[str] = []

    for call in iter_meson_calls(meson_build_text, ("set", "set10")):
        inner = call.text[call.text.find("(") + 1 : -1]
        args = split_top_level_args(inner)
        if len(args) < 2:
            continue
        macro = args[0].strip().strip("'\"")
        if macro not in RML_DEFINE_NAMES:
            continue
        status = apply_define_context(
            classify_define_expression(args[1]),
            meson_build_text,
            call.start,
        )
        macro_statuses.append(status)
        macros.append(macro)
        snippets.append(" ".join(call.text.split()))

    define_arg_pattern = re.compile(
        r"-D(?P<macro>" + "|".join(re.escape(name) for name in RML_DEFINE_NAMES) + r")"
        r"(?:=(?P<value>[A-Za-z0-9_]+))?",
        re.IGNORECASE,
    )
    for match in define_arg_pattern.finditer(meson_build_text):
        macro = match.group("macro")
        value = match.group("value")
        if value is None:
            status = "enabled"
        else:
            status = classify_define_expression(value)
        status = apply_define_context(status, meson_build_text, match.start())
        macro_statuses.append(status)
        macros.append(macro)
        line_start = meson_build_text.rfind("\n", 0, match.start()) + 1
        line_end = meson_build_text.find("\n", match.end())
        if line_end < 0:
            line_end = len(meson_build_text)
        snippets.append(meson_build_text[line_start:line_end].strip())

    status = aggregate_status(macro_statuses)
    return CompileDefineFacts(
        status=status,
        macros=sorted(set(macros)),
        snippets=snippets,
        runtime_compiled=status == "enabled",
    )


def scaffold_source_listed(
    meson_build_text: str,
    scaffold_source_path: Path,
    repo_root: Path,
) -> bool:
    candidates = [normalize_repo_path(scaffold_source_path.as_posix())]
    try:
        candidates.append(
            normalize_repo_path(
                scaffold_source_path.resolve(strict=False)
                .relative_to(repo_root.resolve())
                .as_posix()
            )
        )
    except ValueError:
        pass
    normalized_meson = normalize_repo_path(meson_build_text)
    return any(candidate in normalized_meson for candidate in candidates)


def discover_scaffold(
    scaffold_source_path: Path,
    meson_build_text: str,
    repo_root: Path,
    define_facts: CompileDefineFacts,
) -> ScaffoldSourceFacts:
    source_exists = scaffold_source_path.is_file()
    source_text = read_text_if_file(scaffold_source_path)
    source_default_match = re.search(
        rf"^\s*#\s*define\s+{re.escape(RML_RUNTIME_DEFINE)}\s+(?P<value>[01])\b",
        source_text,
        re.MULTILINE,
    )
    source_runtime_default = source_default_match.group("value") if source_default_match else ""
    source_runtime_compiled = source_runtime_default == "1"
    runtime_compiled = define_facts.runtime_compiled or source_runtime_compiled
    listed_in_meson = scaffold_source_listed(
        meson_build_text,
        scaffold_source_path,
        repo_root,
    )
    runtime_guard_present = RML_RUNTIME_DEFINE in source_text
    default_disabled_cvar = (
        'Cvar_Get("ui_rml_enable", "0"' in source_text
        or "Cvar_Get('ui_rml_enable', '0'" in source_text
    )

    if not source_exists and not listed_in_meson:
        status = "absent"
    elif runtime_compiled and source_exists and listed_in_meson:
        status = "runtime-compiled"
    elif source_exists and listed_in_meson:
        status = "compiled-stub"
    else:
        status = "declared"

    return ScaffoldSourceFacts(
        status=status,
        source_path=display_path(scaffold_source_path, repo_root),
        source_exists=source_exists,
        listed_in_meson=listed_in_meson,
        runtime_guard_present=runtime_guard_present,
        source_runtime_default=source_runtime_default,
        default_disabled_cvar=default_disabled_cvar,
        runtime_compiled=runtime_compiled,
    )


def component_count(report: DependencyIntegrationReport) -> int:
    return sum(
        (
            report.dependency.present(),
            report.option.present(),
            report.define.present(),
            report.scaffold.present(),
        )
    )


def classify_state(report: DependencyIntegrationReport) -> str:
    if report.scaffold.runtime_compiled or report.define.runtime_compiled:
        return "runtime-compiled"
    if report.option.status == "enabled" or report.dependency.status == "enabled":
        return "enabled"
    if report.option.status == "optional" or report.dependency.status == "optional":
        return "optional"
    if report.dependency.present() or report.option.present() or report.define.present():
        return "declared"
    if report.scaffold.present():
        return "scaffold-only"
    return "absent"


def add_consistency_findings(report: DependencyIntegrationReport) -> None:
    if not report.meson_build_exists:
        report.errors.append(
            f"meson: missing {display_path(report.meson_build_path, report.repo_root)}"
        )

    if report.scaffold.listed_in_meson and not report.scaffold.source_exists:
        report.errors.append("scaffold: source is listed in meson.build but the file is missing")

    if report.dependency.meson_declared and not report.option.present():
        report.errors.append(
            "option: RmlUi dependency/subproject is declared without a RmlUi Meson option"
        )

    if report.option.present() and not report.dependency.meson_declared:
        report.errors.append(
            "dependency: RmlUi Meson option is declared but no RmlUi dependency/subproject call uses it"
        )

    if report.option.status == "optional" and report.dependency.meson_declared:
        if not report.dependency.optional_declaration:
            report.errors.append(
                "dependency: optional RmlUi Meson option is not reflected in dependency required handling"
            )

    if report.option.status == "optional" and report.define.status == "enabled":
        report.errors.append(
            "define: RmlUi runtime compile define is enabled while the Meson option is optional/disabled by default"
        )

    if report.option.status == "enabled" and report.dependency.meson_declared:
        if report.define.status not in {"enabled", "optional"}:
            report.errors.append(
                "define: enabled RmlUi option does not feed a runtime compile define"
            )

    if report.dependency.status == "enabled" and report.define.status not in {
        "enabled",
        "optional",
    }:
        report.errors.append(
            "define: enabled RmlUi dependency declaration does not feed a runtime compile define"
        )

    if report.define.present() and not report.scaffold.listed_in_meson:
        report.errors.append(
            "scaffold: RmlUi compile define exists but scaffold source is not compiled"
        )

    if report.define.runtime_compiled and not report.dependency.meson_declared:
        report.errors.append(
            "dependency: RmlUi runtime is compiled but no RmlUi dependency/subproject is declared"
        )

    if report.scaffold.runtime_compiled and not report.dependency.meson_declared:
        report.errors.append(
            "dependency: RmlUi scaffold compiles runtime code without a RmlUi dependency/subproject"
        )

    if report.dependency.meson_declared and not report.scaffold.source_exists:
        report.errors.append(
            "scaffold: RmlUi dependency is declared but scaffold source is absent"
        )

    if report.dependency.has_source_or_wrap and not report.dependency.meson_declared:
        report.warnings.append(
            "dependency: RmlUi source/wrap exists but meson.build has no RmlUi dependency/subproject declaration"
        )

    if report.state == "scaffold-only":
        report.warnings.append(
            "state: scaffold source is compiled, but first-class RmlUi dependency integration is absent"
        )


def validate_dependency_integration(
    repo_root: Path,
    meson_build_path: Path,
    meson_options_path: Path,
    subprojects_dir: Path,
    scaffold_source_path: Path,
) -> DependencyIntegrationReport:
    repo_root = repo_root.resolve()
    meson_build_path = meson_build_path.resolve(strict=False)
    meson_options_path = meson_options_path.resolve(strict=False)
    subprojects_dir = subprojects_dir.resolve(strict=False)
    scaffold_source_path = scaffold_source_path.resolve(strict=False)

    report = DependencyIntegrationReport(
        repo_root=repo_root,
        meson_build_path=meson_build_path,
        meson_options_path=meson_options_path,
        subprojects_dir=subprojects_dir,
        scaffold_source_path=scaffold_source_path,
        meson_build_exists=meson_build_path.is_file(),
        meson_options_exists=meson_options_path.is_file(),
        subprojects_exists=subprojects_dir.is_dir(),
    )

    meson_build_text = read_text_if_file(meson_build_path)
    meson_options_text = read_text_if_file(meson_options_path)

    report.option = discover_options(meson_options_text)
    report.dependency = discover_dependency(
        meson_build_text,
        subprojects_dir,
        repo_root,
        report.option,
    )
    report.define = discover_defines(meson_build_text)
    report.scaffold = discover_scaffold(
        scaffold_source_path,
        meson_build_text,
        repo_root,
        report.define,
    )
    report.state = classify_state(report)
    add_consistency_findings(report)
    return report


def json_report_payload(report: DependencyIntegrationReport) -> dict[str, Any]:
    return {
        "ok": report.ok(),
        "state": report.state,
        "paths": {
            "repo_root": str(report.repo_root),
            "meson_build": display_path(report.meson_build_path, report.repo_root),
            "meson_options": display_path(report.meson_options_path, report.repo_root),
            "subprojects": display_path(report.subprojects_dir, report.repo_root),
            "scaffold_source": display_path(report.scaffold_source_path, report.repo_root),
        },
        "files": {
            "meson_build_exists": report.meson_build_exists,
            "meson_options_exists": report.meson_options_exists,
            "subprojects_exists": report.subprojects_exists,
        },
        "dependency": {
            "status": report.dependency.status,
            "wrap_files": report.dependency.wrap_files,
            "source_dirs": report.dependency.source_dirs,
            "meson_declarations": report.dependency.meson_declarations,
            "declaration_lines": report.dependency.declaration_lines,
            "meson_declared": report.dependency.meson_declared,
            "optional_declaration": report.dependency.optional_declaration,
            "enabled_declaration": report.dependency.enabled_declaration,
            "has_source_or_wrap": report.dependency.has_source_or_wrap,
        },
        "option": {
            "status": report.option.status,
            "options": [
                {
                    "name": option.name,
                    "type": option.option_type,
                    "default": option.default,
                    "status": option.status,
                    "line": option.line,
                }
                for option in report.option.options
            ],
        },
        "define": {
            "status": report.define.status,
            "macros": report.define.macros,
            "snippets": report.define.snippets,
            "runtime_compiled": report.define.runtime_compiled,
        },
        "scaffold": {
            "status": report.scaffold.status,
            "source": report.scaffold.source_path,
            "source_exists": report.scaffold.source_exists,
            "listed_in_meson": report.scaffold.listed_in_meson,
            "runtime_guard_present": report.scaffold.runtime_guard_present,
            "source_runtime_default": report.scaffold.source_runtime_default,
            "default_disabled_cvar": report.scaffold.default_disabled_cvar,
            "runtime_compiled": report.scaffold.runtime_compiled,
        },
        "counts": {
            "components_present": component_count(report),
            "components_total": 4,
            "wrap_files": len(report.dependency.wrap_files),
            "source_dirs": len(report.dependency.source_dirs),
            "meson_declarations": len(report.dependency.meson_declarations),
            "meson_options": len(report.option.options),
            "compile_defines": len(report.define.macros),
            "warnings": len(report.warnings),
            "errors": len(report.errors),
        },
        "warnings": report.warnings,
        "errors": report.errors,
    }


def print_json_report(report: DependencyIntegrationReport) -> None:
    print(json.dumps(json_report_payload(report), indent=2, sort_keys=True))


def yes_no(value: bool) -> str:
    return "yes" if value else "no"


def format_list(values: list[str]) -> str:
    return ", ".join(values) if values else "-"


def print_report(report: DependencyIntegrationReport) -> None:
    print("RmlUi dependency integration:")
    print(f"  State: {report.state}")
    print(f"  Components present: {component_count(report)}/4")
    print(f"  Malformed findings: {len(report.errors)}")
    print(f"  Warnings: {len(report.warnings)}")
    print(f"  Meson build: {display_path(report.meson_build_path, report.repo_root)}")
    print(f"  Meson options: {display_path(report.meson_options_path, report.repo_root)}")
    print(f"  Subprojects: {display_path(report.subprojects_dir, report.repo_root)}")

    print("\nDependency source/wrap:")
    print(f"  Status: {report.dependency.status}")
    print(f"  Wrap files: {format_list(report.dependency.wrap_files)}")
    print(f"  Source dirs: {format_list(report.dependency.source_dirs)}")
    print(f"  Meson declarations: {format_list(report.dependency.meson_declarations)}")
    print(f"  Optional declaration: {yes_no(report.dependency.optional_declaration)}")
    print(f"  Enabled declaration: {yes_no(report.dependency.enabled_declaration)}")

    print("\nMeson option:")
    print(f"  Status: {report.option.status}")
    if report.option.options:
        for option in report.option.options:
            print(
                "  - "
                f"{option.name}: type={option.option_type}, "
                f"default={option.default}, status={option.status}, line={option.line}"
            )
    else:
        print("  Options: -")

    print("\nMeson compile define:")
    print(f"  Status: {report.define.status}")
    print(f"  Macros: {format_list(report.define.macros)}")
    print(f"  Runtime compiled: {yes_no(report.define.runtime_compiled)}")
    if report.define.snippets:
        for snippet in report.define.snippets:
            print(f"  - {snippet}")

    print("\nScaffold source:")
    print(f"  Status: {report.scaffold.status}")
    print(f"  Source: {report.scaffold.source_path}")
    print(f"  Source exists: {yes_no(report.scaffold.source_exists)}")
    print(f"  Listed in meson.build: {yes_no(report.scaffold.listed_in_meson)}")
    print(f"  Runtime guard present: {yes_no(report.scaffold.runtime_guard_present)}")
    print(f"  Source runtime default: {report.scaffold.source_runtime_default or '-'}")
    print(f"  Runtime compiled: {yes_no(report.scaffold.runtime_compiled)}")

    if report.warnings:
        print("\nWarnings:")
        for warning in report.warnings:
            print(f"  - {warning}")

    if report.errors:
        print("\nErrors:")
        for error in report.errors:
            print(f"  - {error}")
        print("\nResult: RmlUi dependency integration check failed.")
    else:
        print("\nResult: RmlUi dependency integration check passed.")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=repo_root_from_script(),
        help="Repository root used to resolve default scan paths.",
    )
    parser.add_argument(
        "--meson-build",
        type=Path,
        default=DEFAULT_MESON_BUILD,
        help="Path to the root meson.build file.",
    )
    parser.add_argument(
        "--meson-options",
        type=Path,
        default=DEFAULT_MESON_OPTIONS,
        help="Path to the root meson_options.txt file.",
    )
    parser.add_argument(
        "--subprojects-dir",
        type=Path,
        default=DEFAULT_SUBPROJECTS_DIR,
        help="Directory containing Meson wraps and subproject sources.",
    )
    parser.add_argument(
        "--scaffold-source",
        type=Path,
        default=DEFAULT_SCAFFOLD_SOURCE,
        help="Path to the WORR RmlUi scaffold source file.",
    )
    parser.add_argument(
        "--format",
        choices=("text", "json"),
        default="text",
        help="Output format. Defaults to the text report.",
    )
    args = parser.parse_args(argv)

    repo_root = args.repo_root.resolve()
    report = validate_dependency_integration(
        repo_root=repo_root,
        meson_build_path=resolve_path(args.meson_build, repo_root),
        meson_options_path=resolve_path(args.meson_options, repo_root),
        subprojects_dir=resolve_path(args.subprojects_dir, repo_root),
        scaffold_source_path=resolve_path(args.scaffold_source, repo_root),
    )

    if args.format == "json":
        print_json_report(report)
    else:
        print_report(report)
    return 0 if report.ok() else 1


if __name__ == "__main__":
    raise SystemExit(main())
