#!/usr/bin/env python3
"""Validate WORR bot profile files for CI."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys
from dataclasses import dataclass
from typing import Any


MAX_BOT_PROFILES = 128
PROFILE_EXTENSIONS = (".c", ".bot")
DEFAULT_PROFILE_ROOTS = (
    (pathlib.Path("assets") / "botfiles" / "bots", (".c",)),
    (pathlib.Path("assets") / "bots" / "profiles", (".bot",)),
    (pathlib.Path("assets") / "bots", (".bot",)),
)

REQUIRED_FIELDS = ("name", "skin")
PROFILE_ID_RE = re.compile(r"^[A-Za-z0-9_.-]+$")

FIELD_ALIASES = {
    "name": "name",
    "characteristic_name": "name",
    "skin": "skin",
    "worr_skin": "skin",
    "characteristic_skin": "skin",
    "team": "team",
    "worr_team": "team",
    "characteristic_team": "team",
    "skill": "skill",
    "reaction": "reaction",
    "reaction_time": "reaction",
    "reaction_ms": "reaction",
    "worr_reaction_ms": "reaction",
    "characteristic_reactiontime": "reaction",
    "aggression": "aggression",
    "aggression_bias": "aggression",
    "characteristic_aggression": "aggression",
    "aim_error": "aim_error",
    "aimerror": "aim_error",
    "accuracy_error": "aim_error",
    "worr_aim_error": "aim_error",
    "preferred_weapon": "preferred_weapon",
    "weapon": "preferred_weapon",
    "favorite_weapon": "preferred_weapon",
    "worr_preferred_weapon": "preferred_weapon",
    "chat_personality": "chat_personality",
    "chat": "chat_personality",
    "personality": "chat_personality",
    "worr_chat_personality": "chat_personality",
    "role": "role",
    "team_role": "role",
    "worr_role": "role",
    "movement_style": "movement_style",
    "movement": "movement_style",
    "move_style": "movement_style",
    "worr_movement_style": "movement_style",
}

REACTION_SECONDS_KEYS = {"characteristic_reactiontime"}
STRUCTURAL_KEYS = {"character"}

NUMERIC_RANGES = {
    "skill": (0.0, 5.0),
    "reaction": (0.0, 5000.0),
    "aggression": (0.0, 1.0),
    "aim_error": (0.0, 90.0),
}

SCRIPT_COMPANION_SUFFIX = "_s.c"
SCRIPT_BLOCK_START_RE = re.compile(r'^script\s+"([^"]+)"\s*(\{)?\s*$')
SCRIPT_BLOCK_INLINE_RE = re.compile(r'^script\s+"([^"]+)"\s*\{\s*\}\s*$')
SCRIPT_COMMAND_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*\((.*)\)\s*;\s*$")

SCRIPT_COMMAND_ARITY = {
    "point": (4, 4),
    "box": (7, 7),
    "movebox": (2, 2),
    "moveto": (1, 1),
    "aim": (1, 1),
    "say": (1, 2),
    "wave": (1, 1),
    "selectweapon": (1, 1),
    "fireweapon": (0, 0),
    "wait": (1, 1),
}
REQUIRED_SCRIPT_COMMANDS = ("point", "box", "movebox", "moveto", "wait")


@dataclass(frozen=True)
class Token:
    value: str
    line: int
    column: int


@dataclass(frozen=True)
class Entry:
    key: str
    value: str
    line: int
    column: int
    skill_block: str | None = None


@dataclass
class Profile:
    path: pathlib.Path
    profile_id: str
    entries: list[Entry]
    fields: dict[str, str]
    skill_blocks: list[str]


@dataclass(frozen=True)
class Issue:
    severity: str
    code: str
    message: str
    path: pathlib.Path | None = None
    line: int | None = None
    column: int | None = None
    profile_id: str | None = None
    key: str | None = None

    def to_json(self) -> dict[str, Any]:
        payload: dict[str, Any] = {
            "severity": self.severity,
            "code": self.code,
            "message": self.message,
        }
        if self.path is not None:
            payload["path"] = str(self.path)
        if self.line is not None:
            payload["line"] = self.line
        if self.column is not None:
            payload["column"] = self.column
        if self.profile_id is not None:
            payload["profile_id"] = self.profile_id
        if self.key is not None:
            payload["key"] = self.key
        return payload


@dataclass(frozen=True)
class ValidationOptions:
    allow_unknown: bool = False
    fail_on_empty: bool = False
    check_companions: bool = True


def advance_position(char: str, line: int, column: int) -> tuple[int, int]:
    if char == "\n":
        return line + 1, 1
    return line, column + 1


def strip_token_trailing_punctuation(token: str) -> str:
    while token.endswith((";", ",")):
        token = token[:-1]
    return token


def tokenize(text: str) -> tuple[list[Token], list[Issue]]:
    tokens: list[Token] = []
    issues: list[Issue] = []
    index = 0
    line = 1
    column = 1

    while index < len(text):
        char = text[index]

        if char.isspace():
            line, column = advance_position(char, line, column)
            index += 1
            continue

        if char == "/" and index + 1 < len(text) and text[index + 1] == "/":
            while index < len(text) and text[index] != "\n":
                line, column = advance_position(text[index], line, column)
                index += 1
            continue

        if char == "/" and index + 1 < len(text) and text[index + 1] == "*":
            start_line = line
            start_column = column
            for _ in range(2):
                line, column = advance_position(text[index], line, column)
                index += 1
            closed = False
            while index < len(text):
                if text[index] == "*" and index + 1 < len(text) and text[index + 1] == "/":
                    for _ in range(2):
                        line, column = advance_position(text[index], line, column)
                        index += 1
                    closed = True
                    break
                line, column = advance_position(text[index], line, column)
                index += 1
            if not closed:
                issues.append(Issue(
                    "error",
                    "unterminated_comment",
                    "unterminated block comment",
                    line=start_line,
                    column=start_column,
                ))
            continue

        if char in "{}=":
            tokens.append(Token(char, line, column))
            line, column = advance_position(char, line, column)
            index += 1
            continue

        if char in "\"'":
            quote = char
            start_line = line
            start_column = column
            index += 1
            line, column = advance_position(char, line, column)
            value: list[str] = []
            closed = False
            while index < len(text):
                char = text[index]
                if char == quote:
                    index += 1
                    line, column = advance_position(char, line, column)
                    closed = True
                    break
                if char == "\\" and index + 1 < len(text):
                    index += 1
                    line, column = advance_position(char, line, column)
                    char = text[index]
                value.append(char)
                line, column = advance_position(char, line, column)
                index += 1
            tokens.append(Token("".join(value), start_line, start_column))
            if not closed:
                issues.append(Issue(
                    "error",
                    "unterminated_quote",
                    "unterminated quoted token",
                    line=start_line,
                    column=start_column,
                ))
            continue

        start_line = line
        start_column = column
        value = []
        while index < len(text):
            char = text[index]
            if char.isspace() or char in "{}=":
                break
            if char == "/" and index + 1 < len(text) and text[index + 1] in "/*":
                break
            value.append(char)
            line, column = advance_position(char, line, column)
            index += 1
        if value:
            tokens.append(Token("".join(value), start_line, start_column))
        else:
            line, column = advance_position(char, line, column)
            index += 1

    return tokens, issues


def parse_entries(text: str) -> tuple[list[Entry], list[Issue]]:
    tokens, issues = tokenize(text)
    entries: list[Entry] = []
    index = 0
    current_skill: str | None = None
    pending_skill: str | None = None

    while index < len(tokens):
        key_token = tokens[index]
        index += 1
        key = strip_token_trailing_punctuation(key_token.value)

        if key == "{":
            if pending_skill:
                current_skill = pending_skill
                pending_skill = None
            continue

        if key == "}":
            current_skill = None
            pending_skill = None
            continue

        if key == "":
            continue

        if index >= len(tokens):
            issues.append(Issue(
                "error",
                "missing_value",
                f"profile key {key!r} has no value",
                line=key_token.line,
                column=key_token.column,
                key=key,
            ))
            break

        value_token = tokens[index]
        index += 1
        value = strip_token_trailing_punctuation(value_token.value)
        if value == "=":
            if index >= len(tokens):
                issues.append(Issue(
                    "error",
                    "missing_value",
                    f"profile key {key!r} has '=' but no value",
                    line=key_token.line,
                    column=key_token.column,
                    key=key,
                ))
                break
            value_token = tokens[index]
            index += 1
            value = strip_token_trailing_punctuation(value_token.value)

        if key.startswith("#"):
            continue

        skill_block = current_skill
        if key.lower() == "skill":
            pending_skill = value
            skill_block = value

        entries.append(Entry(
            key=key,
            value=value,
            line=key_token.line,
            column=key_token.column,
            skill_block=skill_block,
        ))

    return entries, issues


def profile_id_for_path(path: pathlib.Path) -> str:
    profile_id = path.stem
    if path.suffix.lower() == ".c" and profile_id.lower().endswith("_c"):
        profile_id = profile_id[:-2]
    return profile_id


def is_profile_character_file(path: pathlib.Path) -> bool:
    if path.suffix.lower() != ".c":
        return True
    stem = path.stem.lower()
    return not stem.endswith(("_i", "_t", "_w", "_s"))


def parse_number(value: str) -> float | None:
    try:
        return float(value)
    except ValueError:
        return None


def validate_numeric_field(
    path: pathlib.Path,
    profile_id: str,
    entry: Entry,
    canonical_key: str,
) -> list[Issue]:
    if canonical_key not in NUMERIC_RANGES or not entry.value:
        return []

    value = parse_number(entry.value)
    if value is None:
        return [Issue(
            "error",
            "invalid_numeric",
            f"{entry.key} must be numeric; got {entry.value!r}",
            path=path,
            line=entry.line,
            column=entry.column,
            profile_id=profile_id,
            key=entry.key,
        )]

    minimum, maximum = NUMERIC_RANGES[canonical_key]
    if value < minimum or value > maximum:
        return [Issue(
            "error",
            "numeric_out_of_range",
            f"{entry.key}={entry.value} is outside {minimum:g}..{maximum:g}",
            path=path,
            line=entry.line,
            column=entry.column,
            profile_id=profile_id,
            key=entry.key,
        )]

    return []


def normalize_profile_value(entry: Entry) -> str:
    key = entry.key.lower()
    value = entry.value

    if key in REACTION_SECONDS_KEYS:
        seconds = parse_number(value)
        if seconds is None:
            return value
        milliseconds = max(0, int(seconds * 1000.0 + 0.5))
        return str(milliseconds)

    return value


def validate_profile_file(
    path: pathlib.Path,
    options: ValidationOptions,
) -> tuple[Profile | None, list[Issue]]:
    issues: list[Issue] = []
    profile_id = profile_id_for_path(path)

    if not profile_id or not PROFILE_ID_RE.fullmatch(profile_id):
        issues.append(Issue(
            "error",
            "invalid_profile_id",
            "profile ID must come from a non-empty filename using letters, digits, '.', '_' or '-'",
            path=path,
            profile_id=profile_id or None,
        ))

    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        return None, [Issue(
            "error",
            "read_failed",
            f"unable to read profile file: {exc}",
            path=path,
            profile_id=profile_id or None,
        )]

    entries, parse_issues = parse_entries(text)
    issues.extend(Issue(
        issue.severity,
        issue.code,
        issue.message,
        path=path,
        line=issue.line,
        column=issue.column,
        profile_id=profile_id,
        key=issue.key,
    ) for issue in parse_issues)

    fields: dict[str, str] = {}
    first_field_entries: dict[tuple[str, str | None], Entry] = {}
    skill_blocks: list[str] = []
    for entry in entries:
        key_lower = entry.key.lower()
        if key_lower == "skill" and entry.value and entry.value not in skill_blocks:
            skill_blocks.append(entry.value)

        if key_lower in STRUCTURAL_KEYS:
            continue

        canonical_key = FIELD_ALIASES.get(key_lower)
        if canonical_key is None:
            if key_lower.startswith("characteristic_"):
                continue
            severity = "warning" if options.allow_unknown else "error"
            issues.append(Issue(
                severity,
                "unknown_key",
                f"unrecognized profile key {entry.key!r}",
                path=path,
                line=entry.line,
                column=entry.column,
                profile_id=profile_id,
                key=entry.key,
            ))
            continue

        value = normalize_profile_value(entry)
        if not entry.value:
            issues.append(Issue(
                "error",
                "empty_value",
                f"{entry.key} must not be empty",
                path=path,
                line=entry.line,
                column=entry.column,
                profile_id=profile_id,
                key=entry.key,
            ))
        if canonical_key in fields:
            scoped_entry = first_field_entries.get((canonical_key, entry.skill_block))
            if scoped_entry is not None:
                issues.append(Issue(
                    "warning",
                    "duplicate_key",
                    f"{entry.key} overwrites earlier {scoped_entry.key} value for {canonical_key}",
                    path=path,
                    line=entry.line,
                    column=entry.column,
                    profile_id=profile_id,
                    key=entry.key,
                ))

        fields[canonical_key] = value
        first_field_entries.setdefault((canonical_key, entry.skill_block), entry)
        issues.extend(validate_numeric_field(
            path,
            profile_id,
            Entry(entry.key, value, entry.line, entry.column),
            canonical_key,
        ))

    for required in REQUIRED_FIELDS:
        if not fields.get(required):
            issues.append(Issue(
                "error",
                "missing_required_field",
                f"profile is missing required identity field {required!r}",
                path=path,
                profile_id=profile_id,
                key=required,
            ))

    profile = Profile(
        path=path,
        profile_id=profile_id,
        entries=entries,
        fields=fields,
        skill_blocks=skill_blocks,
    )
    return profile, issues


def resolve_input_path(value: str, cwd: pathlib.Path) -> pathlib.Path:
    path = pathlib.Path(value)
    if not path.is_absolute():
        path = cwd / path
    return path


def list_profile_files_in_dir(path: pathlib.Path, extensions: tuple[str, ...] = PROFILE_EXTENSIONS) -> list[pathlib.Path]:
    try:
        children = list(path.iterdir())
    except OSError:
        return []
    allowed = {extension.lower() for extension in extensions}
    return sorted(
        child
        for child in children
        if child.is_file() and child.suffix.lower() in allowed and is_profile_character_file(child)
    )


def collect_profile_files(
    inputs: list[str],
    cwd: pathlib.Path,
) -> tuple[list[pathlib.Path], list[Issue]]:
    issues: list[Issue] = []
    files: list[pathlib.Path] = []

    if inputs:
        for value in inputs:
            path = resolve_input_path(value, cwd)
            if path.is_file():
                files.append(path)
            elif path.is_dir():
                files.extend(list_profile_files_in_dir(path))
            else:
                issues.append(Issue(
                    "error",
                    "input_missing",
                    f"input path does not exist: {path}",
                    path=path,
                ))
        return sorted(dict.fromkeys(files)), issues

    for root, extensions in DEFAULT_PROFILE_ROOTS:
        path = cwd / root
        if path.is_dir():
            files.extend(list_profile_files_in_dir(path, extensions))

    return sorted(dict.fromkeys(files)), issues


def add_duplicate_id_issues(profiles: list[Profile], issues: list[Issue]) -> None:
    seen: dict[str, Profile] = {}
    for profile in profiles:
        lookup = profile.profile_id.lower()
        previous = seen.get(lookup)
        if previous is not None:
            issues.append(Issue(
                "error",
                "duplicate_profile_id",
                f"profile ID {profile.profile_id!r} duplicates {previous.path}",
                path=profile.path,
                profile_id=profile.profile_id,
            ))
            continue
        seen[lookup] = profile


def is_assets_botfiles_bots_path(path: pathlib.Path) -> bool:
    parts = [part.lower() for part in path.parts]
    return len(parts) >= 4 and parts[-4:-1] == ["assets", "botfiles", "bots"]


def read_text_for_companion(path: pathlib.Path) -> str | None:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None


def add_missing_file_issue(
    issues: list[Issue],
    path: pathlib.Path,
    code: str,
    message: str,
    profile_id: str | None = None,
) -> None:
    issues.append(Issue(
        "error",
        code,
        message,
        path=path,
        profile_id=profile_id,
    ))


def add_companion_text_issue(
    issues: list[Issue],
    path: pathlib.Path,
    code: str,
    message: str,
    profile_id: str,
) -> None:
    issues.append(Issue(
        "error",
        code,
        message,
        path=path,
        profile_id=profile_id,
    ))


def strip_line_comment_aware(line: str) -> str:
    quote: str | None = None
    escaped = False

    for index, char in enumerate(line):
        if escaped:
            escaped = False
            continue
        if quote is not None:
            if char == "\\":
                escaped = True
            elif char == quote:
                quote = None
            continue
        if char in "\"'":
            quote = char
            continue
        if char == "/" and index + 1 < len(line) and line[index + 1] == "/":
            return line[:index]

    return line


def split_script_arguments(argument_text: str) -> list[str]:
    if not argument_text.strip():
        return []

    arguments: list[str] = []
    current: list[str] = []
    quote: str | None = None
    escaped = False
    paren_depth = 0

    for char in argument_text:
        if escaped:
            current.append(char)
            escaped = False
            continue
        if quote is not None:
            current.append(char)
            if char == "\\":
                escaped = True
            elif char == quote:
                quote = None
            continue
        if char in "\"'":
            current.append(char)
            quote = char
            continue
        if char == "(":
            paren_depth += 1
            current.append(char)
            continue
        if char == ")" and paren_depth > 0:
            paren_depth -= 1
            current.append(char)
            continue
        if char == "," and paren_depth == 0:
            arguments.append("".join(current).strip())
            current = []
            continue
        current.append(char)

    arguments.append("".join(current).strip())
    return arguments


def is_quoted_script_argument(value: str) -> bool:
    return (
        len(value) >= 2
        and value[0] == value[-1]
        and value[0] in "\"'"
    )


def is_numeric_script_argument(value: str) -> bool:
    return parse_number(value) is not None


def is_integer_script_argument(value: str) -> bool:
    parsed = parse_number(value)
    return parsed is not None and parsed.is_integer()


def add_script_issue(
    issues: list[Issue],
    path: pathlib.Path,
    profile_id: str,
    code: str,
    message: str,
    line: int,
    column: int = 1,
    key: str | None = None,
) -> None:
    issues.append(Issue(
        "error",
        code,
        message,
        path=path,
        line=line,
        column=column,
        profile_id=profile_id,
        key=key,
    ))


def validate_quoted_script_arguments(
    issues: list[Issue],
    path: pathlib.Path,
    profile_id: str,
    command: str,
    line: int,
    argument_indexes: list[int],
    arguments: list[str],
) -> None:
    for argument_index in argument_indexes:
        if argument_index >= len(arguments):
            continue
        if not is_quoted_script_argument(arguments[argument_index]):
            add_script_issue(
                issues,
                path,
                profile_id,
                "invalid_script_argument",
                f"{command} argument {argument_index + 1} must be quoted",
                line,
                key=command,
            )


def validate_numeric_script_arguments(
    issues: list[Issue],
    path: pathlib.Path,
    profile_id: str,
    command: str,
    line: int,
    argument_indexes: list[int],
    arguments: list[str],
) -> None:
    for argument_index in argument_indexes:
        if argument_index >= len(arguments):
            continue
        if not is_numeric_script_argument(arguments[argument_index]):
            add_script_issue(
                issues,
                path,
                profile_id,
                "invalid_script_argument",
                f"{command} argument {argument_index + 1} must be numeric",
                line,
                key=command,
            )


def validate_wait_argument(
    issues: list[Issue],
    path: pathlib.Path,
    profile_id: str,
    line: int,
    argument: str,
) -> None:
    if re.fullmatch(r"time\(\s*\d+(?:\.\d+)?\s*\)", argument):
        return
    if re.fullmatch(r'touch\(\s*\d+\s*,\s*"[^"]+"\s*\)', argument):
        return
    add_script_issue(
        issues,
        path,
        profile_id,
        "invalid_script_argument",
        "wait argument must be time(number) or touch(entity, \"box\")",
        line,
        key="wait",
    )


def validate_script_command(
    path: pathlib.Path,
    profile_id: str,
    line: int,
    command: str,
    argument_text: str,
) -> list[Issue]:
    issues: list[Issue] = []
    arity = SCRIPT_COMMAND_ARITY.get(command)
    if arity is None:
        add_script_issue(
            issues,
            path,
            profile_id,
            "unknown_script_command",
            f"unknown script command {command!r}",
            line,
            key=command,
        )
        return issues

    arguments = split_script_arguments(argument_text)
    minimum, maximum = arity
    if len(arguments) < minimum or len(arguments) > maximum:
        expected = f"{minimum}" if minimum == maximum else f"{minimum}..{maximum}"
        add_script_issue(
            issues,
            path,
            profile_id,
            "invalid_script_arity",
            f"{command} expects {expected} arguments, got {len(arguments)}",
            line,
            key=command,
        )
        return issues

    if command == "point":
        validate_quoted_script_arguments(issues, path, profile_id, command, line, [0], arguments)
        validate_numeric_script_arguments(issues, path, profile_id, command, line, [1, 2, 3], arguments)
    elif command == "box":
        validate_quoted_script_arguments(issues, path, profile_id, command, line, [0], arguments)
        validate_numeric_script_arguments(issues, path, profile_id, command, line, [1, 2, 3, 4, 5, 6], arguments)
    elif command == "movebox":
        validate_quoted_script_arguments(issues, path, profile_id, command, line, [0, 1], arguments)
    elif command in {"moveto", "aim", "wave"}:
        validate_quoted_script_arguments(issues, path, profile_id, command, line, [0], arguments)
    elif command == "say":
        validate_quoted_script_arguments(issues, path, profile_id, command, line, [0], arguments)
        if len(arguments) == 2 and arguments[1] != "NULL" and not is_quoted_script_argument(arguments[1]):
            add_script_issue(
                issues,
                path,
                profile_id,
                "invalid_script_argument",
                "say argument 2 must be NULL or a quoted wav filename",
                line,
                key=command,
            )
    elif command == "selectweapon" and not is_integer_script_argument(arguments[0]):
        add_script_issue(
            issues,
            path,
            profile_id,
            "invalid_script_argument",
            "selectweapon argument must be an integer weapon slot",
            line,
            key=command,
        )
    elif command == "wait":
        validate_wait_argument(issues, path, profile_id, line, arguments[0])

    return issues


def validate_bot_script_file(path: pathlib.Path, profile_id: str) -> list[Issue]:
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        return [Issue(
            "error",
            "script_read_failed",
            f"unable to read script companion: {exc}",
            path=path,
            profile_id=profile_id,
        )]

    issues: list[Issue] = []
    script_names: list[str] = []
    command_counts: dict[str, int] = {}
    pending_script_name: str | None = None
    current_script_name: str | None = None
    current_command_count = 0
    current_script_start_line = 0

    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        stripped = strip_line_comment_aware(raw_line).strip()
        if not stripped or stripped.startswith("#"):
            continue

        if current_script_name is None:
            inline_match = SCRIPT_BLOCK_INLINE_RE.fullmatch(stripped)
            if inline_match:
                script_names.append(inline_match.group(1))
                add_script_issue(
                    issues,
                    path,
                    profile_id,
                    "empty_script_block",
                    f"script {inline_match.group(1)!r} has no commands",
                    line_number,
                )
                continue

            if pending_script_name is not None:
                if stripped == "{":
                    current_script_name = pending_script_name
                    script_names.append(current_script_name)
                    current_command_count = 0
                    current_script_start_line = line_number
                    pending_script_name = None
                    continue
                add_script_issue(
                    issues,
                    path,
                    profile_id,
                    "invalid_script_statement",
                    f"script {pending_script_name!r} must open with '{{'",
                    line_number,
                )
                pending_script_name = None

            block_match = SCRIPT_BLOCK_START_RE.fullmatch(stripped)
            if block_match:
                pending_script_name = block_match.group(1)
                if block_match.group(2):
                    current_script_name = pending_script_name
                    script_names.append(current_script_name)
                    current_command_count = 0
                    current_script_start_line = line_number
                    pending_script_name = None
                continue

            add_script_issue(
                issues,
                path,
                profile_id,
                "invalid_script_statement",
                "expected script block declaration",
                line_number,
            )
            continue

        if stripped == "}":
            if current_command_count == 0:
                add_script_issue(
                    issues,
                    path,
                    profile_id,
                    "empty_script_block",
                    f"script {current_script_name!r} has no commands",
                    current_script_start_line,
                )
            current_script_name = None
            current_command_count = 0
            current_script_start_line = 0
            continue

        command_match = SCRIPT_COMMAND_RE.fullmatch(stripped)
        if command_match is None:
            add_script_issue(
                issues,
                path,
                profile_id,
                "invalid_script_statement",
                f"invalid script statement in script {current_script_name!r}",
                line_number,
            )
            continue

        command = command_match.group(1)
        current_command_count += 1
        command_counts[command] = command_counts.get(command, 0) + 1
        issues.extend(validate_script_command(
            path,
            profile_id,
            line_number,
            command,
            command_match.group(2),
        ))

    if pending_script_name is not None:
        add_script_issue(
            issues,
            path,
            profile_id,
            "unterminated_script_block",
            f"script {pending_script_name!r} is missing '{{'",
            len(text.splitlines()) or 1,
        )
    if current_script_name is not None:
        add_script_issue(
            issues,
            path,
            profile_id,
            "unterminated_script_block",
            f"script {current_script_name!r} is missing closing '}}'",
            current_script_start_line,
        )

    if "main" not in script_names:
        issues.append(Issue(
            "error",
            "missing_script_main",
            'script companion must define script "main"',
            path=path,
            profile_id=profile_id,
        ))
    else:
        for command in REQUIRED_SCRIPT_COMMANDS:
            if command_counts.get(command, 0) == 0:
                issues.append(Issue(
                    "error",
                    "missing_script_command",
                    f'script "main" should exercise {command}',
                    path=path,
                    profile_id=profile_id,
                    key=command,
                ))

    return issues


def add_botfile_companion_issues(profiles: list[Profile], issues: list[Issue]) -> None:
    shared_roots: set[pathlib.Path] = set()

    for profile in profiles:
        if profile.path.suffix.lower() != ".c" or not profile.path.stem.lower().endswith("_c"):
            continue
        if not is_assets_botfiles_bots_path(profile.path):
            continue

        bot_id = profile.profile_id
        bots_dir = profile.path.parent
        botfiles_dir = bots_dir.parent
        shared_roots.add(botfiles_dir)

        character_text = read_text_for_companion(profile.path) or ""
        companions = {
            "weapon": (bots_dir / f"{bot_id}_w.c", f'bots/{bot_id}_w.c', '#include "fw_weap.c"'),
            "item": (bots_dir / f"{bot_id}_i.c", f'bots/{bot_id}_i.c', '#include "fw_items.c"'),
            "chat": (bots_dir / f"{bot_id}_t.c", f'bots/{bot_id}_t.c', f'chat "{bot_id}"'),
        }
        script_path = botfiles_dir / "scripts" / f"{bot_id}{SCRIPT_COMPANION_SUFFIX}"

        for kind, (path, reference, marker) in companions.items():
            if not path.is_file():
                add_missing_file_issue(
                    issues,
                    path,
                    "missing_companion",
                    f"{bot_id} is missing its {kind} companion script",
                    bot_id,
                )
                continue

            if reference not in character_text:
                add_companion_text_issue(
                    issues,
                    profile.path,
                    "missing_companion_reference",
                    f"{bot_id}_c.c does not reference {reference}",
                    bot_id,
                )

            companion_text = read_text_for_companion(path) or ""
            if '#include "inv.h"' not in companion_text and kind in {"weapon", "item"}:
                add_companion_text_issue(
                    issues,
                    path,
                    "missing_inv_include",
                    f"{path.name} must include inv.h",
                    bot_id,
                )
            if marker not in companion_text:
                add_companion_text_issue(
                    issues,
                    path,
                    "missing_companion_marker",
                    f"{path.name} is missing expected marker {marker!r}",
                    bot_id,
                )
            if kind == "item" and "GWW_" not in companion_text:
                add_companion_text_issue(
                    issues,
                    path,
                    "missing_weapon_stay_weights",
                    f"{path.name} should define GWW_* held-weapon item weights",
                    bot_id,
                )
            if kind == "chat":
                type_count = len(re.findall(r'\btype\s+"', companion_text))
                if type_count < 8:
                    add_companion_text_issue(
                        issues,
                        path,
                        "too_few_chat_types",
                        f"{path.name} has {type_count} chat type blocks; expected at least 8",
                        bot_id,
                    )
                if '#include "teamplay.h"' in companion_text and not (botfiles_dir / "teamplay.h").is_file():
                    add_missing_file_issue(
                        issues,
                        botfiles_dir / "teamplay.h",
                        "missing_shared_botfile",
                        "chat scripts include teamplay.h but the shared file is missing",
                        bot_id,
                    )

        if not script_path.is_file():
            add_missing_file_issue(
                issues,
                script_path,
                "missing_script_companion",
                f"{bot_id} is missing its scripts/{bot_id}{SCRIPT_COMPANION_SUFFIX} companion",
                bot_id,
            )
        else:
            issues.extend(validate_bot_script_file(script_path, bot_id))

    for botfiles_dir in sorted(shared_roots):
        required_shared = {
            "chars.h": "character symbol table",
            "inv.h": "inventory symbol table",
            "fw_weap.c": "shared weapon-weight framework",
            "fw_items.c": "shared item-weight framework",
        }
        for filename, description in required_shared.items():
            path = botfiles_dir / filename
            if not path.is_file():
                add_missing_file_issue(
                    issues,
                    path,
                    "missing_shared_botfile",
                    f"missing {description}",
                )
                continue
            if filename.startswith("fw_"):
                text = read_text_for_companion(path) or ""
                if 'weight "' not in text:
                    add_companion_text_issue(
                        issues,
                        path,
                        "missing_weight_blocks",
                        f"{filename} should contain BotLib weight blocks",
                        "<shared>",
                    )


def validate_paths(
    inputs: list[str],
    options: ValidationOptions | None = None,
    cwd: pathlib.Path | None = None,
) -> dict[str, Any]:
    options = options or ValidationOptions()
    cwd = cwd or pathlib.Path.cwd()
    files, issues = collect_profile_files(inputs, cwd)

    profiles: list[Profile] = []
    for path in files:
        profile, file_issues = validate_profile_file(path, options)
        issues.extend(file_issues)
        if profile is not None:
            profiles.append(profile)

    add_duplicate_id_issues(profiles, issues)
    if options.check_companions:
        add_botfile_companion_issues(profiles, issues)

    if len(profiles) > MAX_BOT_PROFILES:
        issues.append(Issue(
            "error",
            "profile_limit_exceeded",
            f"{len(profiles)} profiles exceed server limit of {MAX_BOT_PROFILES}",
        ))

    if not profiles:
        severity = "error" if options.fail_on_empty else "warning"
        issues.append(Issue(
            severity,
            "no_profiles",
            "no bot profile files were found",
        ))

    return build_report(profiles, files, issues)


def profile_to_json(profile: Profile) -> dict[str, Any]:
    return {
        "id": profile.profile_id,
        "path": str(profile.path),
        "fields": dict(sorted(profile.fields.items())),
        "entry_count": len(profile.entries),
        "skill_blocks": profile.skill_blocks,
    }


def build_report(
    profiles: list[Profile],
    files: list[pathlib.Path],
    issues: list[Issue],
) -> dict[str, Any]:
    errors = [issue for issue in issues if issue.severity == "error"]
    warnings = [issue for issue in issues if issue.severity == "warning"]
    return {
        "summary": {
            "status": "failed" if errors else "passed",
            "files": len(files),
            "profiles": len(profiles),
            "errors": len(errors),
            "warnings": len(warnings),
        },
        "profiles": [profile_to_json(profile) for profile in profiles],
        "issues": [issue.to_json() for issue in issues],
    }


def format_issue(issue: dict[str, Any]) -> str:
    location = issue.get("path", "<global>")
    if issue.get("line") is not None:
        location += f":{issue['line']}"
        if issue.get("column") is not None:
            location += f":{issue['column']}"
    code = issue.get("code", "issue")
    severity = issue.get("severity", "info")
    message = issue.get("message", "")
    return f"{severity}: {location}: {code}: {message}"


def print_text_report(report: dict[str, Any]) -> None:
    summary = report["summary"]
    print(f"bot profile validation: {summary['status']}")
    print(
        "  "
        f"files={summary['files']} profiles={summary['profiles']} "
        f"errors={summary['errors']} warnings={summary['warnings']}"
    )

    issues = report.get("issues", [])
    if issues:
        print("issues:")
        for issue in issues:
            print(f"  {format_issue(issue)}")
    else:
        print("issues: none")

    if report.get("profiles"):
        print("profiles:")
        for profile in report["profiles"]:
            fields = ", ".join(sorted(profile["fields"]))
            print(f"  {profile['id']}: {profile['path']} ({fields})")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate WORR bot profile key/value files."
    )
    parser.add_argument(
        "paths",
        nargs="*",
        help=(
            "Profile files or directories. Defaults to assets/botfiles/bots, "
            "assets/bots/profiles, and assets/bots when present."
        ),
    )
    parser.add_argument(
        "--format",
        choices=("text", "json"),
        default="text",
        help="Output format for CI logs. Defaults to text.",
    )
    parser.add_argument(
        "--allow-unknown",
        action="store_true",
        help="Downgrade unrecognized profile keys from errors to warnings.",
    )
    parser.add_argument(
        "--fail-on-empty",
        action="store_true",
        help="Fail when no profile files are found.",
    )
    parser.add_argument(
        "--skip-companion-checks",
        action="store_true",
        help="Skip Q3-style botfiles companion script checks.",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    report = validate_paths(
        args.paths,
        ValidationOptions(
            allow_unknown=args.allow_unknown,
            fail_on_empty=args.fail_on_empty,
            check_companions=not args.skip_companion_checks,
        ),
    )

    if args.format == "json":
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print_text_report(report)

    return 0 if report["summary"]["errors"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
