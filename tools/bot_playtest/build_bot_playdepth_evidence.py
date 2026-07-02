#!/usr/bin/env python3
"""Build a release attachment from Duel/CTF bot play-depth notes."""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
from typing import Any, Iterable


SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
DEFAULT_PLAYTEST_JSON = pathlib.Path(".tmp") / "bot_playtest" / "bot_multiplayer_playtest.json"
DEFAULT_NOTES_JSON = pathlib.Path(".tmp") / "bot_playtest" / "bot_multiplayer_playtest_notes_template.json"
DEFAULT_OUTPUT_DIR = pathlib.Path(".tmp") / "bot_playtest"
DEFAULT_REQUIRED_CASES = ("duel_rotation", "ctf_objectives")
SCHEMA = "worr.bot_playdepth_evidence.v1"

sys.path.insert(0, str(SCRIPT_DIR))
import triage_bot_playtest  # noqa: E402


def resolve_repo_path(repo_root: pathlib.Path, path: pathlib.Path) -> pathlib.Path:
    if path.is_absolute():
        return path
    return repo_root / path


def load_json(path: pathlib.Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def index_by_id(rows: Iterable[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    for row in rows:
        row_id = row.get("id")
        if row_id:
            result[str(row_id)] = row
    return result


def string_list(value: Any) -> list[str]:
    if isinstance(value, str):
        return [value] if value.strip() else []
    if isinstance(value, list):
        return [str(item) for item in value if str(item).strip()]
    return []


def normalized_string_set(value: Any) -> set[str]:
    return {item.strip().lower() for item in string_list(value) if item.strip()}


def profile_coverage(plan_case: dict[str, Any], observed_profiles: list[str]) -> dict[str, Any]:
    expected = sorted(normalized_string_set(plan_case.get("profiles", [])))
    observed = sorted({profile.strip().lower() for profile in observed_profiles if profile.strip()})
    bot_target = int(plan_case.get("bot_target", 0) or 0)
    required_all = bool(expected) and bot_target >= len(expected)
    missing = sorted(set(expected) - set(observed)) if required_all else []
    return {
        "expected_profiles": expected,
        "observed_profiles": observed,
        "required_all_expected": required_all,
        "missing_expected_profiles": missing,
        "passed": not missing,
    }


def normalized_required_cases(required_cases: Iterable[str] | None) -> tuple[str, ...]:
    if required_cases is None:
        return DEFAULT_REQUIRED_CASES
    result: list[str] = []
    seen: set[str] = set()
    for case_id in required_cases:
        clean = str(case_id).strip()
        if not clean or clean in seen:
            continue
        result.append(clean)
        seen.add(clean)
    return tuple(result or DEFAULT_REQUIRED_CASES)


def case_evidence(
    case_id: str,
    *,
    plan_case: dict[str, Any] | None,
    note_case: dict[str, Any] | None,
    triage_case: dict[str, Any] | None,
) -> dict[str, Any]:
    plan_case = plan_case or {}
    note_case = note_case or {}
    triage_case = triage_case or {}
    outcome = triage_bot_playtest.normalize_outcome(note_case.get("outcome"))
    signals = triage_bot_playtest.failure_signal_rows(note_case)
    profiles_observed = string_list(note_case.get("profiles_observed", []))
    coverage = profile_coverage(plan_case, profiles_observed)
    categories = sorted(
        {
            category
            for signal in signals
            for category in triage_bot_playtest.categories_for_signal(signal)
        }
    )
    return {
        "id": case_id,
        "title": plan_case.get("title", ""),
        "mode": plan_case.get("mode", ""),
        "map": plan_case.get("map", ""),
        "config": plan_case.get("config", ""),
        "outcome": outcome,
        "duration_minutes": note_case.get(
            "duration_minutes",
            plan_case.get("duration_minutes", 0),
        ),
        "botlist_present": bool(str(note_case.get("botlist", "")).strip()),
        "profiles_expected": coverage["expected_profiles"],
        "profiles_observed": profiles_observed,
        "missing_expected_profiles": coverage["missing_expected_profiles"],
        "profile_coverage_required": coverage["required_all_expected"],
        "profile_coverage_passed": coverage["passed"],
        "failure_signals": signals,
        "failure_categories": categories,
        "repro_steps": string_list(note_case.get("repro_steps", [])),
        "notes": note_case.get("notes", ""),
        "triage_signal_count": len(triage_case.get("signals", [])),
    }


def summarize_status(cases: list[dict[str, Any]], missing_cases: list[str]) -> str:
    if missing_cases:
        return "failed"
    if any(case["outcome"] == "pass" and not case["profile_coverage_passed"] for case in cases):
        return "failed"
    outcomes = {case["outcome"] for case in cases}
    if outcomes & {"fail", "blocked"}:
        return "failed"
    if outcomes & {"pending", "skip"}:
        return "pending"
    return "passed"


def build_evidence(
    plan: dict[str, Any],
    notes: dict[str, Any],
    *,
    required_cases: Iterable[str] | None = None,
    repeat_threshold: int = 2,
) -> dict[str, Any]:
    required = normalized_required_cases(required_cases)
    triage = triage_bot_playtest.triage(plan, notes, repeat_threshold=repeat_threshold)
    plan_cases = index_by_id(plan.get("cases", []))
    note_cases = index_by_id(notes.get("cases", []))
    triage_cases = index_by_id(triage.get("cases", []))
    missing_cases = [case_id for case_id in required if case_id not in plan_cases]
    cases = [
        case_evidence(
            case_id,
            plan_case=plan_cases.get(case_id),
            note_case=note_cases.get(case_id),
            triage_case=triage_cases.get(case_id),
        )
        for case_id in required
        if case_id in plan_cases
    ]
    status = summarize_status(cases, missing_cases)
    outcomes = {name: 0 for name in sorted(triage_bot_playtest.ALLOWED_OUTCOMES)}
    for case in cases:
        outcomes[case["outcome"]] = outcomes.get(case["outcome"], 0) + 1

    promoted = [
        candidate
        for candidate in triage.get("scenario_candidates", [])
        if candidate.get("promote_to_scenario")
    ]
    warnings = list(triage.get("warnings", []))
    for case_id in missing_cases:
        warnings.append(f"required play-depth case missing from plan: {case_id}")
    for case in cases:
        if case["outcome"] == "pass" and not case["profile_coverage_passed"]:
            warnings.append(
                f"{case['id']} marked pass but missed expected profiles: "
                + ", ".join(case["missing_expected_profiles"])
            )

    return {
        "schema": SCHEMA,
        "playtest_schema": plan.get("schema", ""),
        "notes_schema": notes.get("schema", ""),
        "required_cases": list(required),
        "summary": {
            "status": status,
            "required_cases": len(required),
            "reported_cases": len(cases),
            "passed": outcomes.get("pass", 0),
            "failed": outcomes.get("fail", 0),
            "blocked": outcomes.get("blocked", 0),
            "pending": outcomes.get("pending", 0),
            "skipped": outcomes.get("skip", 0),
            "botlists_present": sum(1 for case in cases if case["botlist_present"]),
            "profile_coverage_passed": sum(1 for case in cases if case["profile_coverage_passed"]),
            "profile_coverage_required": sum(1 for case in cases if case["profile_coverage_required"]),
            "failure_signals": sum(len(case["failure_signals"]) for case in cases),
            "promoted_candidates": len(promoted),
            "warnings": len(warnings),
        },
        "warnings": warnings,
        "cases": cases,
        "scenario_candidates": triage.get("scenario_candidates", []),
        "promoted_candidates": promoted,
        "triage_summary": triage.get("summary", {}),
    }


def render_markdown(evidence: dict[str, Any]) -> str:
    summary = evidence["summary"]
    lines = [
        "# WORR Bot Duel/CTF Play-Depth Evidence",
        "",
        f"Status: `{summary['status']}`",
        (
            f"Required cases: `{summary['required_cases']}`; reported "
            f"`{summary['reported_cases']}`; passed `{summary['passed']}`; "
            f"failed `{summary['failed']}`; blocked `{summary['blocked']}`; "
            f"pending `{summary['pending']}`; skipped `{summary['skipped']}`"
        ),
        (
            f"Botlists present: `{summary['botlists_present']}`; failure signals "
            f"`{summary['failure_signals']}`; promoted candidates "
            f"`{summary['promoted_candidates']}`"
        ),
        "",
    ]
    if evidence["warnings"]:
        lines.extend(["## Warnings", ""])
        lines.extend(f"- {warning}" for warning in evidence["warnings"])
        lines.append("")

    lines.extend(["## Required Cases", ""])
    for case in evidence["cases"]:
        lines.extend(
            [
                f"### {case['id']}",
                "",
                f"- Mode: `{case['mode']}`",
                f"- Map: `{case['map']}`",
                f"- Config: `{case['config']}`",
                f"- Outcome: `{case['outcome']}`",
                f"- Duration: `{case['duration_minutes']}` minutes",
                f"- Botlist captured: `{str(case['botlist_present']).lower()}`",
                f"- Expected profiles: `{', '.join(case['profiles_expected']) or 'none'}`",
                f"- Profiles observed: `{', '.join(case['profiles_observed']) or 'none'}`",
                f"- Missing expected profiles: `{', '.join(case['missing_expected_profiles']) or 'none'}`",
                "",
            ]
        )
        if case["failure_signals"]:
            lines.append("Failure signals:")
            lines.extend(f"- {signal}" for signal in case["failure_signals"])
            lines.append("")
        if case["notes"]:
            lines.extend(["Notes:", "", case["notes"], ""])

    lines.extend(["## Promoted Scenario Candidates", ""])
    if not evidence["promoted_candidates"]:
        lines.append("- None.")
    for candidate in evidence["promoted_candidates"]:
        lines.append(
            f"- `{candidate['scenario_candidate']}`: "
            f"{candidate['title']} ({candidate['promotion_reason']})."
        )
    lines.append("")
    return "\n".join(lines)


def write_evidence(evidence: dict[str, Any], *, output_dir: pathlib.Path) -> dict[str, str]:
    output_dir.mkdir(parents=True, exist_ok=True)
    json_path = output_dir / "bot_duel_ctf_playdepth_evidence.json"
    markdown_path = output_dir / "bot_duel_ctf_playdepth_evidence.md"
    json_path.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8", newline="\n")
    markdown_path.write_text(render_markdown(evidence), encoding="utf-8", newline="\n")
    return {"json": str(json_path), "markdown": str(markdown_path)}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=pathlib.Path, default=REPO_ROOT)
    parser.add_argument("--plan", type=pathlib.Path, default=DEFAULT_PLAYTEST_JSON)
    parser.add_argument("--notes", type=pathlib.Path, default=DEFAULT_NOTES_JSON)
    parser.add_argument("--output-dir", type=pathlib.Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--required-case", action="append", dest="required_cases")
    parser.add_argument("--repeat-threshold", type=int, default=2)
    parser.add_argument("--strict", action="store_true", help="Return nonzero unless required evidence passed.")
    parser.add_argument("--format", choices=("text", "json"), default="text")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo_root = args.repo_root.resolve()
    plan_path = resolve_repo_path(repo_root, args.plan)
    notes_path = resolve_repo_path(repo_root, args.notes)
    output_dir = resolve_repo_path(repo_root, args.output_dir)

    plan = load_json(plan_path)
    notes = (
        load_json(notes_path)
        if notes_path.is_file()
        else triage_bot_playtest.default_notes_from_plan(plan)
    )
    evidence = build_evidence(
        plan,
        notes,
        required_cases=args.required_cases,
        repeat_threshold=args.repeat_threshold,
    )
    artifacts = write_evidence(evidence, output_dir=output_dir)
    evidence["artifacts"] = artifacts

    if args.format == "json":
        print(json.dumps(evidence, indent=2))
    else:
        summary = evidence["summary"]
        print(
            "bot play-depth evidence: "
            f"{summary['status']} required={summary['required_cases']} "
            f"passed={summary['passed']} failed={summary['failed']} "
            f"blocked={summary['blocked']} pending={summary['pending']} "
            f"promoted={summary['promoted_candidates']}"
        )
        print(f"json: {artifacts['json']}")
        print(f"markdown: {artifacts['markdown']}")

    if evidence["summary"]["status"] == "failed":
        return 1
    if args.strict and evidence["summary"]["status"] != "passed":
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
