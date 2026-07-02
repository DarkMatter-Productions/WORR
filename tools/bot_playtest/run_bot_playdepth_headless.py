#!/usr/bin/env python3
"""Run Duel/CTF play-depth cases headlessly and prepare review notes."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import pathlib
import re
import subprocess
import time
from typing import Any, Iterable

import triage_bot_playtest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_PLAN = pathlib.Path(".tmp") / "bot_playtest" / "bot_multiplayer_playtest.json"
DEFAULT_OUTPUT_DIR = pathlib.Path(".tmp") / "bot_playtest" / "headless"
DEFAULT_REQUIRED_CASES = ("duel_rotation", "ctf_objectives")
SCHEMA = "worr.bot_playdepth_headless.v1"
FIRST_PARTY_PROFILES = ("vanguard", "vector", "bulwark", "relay", "smoke")
BOT_NAME_RE = re.compile(
    r"\bB\|[A-Za-z0-9_-]+(?: [A-Za-z0-9_-]+)*?"
    r"(?=\s*(?:$|profile=|profile\b|team=|score=|ping=|in\b|from\b|,|;|\.|\^))"
)
PROFILE_FIELD_RE = re.compile(r"\bprofile=([A-Za-z0-9_]+)")
BOTLIST_ROW_RE = re.compile(r"^\s*\d+\s+[A-Za-z_]+\s+B\|")
ADDED_BOT_RE = re.compile(r"^(Added bot|Removed bot)\s+B\|")
DUEL_PAIR_RE = re.compile(r"^B\|.+\^7 vs B\|")


def utc_stamp() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def resolve_repo_path(repo_root: pathlib.Path, path: pathlib.Path) -> pathlib.Path:
    if path.is_absolute():
        return path
    return repo_root / path


def load_json(path: pathlib.Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def index_cases(plan: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {
        str(case.get("id")): case
        for case in plan.get("cases", [])
        if isinstance(case, dict) and case.get("id")
    }


def cvar_pairs(case: dict[str, Any]) -> list[tuple[str, str]]:
    pairs: list[tuple[str, str]] = []
    for item in case.get("cvars", []):
        if not isinstance(item, dict):
            continue
        name = str(item.get("name", "")).strip()
        if not name:
            continue
        pairs.append((name, str(item.get("value", ""))))
    return pairs


def build_command(
    *,
    binary: pathlib.Path,
    install_dir: pathlib.Path,
    base_game: str,
    port: int,
    case: dict[str, Any],
    log_name: str,
    startup_wait: int,
    run_wait: int,
) -> list[str]:
    command = [
        str(binary),
        "+set",
        "game",
        base_game,
        "+set",
        "basedir",
        str(install_dir),
        "+set",
        "net_port",
        str(port),
        "+set",
        "logfile",
        "1",
        "+set",
        "logfile_flush",
        "1",
        "+set",
        "logfile_name",
        log_name,
        "+set",
        "developer",
        "1",
    ]
    for name, value in cvar_pairs(case):
        command.extend(("+set", name, value))
    command.extend(
        (
            "+map",
            str(case.get("map", "")),
            "+wait",
            str(startup_wait),
            "+botlist",
            "+wait",
            str(run_wait),
            "+botlist",
            "+quit",
        )
    )
    return command


def extract_botlist_lines(text: str) -> list[str]:
    lines: list[str] = []
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        if BOTLIST_ROW_RE.search(line) or ADDED_BOT_RE.search(line) or DUEL_PAIR_RE.search(line):
            lines.append(line)
    return lines


def profile_from_bot_name(name: str) -> str | None:
    lowered = re.sub(r"\^\d", "", name).lower()
    if lowered.startswith("b|"):
        lowered = lowered[2:]
    lowered = re.sub(r"\d+$", "", lowered)
    lowered = lowered.replace(" ", "_").strip("_")
    if lowered in FIRST_PARTY_PROFILES:
        return lowered
    return None


def extract_profiles(text: str) -> list[str]:
    found: list[str] = []
    candidates = [match.group(1).lower() for match in PROFILE_FIELD_RE.finditer(text)]
    candidates.extend(
        profile
        for name in extract_bot_names(text)
        if (profile := profile_from_bot_name(name)) is not None
    )
    for profile in FIRST_PARTY_PROFILES:
        if profile in candidates:
            found.append(profile)
    return found


def extract_bot_names(text: str) -> list[str]:
    names = {match.group(0).strip() for match in BOT_NAME_RE.finditer(text)}
    return sorted(names)


def case_expected_profiles(case: dict[str, Any]) -> list[str]:
    expected: list[str] = []
    seen: set[str] = set()
    for profile in case.get("profiles", []):
        clean = str(profile).strip().lower()
        if not clean or clean in seen:
            continue
        expected.append(clean)
        seen.add(clean)
    return expected


def case_profile_coverage(case: dict[str, Any], observed_profiles: list[str]) -> dict[str, Any]:
    expected = case_expected_profiles(case)
    observed = sorted({profile for profile in observed_profiles if profile})
    bot_target = int(case.get("bot_target", 0) or 0)
    required_all = bool(expected) and bot_target >= len(expected)
    min_distinct = min(bot_target, len(FIRST_PARTY_PROFILES))
    missing_expected = sorted(set(expected) - set(observed)) if required_all else []
    return {
        "expected_profiles": expected,
        "observed_profiles": observed,
        "required_all_expected": required_all,
        "missing_expected_profiles": missing_expected,
        "distinct_first_party_required": min_distinct,
        "distinct_first_party_observed": len(observed),
        "distinct_first_party_passed": len(observed) >= min_distinct if min_distinct else True,
        "passed": not missing_expected and (len(observed) >= min_distinct if min_distinct else True),
    }


def run_case(
    *,
    repo_root: pathlib.Path,
    binary: pathlib.Path,
    install_dir: pathlib.Path,
    base_game: str,
    port: int,
    case: dict[str, Any],
    output_dir: pathlib.Path,
    startup_wait: int,
    run_wait: int,
    timeout: int,
    dry_run: bool = False,
) -> dict[str, Any]:
    case_id = str(case.get("id", "unknown"))
    log_name = f"bot_playdepth_{case_id}_{utc_stamp()}"
    stdout_path = output_dir / f"{case_id}.stdout.txt"
    stderr_path = output_dir / f"{case_id}.stderr.txt"
    command = build_command(
        binary=binary,
        install_dir=install_dir,
        base_game=base_game,
        port=port,
        case=case,
        log_name=log_name,
        startup_wait=startup_wait,
        run_wait=run_wait,
    )
    started = time.monotonic()
    returncode: int | None = None
    timed_out = False
    if dry_run:
        stdout_path.write_text("", encoding="utf-8", newline="\n")
        stderr_path.write_text("", encoding="utf-8", newline="\n")
        returncode = 0
    else:
        creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
        with stdout_path.open("w", encoding="utf-8", errors="replace") as stdout_file, \
                stderr_path.open("w", encoding="utf-8", errors="replace") as stderr_file:
            process = subprocess.Popen(
                command,
                cwd=repo_root,
                stdout=stdout_file,
                stderr=stderr_file,
                text=True,
                creationflags=creationflags,
            )
            try:
                returncode = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                process.kill()
                returncode = process.wait(timeout=10)

    duration = round(time.monotonic() - started, 3)
    stdout_text = stdout_path.read_text(encoding="utf-8", errors="replace")
    stderr_text = stderr_path.read_text(encoding="utf-8", errors="replace")
    combined = stdout_text + "\n" + stderr_text
    botlist_lines = extract_botlist_lines(combined)
    bot_names = extract_bot_names(combined)
    profiles = extract_profiles(combined)
    profile_coverage = case_profile_coverage(case, profiles)
    failures: list[str] = []
    if timed_out:
        failures.append(f"headless run timed out after {timeout} seconds")
    if returncode != 0:
        failures.append(f"headless run returned {returncode}")
    if not dry_run and not botlist_lines:
        failures.append("headless run did not capture botlist/profile output")
    if not dry_run and profile_coverage["missing_expected_profiles"]:
        failures.append(
            "headless run missed expected profiles: "
            + ", ".join(profile_coverage["missing_expected_profiles"])
        )
    if not dry_run and not profile_coverage["distinct_first_party_passed"]:
        failures.append(
            "headless run observed too few first-party profiles: "
            f"{profile_coverage['distinct_first_party_observed']}/"
            f"{profile_coverage['distinct_first_party_required']}"
        )

    return {
        "id": case_id,
        "title": case.get("title", ""),
        "mode": case.get("mode", ""),
        "map": case.get("map", ""),
        "bot_target": int(case.get("bot_target", 0) or 0),
        "port": port,
        "log_name": log_name,
        "command": command,
        "stdout_path": str(stdout_path),
        "stderr_path": str(stderr_path),
        "returncode": returncode,
        "timed_out": timed_out,
        "duration_seconds": duration,
        "botlist_lines": botlist_lines,
        "bot_names": bot_names,
        "profiles_observed": profiles,
        "profile_coverage": profile_coverage,
        "failures": failures,
        "status": "failed" if failures else "passed",
    }


def selected_cases(plan: dict[str, Any], required_cases: Iterable[str]) -> tuple[list[dict[str, Any]], list[str]]:
    by_id = index_cases(plan)
    cases: list[dict[str, Any]] = []
    missing: list[str] = []
    for case_id in required_cases:
        case = by_id.get(case_id)
        if case is None:
            missing.append(case_id)
        else:
            cases.append(case)
    return cases, missing


def build_notes_from_results(plan: dict[str, Any], results: list[dict[str, Any]], artifact_json: pathlib.Path) -> dict[str, Any]:
    notes = triage_bot_playtest.default_notes_from_plan(plan)
    by_id = {result["id"]: result for result in results}
    for case in notes.get("cases", []):
        case_id = case.get("id")
        result = by_id.get(case_id)
        if result is None:
            continue
        case["outcome"] = "blocked" if result["failures"] else "pending"
        case["botlist"] = "\n".join(result["botlist_lines"] or result["bot_names"])
        case["profiles_observed"] = result["profiles_observed"]
        case["custom_failure_signals"] = list(result["failures"])
        case["repro_steps"] = [
            f"Headless command artifact: {artifact_json.as_posix()}",
            f"Stdout: {result['stdout_path']}",
            f"Stderr: {result['stderr_path']}",
        ]
        case["notes"] = (
            "Headless run captured machine evidence. Manual visual review is "
            "still required before marking this play-depth case as pass."
        )
    notes["playtest_artifact"] = plan.get("playtest_artifact", "bot_multiplayer_playtest.json")
    notes["notes"] = "Generated by run_bot_playdepth_headless.py; outcomes remain pending until reviewed."
    return notes


def build_report(
    *,
    plan: dict[str, Any],
    results: list[dict[str, Any]],
    missing_cases: list[str],
    output_dir: pathlib.Path,
) -> dict[str, Any]:
    failures = [f"missing required case in plan: {case_id}" for case_id in missing_cases]
    for result in results:
        failures.extend(f"{result['id']}: {failure}" for failure in result["failures"])
    coverage_rows = [result.get("profile_coverage", {}) for result in results]
    return {
        "schema": SCHEMA,
        "summary": {
            "status": "failed" if failures else "passed",
            "cases": len(results),
            "passed": sum(1 for result in results if result["status"] == "passed"),
            "failed": sum(1 for result in results if result["status"] == "failed"),
            "missing": len(missing_cases),
            "botlist_captures": sum(1 for result in results if result["botlist_lines"] or result["bot_names"]),
            "profile_coverage_passed": sum(1 for row in coverage_rows if row.get("passed")),
            "profile_coverage_cases": len(coverage_rows),
            "failures": len(failures),
        },
        "failures": failures,
        "cases": results,
        "output_dir": str(output_dir),
        "playtest_schema": plan.get("schema", ""),
    }


def render_markdown(report: dict[str, Any]) -> str:
    summary = report["summary"]
    lines = [
        "# WORR Bot Headless Play-Depth Runs",
        "",
        f"Status: `{summary['status']}`",
        (
            f"Cases: `{summary['cases']}` passed `{summary['passed']}` "
            f"failed `{summary['failed']}` missing `{summary['missing']}`"
        ),
        f"Botlist captures: `{summary['botlist_captures']}`",
        (
            f"Profile coverage: `{summary['profile_coverage_passed']}` / "
            f"`{summary['profile_coverage_cases']}`"
        ),
        "",
    ]
    if report["failures"]:
        lines.extend(["## Failures", ""])
        lines.extend(f"- {failure}" for failure in report["failures"])
        lines.append("")
    lines.extend(["## Cases", ""])
    for result in report["cases"]:
        lines.extend(
            [
                f"### {result['id']}",
                "",
                f"- Mode: `{result['mode']}`",
                f"- Map: `{result['map']}`",
                f"- Status: `{result['status']}`",
                f"- Duration: `{result['duration_seconds']}` seconds",
                f"- Stdout: `{result['stdout_path']}`",
                f"- Profiles observed: `{', '.join(result['profiles_observed']) or 'none'}`",
                "",
            ]
        )
        coverage = result.get("profile_coverage", {})
        if coverage:
            lines.extend(
                [
                    (
                        "Profile coverage: "
                        f"`{'passed' if coverage.get('passed') else 'failed'}`"
                    ),
                    f"- Expected: `{', '.join(coverage.get('expected_profiles', [])) or 'none'}`",
                    (
                        "- Missing expected: "
                        f"`{', '.join(coverage.get('missing_expected_profiles', [])) or 'none'}`"
                    ),
                    "",
                ]
            )
        if result["botlist_lines"]:
            lines.append("Botlist/profile lines:")
            lines.extend(f"- {line}" for line in result["botlist_lines"][:12])
            lines.append("")
    return "\n".join(lines)


def write_outputs(report: dict[str, Any], notes: dict[str, Any], output_dir: pathlib.Path) -> dict[str, str]:
    output_dir.mkdir(parents=True, exist_ok=True)
    json_path = output_dir / "bot_playdepth_headless_runs.json"
    markdown_path = output_dir / "bot_playdepth_headless_runs.md"
    notes_path = output_dir / "bot_multiplayer_playtest_headless_notes.json"
    artifacts = {"json": str(json_path), "markdown": str(markdown_path), "notes": str(notes_path)}
    report["artifacts"] = artifacts
    json_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")
    markdown_path.write_text(render_markdown(report), encoding="utf-8", newline="\n")
    notes_path.write_text(json.dumps(notes, indent=2) + "\n", encoding="utf-8", newline="\n")
    return artifacts


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=pathlib.Path, default=REPO_ROOT)
    parser.add_argument("--plan", type=pathlib.Path, default=DEFAULT_PLAN)
    parser.add_argument("--output-dir", type=pathlib.Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--binary", type=pathlib.Path, default=pathlib.Path(".install") / "worr_ded_x86_64.exe")
    parser.add_argument("--install-dir", type=pathlib.Path, default=pathlib.Path(".install"))
    parser.add_argument("--base-game", default="basew")
    parser.add_argument("--case", action="append", dest="cases", default=None)
    parser.add_argument("--base-port", type=int, default=28100)
    parser.add_argument("--startup-wait", type=int, default=240)
    parser.add_argument("--run-wait", type=int, default=600)
    parser.add_argument("--timeout", type=int, default=90)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--format", choices=("text", "json"), default="text")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo_root = args.repo_root.resolve()
    plan_path = resolve_repo_path(repo_root, args.plan)
    output_root = resolve_repo_path(repo_root, args.output_dir)
    run_dir = output_root / utc_stamp()
    binary = resolve_repo_path(repo_root, args.binary)
    install_dir = resolve_repo_path(repo_root, args.install_dir)
    if not args.dry_run and not binary.is_file():
        raise SystemExit(f"dedicated binary not found: {binary}")
    if not install_dir.is_dir():
        raise SystemExit(f"install dir not found: {install_dir}")
    if args.timeout <= 0:
        raise SystemExit("--timeout must be positive")

    plan = load_json(plan_path)
    required = tuple(args.cases or DEFAULT_REQUIRED_CASES)
    cases, missing = selected_cases(plan, required)
    run_dir.mkdir(parents=True, exist_ok=True)
    results = [
        run_case(
            repo_root=repo_root,
            binary=binary,
            install_dir=install_dir,
            base_game=args.base_game,
            port=args.base_port + index,
            case=case,
            output_dir=run_dir,
            startup_wait=args.startup_wait,
            run_wait=args.run_wait,
            timeout=args.timeout,
            dry_run=args.dry_run,
        )
        for index, case in enumerate(cases)
    ]
    report = build_report(plan=plan, results=results, missing_cases=missing, output_dir=run_dir)
    notes = build_notes_from_results(plan, results, run_dir / "bot_playdepth_headless_runs.json")
    artifacts = write_outputs(report, notes, run_dir)

    if args.format == "json":
        print(json.dumps(report, indent=2))
    else:
        summary = report["summary"]
        print(
            "bot headless play-depth: "
            f"{summary['status']} cases={summary['cases']} "
            f"passed={summary['passed']} failed={summary['failed']} "
            f"missing={summary['missing']} botlist={summary['botlist_captures']}"
        )
        print(f"json: {artifacts['json']}")
        print(f"markdown: {artifacts['markdown']}")
        print(f"notes: {artifacts['notes']}")
    return 0 if report["summary"]["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
