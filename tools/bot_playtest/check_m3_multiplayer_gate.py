#!/usr/bin/env python3
"""Evaluate the M3 Multiplayer Mode Intelligence completion gate."""

from __future__ import annotations

import argparse
import json
import pathlib
from typing import Any


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_SCENARIO_REPORT = pathlib.Path(".tmp") / "bot_scenarios" / "implemented_hazard_context.json"
DEFAULT_PLAYDEPTH_EVIDENCE = pathlib.Path(".tmp") / "bot_playtest" / "bot_duel_ctf_playdepth_evidence.json"
DEFAULT_OUTPUT_DIR = pathlib.Path(".tmp") / "bot_playtest"
SCHEMA = "worr.bot_m3_multiplayer_gate.v1"

REQUIRED_AUTOMATED_SCENARIOS = (
    "duel_queue_spectator",
    "tdm_role_spawn_stability",
    "ffa_live_pacing",
    "duel_live_pacing",
    "ctf_objective_route",
    "ctf_objective_transitions",
)

REQUIRED_PLAYDEPTH_CASES = ("duel_rotation", "ctf_objectives")


def resolve_repo_path(repo_root: pathlib.Path, path: pathlib.Path) -> pathlib.Path:
    if path.is_absolute():
        return path
    return repo_root / path


def load_json(path: pathlib.Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def scenario_rows(payload: dict[str, Any]) -> list[dict[str, Any]]:
    rows = payload.get("scenarios", [])
    if isinstance(rows, list):
        return [row for row in rows if isinstance(row, dict)]
    return []


def row_passed(row: dict[str, Any]) -> bool:
    if row.get("status") not in {None, "passed", "pass"}:
        return False
    failures = row.get("failures", [])
    if isinstance(failures, list) and failures:
        return False
    if "returncode" in row:
        try:
            if int(row.get("returncode") or 0) != 0:
                return False
        except (TypeError, ValueError):
            return False
    if "duration_budget_passed" in row and not bool(row.get("duration_budget_passed")):
        return False
    return True


def automated_results(payload: dict[str, Any]) -> tuple[list[dict[str, Any]], list[str]]:
    by_name = {str(row.get("name")): row for row in scenario_rows(payload) if row.get("name")}
    rows: list[dict[str, Any]] = []
    failures: list[str] = []
    for name in REQUIRED_AUTOMATED_SCENARIOS:
        row = by_name.get(name)
        if row is None:
            rows.append({"name": name, "status": "missing", "passed": False})
            failures.append(f"missing M3 automated scenario: {name}")
            continue
        passed = row_passed(row)
        rows.append(
            {
                "name": name,
                "status": row.get("status", ""),
                "passed": passed,
                "returncode": row.get("returncode"),
                "duration_budget_passed": row.get("duration_budget_passed"),
                "failure_count": len(row.get("failures", [])) if isinstance(row.get("failures"), list) else 0,
            }
        )
        if not passed:
            failures.append(f"M3 automated scenario did not pass: {name}")
    return rows, failures


def playdepth_case_results(evidence: dict[str, Any] | None) -> tuple[list[dict[str, Any]], list[str], list[str]]:
    if evidence is None:
        return (
            [{"id": case_id, "outcome": "missing", "passed": False} for case_id in REQUIRED_PLAYDEPTH_CASES],
            [],
            ["missing Duel/CTF play-depth evidence attachment"],
        )

    by_id = {
        str(case.get("id")): case
        for case in evidence.get("cases", [])
        if isinstance(case, dict) and case.get("id")
    }
    failures: list[str] = []
    pending: list[str] = []
    rows: list[dict[str, Any]] = []
    for case_id in REQUIRED_PLAYDEPTH_CASES:
        case = by_id.get(case_id)
        if case is None:
            rows.append({"id": case_id, "outcome": "missing", "passed": False})
            failures.append(f"missing required play-depth case evidence: {case_id}")
            continue
        outcome = str(case.get("outcome", "pending")).lower()
        passed = outcome == "pass"
        rows.append(
            {
                "id": case_id,
                "mode": case.get("mode", ""),
                "map": case.get("map", ""),
                "outcome": outcome,
                "passed": passed,
                "botlist_present": bool(case.get("botlist_present")),
                "failure_signals": len(case.get("failure_signals", [])),
            }
        )
        if outcome in {"fail", "blocked"}:
            failures.append(f"play-depth case is {outcome}: {case_id}")
        elif not passed:
            pending.append(f"play-depth case is {outcome}: {case_id}")

    summary = evidence.get("summary", {})
    if int(summary.get("promoted_candidates", 0) or 0) > 0:
        failures.append("play-depth evidence has promoted scenario candidates")
    return rows, failures, pending


def evaluate_gate(
    scenario_report: dict[str, Any],
    playdepth_evidence: dict[str, Any] | None,
) -> dict[str, Any]:
    scenario_rows_out, scenario_failures = automated_results(scenario_report)
    playdepth_rows, playdepth_failures, playdepth_pending = playdepth_case_results(playdepth_evidence)
    failures = [*scenario_failures, *playdepth_failures]
    pending = list(playdepth_pending)
    status = "failed" if failures else "pending" if pending else "passed"
    return {
        "schema": SCHEMA,
        "summary": {
            "status": status,
            "automated_scenarios": len(REQUIRED_AUTOMATED_SCENARIOS),
            "automated_passed": sum(1 for row in scenario_rows_out if row["passed"]),
            "playdepth_cases": len(REQUIRED_PLAYDEPTH_CASES),
            "playdepth_passed": sum(1 for row in playdepth_rows if row["passed"]),
            "failures": len(failures),
            "pending": len(pending),
        },
        "failures": failures,
        "pending": pending,
        "automated_scenarios": scenario_rows_out,
        "playdepth_cases": playdepth_rows,
    }


def render_markdown(report: dict[str, Any]) -> str:
    summary = report["summary"]
    lines = [
        "# WORR Bot M3 Multiplayer Gate",
        "",
        f"Status: `{summary['status']}`",
        (
            f"Automated scenarios: `{summary['automated_passed']}` / "
            f"`{summary['automated_scenarios']}`"
        ),
        f"Play-depth cases: `{summary['playdepth_passed']}` / `{summary['playdepth_cases']}`",
        "",
    ]
    if report["failures"]:
        lines.extend(["## Failures", ""])
        lines.extend(f"- {failure}" for failure in report["failures"])
        lines.append("")
    if report["pending"]:
        lines.extend(["## Pending", ""])
        lines.extend(f"- {item}" for item in report["pending"])
        lines.append("")

    lines.extend(["## Automated Baseline", ""])
    for row in report["automated_scenarios"]:
        mark = "pass" if row["passed"] else "fail"
        lines.append(f"- `{row['name']}`: `{mark}`")
    lines.extend(["", "## Play-Depth Cases", ""])
    for row in report["playdepth_cases"]:
        mark = "pass" if row["passed"] else row["outcome"]
        lines.append(f"- `{row['id']}`: `{mark}`")
    lines.append("")
    return "\n".join(lines)


def write_report(report: dict[str, Any], *, output_dir: pathlib.Path) -> dict[str, str]:
    output_dir.mkdir(parents=True, exist_ok=True)
    json_path = output_dir / "bot_m3_multiplayer_gate.json"
    markdown_path = output_dir / "bot_m3_multiplayer_gate.md"
    json_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")
    markdown_path.write_text(render_markdown(report), encoding="utf-8", newline="\n")
    return {"json": str(json_path), "markdown": str(markdown_path)}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=pathlib.Path, default=REPO_ROOT)
    parser.add_argument("--scenario-report", type=pathlib.Path, default=DEFAULT_SCENARIO_REPORT)
    parser.add_argument("--playdepth-evidence", type=pathlib.Path, default=DEFAULT_PLAYDEPTH_EVIDENCE)
    parser.add_argument("--output-dir", type=pathlib.Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--strict", action="store_true", help="Return nonzero unless M3 is fully passed.")
    parser.add_argument("--format", choices=("text", "json"), default="text")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo_root = args.repo_root.resolve()
    scenario_path = resolve_repo_path(repo_root, args.scenario_report)
    evidence_path = resolve_repo_path(repo_root, args.playdepth_evidence)
    output_dir = resolve_repo_path(repo_root, args.output_dir)

    scenario_report = load_json(scenario_path)
    evidence = load_json(evidence_path) if evidence_path.is_file() else None
    report = evaluate_gate(scenario_report, evidence)
    artifacts = write_report(report, output_dir=output_dir)
    report["artifacts"] = artifacts

    if args.format == "json":
        print(json.dumps(report, indent=2))
    else:
        summary = report["summary"]
        print(
            "bot M3 multiplayer gate: "
            f"{summary['status']} automated={summary['automated_passed']}/"
            f"{summary['automated_scenarios']} playdepth={summary['playdepth_passed']}/"
            f"{summary['playdepth_cases']} failures={summary['failures']} "
            f"pending={summary['pending']}"
        )
        print(f"json: {artifacts['json']}")
        print(f"markdown: {artifacts['markdown']}")

    if report["summary"]["status"] == "failed":
        return 1
    if args.strict and report["summary"]["status"] != "passed":
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
