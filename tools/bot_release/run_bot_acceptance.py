#!/usr/bin/env python3
"""Run a release-readiness acceptance audit for WORR bots."""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
from dataclasses import dataclass, field
from typing import Any


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS_ROOT = REPO_ROOT / "tools"

sys.path.insert(0, str(TOOLS_ROOT))
sys.path.insert(0, str(TOOLS_ROOT / "bot_playtest"))
sys.path.insert(0, str(TOOLS_ROOT / "bot_perf"))
sys.path.insert(0, str(TOOLS_ROOT / "bot_profiles"))
sys.path.insert(0, str(TOOLS_ROOT / "bot_surface"))

import package_assets  # noqa: E402
import generate_bot_playtest  # noqa: E402
import build_bot_playdepth_evidence  # noqa: E402
import check_m3_multiplayer_gate  # noqa: E402
import run_bot_playdepth_headless  # noqa: E402
import analyze_bot_perf  # noqa: E402
import triage_bot_playtest  # noqa: E402
import validate_bot_profiles  # noqa: E402
import audit_bot_surface  # noqa: E402


REQUIRED_BOT_PROFILES = ("bulwark", "relay", "smoke", "vanguard", "vector")
REQUIRED_AAS_MAPS = (
    "mm-rage",
    "worr_crouch_ref",
    "q2dm1",
    "q2dm2",
    "q2dm7",
    "q2dm8",
    "q2ctf1",
    "base1",
    "base2",
    "fact2",
    "train",
)
REQUIRED_REFERENCE_BSPS = ("worr_crouch_ref",)
REQUIRED_USER_DOCS = (
    "docs-user/bots.md",
    "docs-user/bot-cvars.md",
    "docs-user/bot-profiles.md",
    "docs-user/bot-chat.md",
    "docs-user/bot-map-readiness.md",
    "docs-user/bot-playtest.md",
)
REQUIRED_CHAT_DOC_CVARS = (
    "bot_allow_chat",
    "bot_chat_live_events",
    "bot_chat_min_interval_ms",
    "bot_chat_team_only",
)
REQUIRED_CHAT_DOC_EVENTS = (
    "spawn",
    "team_ready",
    "route_ready",
    "item_taken",
    "item_denied",
    "enemy_sighted",
    "objective_changed",
    "flag_state",
    "low_health",
    "blocked",
    "victory_defeat",
)
REQUIRED_PLAYTEST_MODES = ("FFA", "Duel", "TDM", "CTF")
REQUIRED_PERF_BUDGETS = (
    "default_soak_budget.json",
    "source_counter_soak_budget.json",
)
REQUIRED_VARIANCE_BUDGETS = ("source_counter_variance_budget.json",)
REQUIRED_SCENARIOS = (
    "spawn_route_to_item",
    "behavior_arbitration",
    "combat_survival_regression",
    "ffa_live_pacing",
    "duel_live_pacing",
    "ctf_objective_transitions",
    "coop_campaign_interaction_matrix",
    "coop_campaign_interaction_matrix_base2",
    "coop_campaign_interaction_depth_base2",
    "coop_campaign_progression_chain_base2",
    "coop_campaign_progression_consumer_base2",
    "coop_campaign_post_interaction_base2",
    "coop_campaign_progression_carry_base2",
    "coop_campaign_keyed_path_train",
    "coop_campaign_key_carry_train",
    "movement_crouch_route",
    "movement_hazard_context",
    "min_players_profile_coverage",
)
REQUIRED_MOVEMENT_AUDIT_CHECKS = {
    "natural_crouch": "movement_crouch_route",
    "hazard_context": "movement_hazard_context",
}


@dataclass
class CheckResult:
    name: str
    status: str
    message: str
    metrics: dict[str, Any] = field(default_factory=dict)
    failures: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)
    artifacts: dict[str, str] = field(default_factory=dict)

    def to_json(self) -> dict[str, Any]:
        return {
            "name": self.name,
            "status": self.status,
            "message": self.message,
            "metrics": self.metrics,
            "failures": self.failures,
            "warnings": self.warnings,
            "artifacts": self.artifacts,
        }


def ok(
    name: str,
    message: str,
    *,
    metrics: dict[str, Any] | None = None,
    artifacts: dict[str, str] | None = None,
    warnings: list[str] | None = None,
) -> CheckResult:
    return CheckResult(
        name=name,
        status="pass",
        message=message,
        metrics=metrics or {},
        artifacts=artifacts or {},
        warnings=warnings or [],
    )


def fail(
    name: str,
    message: str,
    failures: list[str],
    *,
    metrics: dict[str, Any] | None = None,
    artifacts: dict[str, str] | None = None,
    warnings: list[str] | None = None,
) -> CheckResult:
    return CheckResult(
        name=name,
        status="fail",
        message=message,
        failures=failures,
        metrics=metrics or {},
        artifacts=artifacts or {},
        warnings=warnings or [],
    )


def warn(
    name: str,
    message: str,
    warnings: list[str],
    *,
    metrics: dict[str, Any] | None = None,
    artifacts: dict[str, str] | None = None,
) -> CheckResult:
    return CheckResult(
        name=name,
        status="warn",
        message=message,
        warnings=warnings,
        metrics=metrics or {},
        artifacts=artifacts or {},
    )


def load_json(path: pathlib.Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def rel(path: pathlib.Path, repo_root: pathlib.Path) -> str:
    try:
        return path.resolve().relative_to(repo_root.resolve()).as_posix()
    except ValueError:
        return str(path)


def bots_txt_entries(path: pathlib.Path) -> dict[str, str]:
    text = path.read_text(encoding="utf-8", errors="replace")
    entries: dict[str, str] = {}
    current_name: str | None = None
    for raw_line in text.splitlines():
        parts = raw_line.strip().split()
        if len(parts) < 2:
            continue
        key, value = parts[0].lower(), parts[1]
        if key == "name":
            current_name = value.lower()
            entries.setdefault(current_name, "")
        elif key == "aifile" and current_name:
            entries[current_name] = value.replace("\\", "/")
    return entries


def check_surface(repo_root: pathlib.Path) -> CheckResult:
    result = audit_bot_surface.audit_repo(repo_root)
    payload = result.to_json()
    summary = payload["summary"]
    metrics = {
        "cvars": summary["cvars"],
        "commands": summary["commands"],
        "public_cvars": summary["classifications"].get("public", 0),
        "smoke_only_cvars": summary["classifications"].get("smoke-only", 0),
        "violations": summary["violations"],
        "warnings": summary["warnings"],
    }
    if result.violations:
        return fail(
            "public_surface",
            "public bot surface audit failed",
            result.violations,
            metrics=metrics,
        )
    return ok(
        "public_surface",
        "public bot cvars and Q3-style commands are clean",
        metrics=metrics,
        warnings=result.warnings,
    )


def check_profiles(repo_root: pathlib.Path) -> CheckResult:
    report = validate_bot_profiles.validate_paths(
        [str(repo_root / "assets" / "botfiles" / "bots")],
        validate_bot_profiles.ValidationOptions(
            allow_unknown=False,
            fail_on_empty=True,
            check_companions=True,
        ),
        cwd=repo_root,
    )
    summary = report["summary"]
    profile_ids = sorted(profile["id"] for profile in report.get("profiles", []))
    missing = sorted(set(REQUIRED_BOT_PROFILES) - set(profile_ids))
    errors = [
        validate_bot_profiles.format_issue(issue)
        for issue in report.get("issues", [])
        if issue.get("severity") == "error"
    ]
    warnings = [
        validate_bot_profiles.format_issue(issue)
        for issue in report.get("issues", [])
        if issue.get("severity") == "warning"
    ]
    failures = errors + [f"missing required first-party bot profile: {name}" for name in missing]
    metrics = {
        "files": summary["files"],
        "profiles": summary["profiles"],
        "errors": summary["errors"],
        "warnings": summary["warnings"],
        "required_profiles": len(REQUIRED_BOT_PROFILES),
    }
    if failures:
        return fail(
            "profile_pack",
            "bot profile validation failed",
            failures,
            metrics=metrics,
            warnings=warnings,
        )
    return ok(
        "profile_pack",
        "first-party bot profiles validate and cover min-player rotation",
        metrics=metrics,
        warnings=warnings,
    )


def check_bots_txt(repo_root: pathlib.Path) -> CheckResult:
    bots_txt = repo_root / "assets" / "botfiles" / "bots.txt"
    if not bots_txt.is_file():
        return fail("bots_txt", "bots.txt is missing", [f"missing {bots_txt}"])

    entries = bots_txt_entries(bots_txt)
    failures: list[str] = []
    for name in REQUIRED_BOT_PROFILES:
        aifile = entries.get(name)
        expected = f"bots/{name}_c.c"
        if aifile != expected:
            failures.append(
                f"bots.txt entry {name!r} should reference {expected!r}; found {aifile!r}"
            )

    metrics = {
        "entries": len(entries),
        "required_entries": len(REQUIRED_BOT_PROFILES),
    }
    if failures:
        return fail(
            "bots_txt",
            "bots.txt does not expose the first-party character roster",
            failures,
            metrics=metrics,
            artifacts={"bots_txt": rel(bots_txt, repo_root)},
        )
    return ok(
        "bots_txt",
        "bots.txt exposes the first-party character roster",
        metrics=metrics,
        artifacts={"bots_txt": rel(bots_txt, repo_root)},
    )


def check_authored_botfiles(repo_root: pathlib.Path) -> CheckResult:
    assets_dir = repo_root / "assets"
    try:
        members = package_assets.botfile_release_members(assets_dir)
    except SystemExit as exc:
        return fail(
            "authored_botfiles",
            "authored botfile release payload is invalid",
            [str(exc)],
        )

    failures: list[str] = []
    member_set = set(members)
    for name in REQUIRED_BOT_PROFILES:
        for suffix in package_assets.BOTFILE_PROFILE_SUFFIXES:
            member = f"botfiles/bots/{name}{suffix}"
            if member not in member_set:
                failures.append(f"missing authored botfile member: {member}")
        script_member = f"botfiles/scripts/{name}{package_assets.BOTFILE_SCRIPT_SUFFIX}"
        if script_member not in member_set:
            failures.append(f"missing authored bot script member: {script_member}")

    metrics = {
        "members": len(members),
        "required_profiles": len(REQUIRED_BOT_PROFILES),
    }
    if failures:
        return fail(
            "authored_botfiles",
            "authored botfile release payload is incomplete",
            failures,
            metrics=metrics,
        )
    return ok(
        "authored_botfiles",
        "authored botfile release payload is complete",
        metrics=metrics,
    )


def check_staged_botfiles(repo_root: pathlib.Path, install_dir: pathlib.Path, base_game: str) -> CheckResult:
    assets_dir = repo_root / "assets"
    output_dir = install_dir / base_game
    archive_path = output_dir / "pak0.pkz"

    try:
        members = package_assets.botfile_release_members(assets_dir)
        package_assets.validate_botfile_payload(assets_dir, output_dir, archive_path, members)
    except SystemExit as exc:
        return fail(
            "staged_botfiles",
            "staged package/loose botfiles are invalid",
            [str(exc)],
            artifacts={
                "install_dir": rel(install_dir, repo_root),
                "archive": rel(archive_path, repo_root),
            },
        )

    metrics = {
        "members": len(members),
        "archive_bytes": archive_path.stat().st_size if archive_path.exists() else 0,
    }
    return ok(
        "staged_botfiles",
        "staged archive and loose botfiles match authored assets",
        metrics=metrics,
        artifacts={
            "install_dir": rel(install_dir, repo_root),
            "archive": rel(archive_path, repo_root),
        },
    )


def check_staged_aas(repo_root: pathlib.Path, install_dir: pathlib.Path, base_game: str) -> CheckResult:
    maps_dir = install_dir / base_game / "maps"
    failures: list[str] = []
    sizes: dict[str, int] = {}
    for map_name in REQUIRED_AAS_MAPS:
        path = maps_dir / f"{map_name}.aas"
        if not path.is_file():
            failures.append(f"missing staged AAS: {rel(path, repo_root)}")
            continue
        size = path.stat().st_size
        sizes[map_name] = size
        if size <= 0:
            failures.append(f"empty staged AAS: {rel(path, repo_root)}")

    reference_bsps: dict[str, int] = {}
    for map_name in REQUIRED_REFERENCE_BSPS:
        path = maps_dir / f"{map_name}.bsp"
        if not path.is_file():
            failures.append(f"missing staged q2aas reference BSP: {rel(path, repo_root)}")
            continue
        size = path.stat().st_size
        reference_bsps[map_name] = size
        if size <= 0:
            failures.append(f"empty staged q2aas reference BSP: {rel(path, repo_root)}")

    metrics = {
        "required_maps": len(REQUIRED_AAS_MAPS),
        "present_maps": len(sizes),
        "required_reference_bsps": len(REQUIRED_REFERENCE_BSPS),
        "present_reference_bsps": len(reference_bsps),
        "total_aas_bytes": sum(sizes.values()),
    }
    if failures:
        return fail(
            "staged_aas",
            "required staged AAS/reference map files are missing or empty",
            failures,
            metrics=metrics,
            artifacts={"maps_dir": rel(maps_dir, repo_root)},
        )
    return ok(
        "staged_aas",
        "required staged AAS and q2aas reference maps are present",
        metrics=metrics,
        artifacts={"maps_dir": rel(maps_dir, repo_root)},
    )


def check_user_docs(repo_root: pathlib.Path) -> CheckResult:
    failures: list[str] = []
    metrics: dict[str, Any] = {}
    texts: dict[str, str] = {}
    for doc in REQUIRED_USER_DOCS:
        path = repo_root / doc
        if not path.is_file():
            failures.append(f"missing user doc: {doc}")
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        texts[doc] = text
        metrics[doc] = len(text)

    bots_text = texts.get("docs-user/bots.md", "")
    for token in ("bot_min_players", "addbot", "bot_reload_profiles"):
        if token not in bots_text:
            failures.append(f"docs-user/bots.md should mention {token}")

    chat_text = texts.get("docs-user/bot-chat.md", "")
    if chat_text:
        missing_cvars = [
            token for token in REQUIRED_CHAT_DOC_CVARS if token not in chat_text
        ]
        missing_events = [
            token for token in REQUIRED_CHAT_DOC_EVENTS if token not in chat_text
        ]
        metrics["chat_doc_required_cvars"] = len(REQUIRED_CHAT_DOC_CVARS)
        metrics["chat_doc_required_events"] = len(REQUIRED_CHAT_DOC_EVENTS)
        metrics["chat_doc_missing_cvars"] = len(missing_cvars)
        metrics["chat_doc_missing_events"] = len(missing_events)
        failures.extend(
            f"docs-user/bot-chat.md should mention {token}"
            for token in missing_cvars + missing_events
        )

    for doc in ("docs-user/bots.md", "docs-user/bot-cvars.md", "docs-user/bot-profiles.md"):
        if texts.get(doc, "") and "bot-chat.md" not in texts[doc]:
            failures.append(f"{doc} should link to bot-chat.md")

    if failures:
        return fail("user_docs", "bot user documentation is incomplete", failures, metrics=metrics)
    return ok(
        "user_docs",
        "bot user documentation covers setup, profiles, chat, and map readiness",
        metrics=metrics,
    )


def check_playtest_plan(repo_root: pathlib.Path) -> CheckResult:
    cases = generate_bot_playtest.default_playtest_cases()
    failures = generate_bot_playtest.validate_cases(cases)
    modes = {case.mode for case in cases}
    for mode in REQUIRED_PLAYTEST_MODES:
        if mode not in modes:
            failures.append(f"playtest plan missing required mode: {mode}")

    for case in cases:
        command_text = "\n".join(case.command_lines())
        if "bot_min_players" not in command_text:
            failures.append(f"{case.case_id} does not exercise bot_min_players")
        if case.map_name not in REQUIRED_AAS_MAPS:
            failures.append(
                f"{case.case_id} uses map {case.map_name!r}, which is not in the release AAS gate"
            )

    metrics = {
        "cases": len(cases),
        "modes": len(modes),
        "total_minutes": sum(case.duration_minutes for case in cases),
        "configs": len({case.config_name for case in cases}),
    }
    artifacts = {
        "tool": "tools/bot_playtest/generate_bot_playtest.py",
        "doc": "docs-user/bot-playtest.md",
    }
    if failures:
        return fail(
            "playtest_plan",
            "multiplayer bot playtest plan is incomplete",
            failures,
            metrics=metrics,
            artifacts=artifacts,
        )
    return ok(
        "playtest_plan",
        "multiplayer bot playtest covers FFA, Duel, TDM, and CTF",
        metrics=metrics,
        artifacts=artifacts,
    )


def check_playtest_triage(repo_root: pathlib.Path) -> CheckResult:
    cases = generate_bot_playtest.default_playtest_cases()
    missing = triage_bot_playtest.validate_category_coverage(cases)
    categories = {
        category.key: category
        for category in triage_bot_playtest.FAILURE_CATEGORIES
    }
    failures: list[str] = []
    if missing:
        failures.extend(f"unclassified playtest failure signal: {item}" for item in missing)

    required_categories = {
        "route_commitment",
        "route_stuck",
        "close_threat_spacing",
        "weak_retreat",
        "min_players",
        "duel_queue",
        "ctf_objective",
    }
    missing_categories = sorted(required_categories - set(categories))
    failures.extend(f"missing playtest triage category: {item}" for item in missing_categories)

    metrics = {
        "categories": len(categories),
        "case_failure_signals": sum(len(case.failure_signals) for case in cases),
        "critical_categories": sum(
            1 for category in triage_bot_playtest.FAILURE_CATEGORIES if category.critical
        ),
    }
    artifacts = {
        "tool": "tools/bot_playtest/triage_bot_playtest.py",
        "notes_template": ".tmp/bot_playtest/bot_multiplayer_playtest_notes_template.json",
    }
    if failures:
        return fail(
            "playtest_triage",
            "multiplayer bot playtest triage catalog is incomplete",
            failures,
            metrics=metrics,
            artifacts=artifacts,
        )
    return ok(
        "playtest_triage",
        "multiplayer bot playtest failure signals map to scenario candidates",
        metrics=metrics,
        artifacts=artifacts,
    )


def passing_playdepth_notes(plan: dict[str, Any]) -> dict[str, Any]:
    notes = triage_bot_playtest.default_notes_from_plan(plan)
    for case in notes.get("cases", []):
        case_id = case.get("id")
        if case_id == "duel_rotation":
            case["outcome"] = "pass"
            case["botlist"] = "B|Vanguard\nB|Vector"
            case["profiles_observed"] = ["vanguard", "vector"]
            case["notes"] = "Synthetic release-tooling proof: Duel required case can be recorded."
        elif case_id == "ctf_objectives":
            case["outcome"] = "pass"
            case["botlist"] = "B|Vanguard\nB|Vector\nB|Bulwark\nB|Relay\nB|Smoke\nB|Vanguard"
            case["profiles_observed"] = ["vanguard", "vector", "bulwark", "relay", "smoke"]
            case["notes"] = "Synthetic release-tooling proof: CTF required case can be recorded."
    return notes


def check_playdepth_evidence_tooling(repo_root: pathlib.Path) -> CheckResult:
    cases = generate_bot_playtest.default_playtest_cases()
    generated_at = "2026-06-30T00:00:00Z"
    plan = generate_bot_playtest.build_payload(
        cases,
        repo_root=repo_root,
        output_dir=repo_root / ".tmp" / "bot_playtest",
        base_game="basew",
        generated_at=generated_at,
    )
    evidence = build_bot_playdepth_evidence.build_evidence(
        plan,
        passing_playdepth_notes(plan),
        required_cases=build_bot_playdepth_evidence.DEFAULT_REQUIRED_CASES,
    )
    markdown = build_bot_playdepth_evidence.render_markdown(evidence)
    failures: list[str] = []
    summary = evidence.get("summary", {})
    if evidence.get("schema") != build_bot_playdepth_evidence.SCHEMA:
        failures.append("play-depth evidence schema mismatch")
    if summary.get("status") != "passed":
        failures.append(f"play-depth evidence proof did not pass: {summary.get('status')}")
    for case_id in build_bot_playdepth_evidence.DEFAULT_REQUIRED_CASES:
        if case_id not in evidence.get("required_cases", []):
            failures.append(f"missing required play-depth case: {case_id}")
        if case_id not in markdown:
            failures.append(f"play-depth attachment markdown omits case: {case_id}")

    metrics = {
        "required_cases": int(summary.get("required_cases", 0) or 0),
        "passed": int(summary.get("passed", 0) or 0),
        "botlists_present": int(summary.get("botlists_present", 0) or 0),
        "promoted_candidates": int(summary.get("promoted_candidates", 0) or 0),
    }
    artifacts = {
        "tool": "tools/bot_playtest/build_bot_playdepth_evidence.py",
        "json": ".tmp/bot_playtest/bot_duel_ctf_playdepth_evidence.json",
        "markdown": ".tmp/bot_playtest/bot_duel_ctf_playdepth_evidence.md",
    }
    if failures:
        return fail(
            "playdepth_evidence_tooling",
            "Duel/CTF play-depth release attachment tooling is incomplete",
            failures,
            metrics=metrics,
            artifacts=artifacts,
        )
    return ok(
        "playdepth_evidence_tooling",
        "Duel/CTF play-depth release attachment tooling is ready",
        metrics=metrics,
        artifacts=artifacts,
    )


def m3_synthetic_scenario_report() -> dict[str, Any]:
    return {
        "scenarios": [
            {
                "name": name,
                "status": "passed",
                "returncode": 0,
                "duration_budget_passed": True,
                "failures": [],
            }
            for name in check_m3_multiplayer_gate.REQUIRED_AUTOMATED_SCENARIOS
        ]
    }


def check_m3_multiplayer_gate_tooling(repo_root: pathlib.Path) -> CheckResult:
    cases = generate_bot_playtest.default_playtest_cases()
    plan = generate_bot_playtest.build_payload(
        cases,
        repo_root=repo_root,
        output_dir=repo_root / ".tmp" / "bot_playtest",
        base_game="basew",
        generated_at="2026-06-30T00:00:00Z",
    )
    gate_report = check_m3_multiplayer_gate.evaluate_gate(
        m3_synthetic_scenario_report(),
        build_bot_playdepth_evidence.build_evidence(
            plan,
            passing_playdepth_notes(plan),
            required_cases=build_bot_playdepth_evidence.DEFAULT_REQUIRED_CASES,
        ),
    )
    markdown = check_m3_multiplayer_gate.render_markdown(gate_report)
    summary = gate_report.get("summary", {})
    failures: list[str] = []
    if gate_report.get("schema") != check_m3_multiplayer_gate.SCHEMA:
        failures.append("M3 gate schema mismatch")
    if summary.get("status") != "passed":
        failures.append(f"M3 synthetic gate did not pass: {summary.get('status')}")
    for name in check_m3_multiplayer_gate.REQUIRED_AUTOMATED_SCENARIOS:
        if name not in markdown:
            failures.append(f"M3 gate markdown omits automated scenario: {name}")

    metrics = {
        "automated_scenarios": int(summary.get("automated_scenarios", 0) or 0),
        "automated_passed": int(summary.get("automated_passed", 0) or 0),
        "playdepth_cases": int(summary.get("playdepth_cases", 0) or 0),
        "playdepth_passed": int(summary.get("playdepth_passed", 0) or 0),
    }
    artifacts = {
        "tool": "tools/bot_playtest/check_m3_multiplayer_gate.py",
        "json": ".tmp/bot_playtest/bot_m3_multiplayer_gate.json",
        "markdown": ".tmp/bot_playtest/bot_m3_multiplayer_gate.md",
    }
    if failures:
        return fail(
            "m3_multiplayer_gate_tooling",
            "M3 multiplayer milestone gate tooling is incomplete",
            failures,
            metrics=metrics,
            artifacts=artifacts,
        )
    return ok(
        "m3_multiplayer_gate_tooling",
        "M3 multiplayer milestone gate tooling is ready",
        metrics=metrics,
        artifacts=artifacts,
    )


def check_playdepth_headless_tooling(repo_root: pathlib.Path) -> CheckResult:
    cases = generate_bot_playtest.default_playtest_cases()
    plan = generate_bot_playtest.build_payload(
        cases,
        repo_root=repo_root,
        output_dir=repo_root / ".tmp" / "bot_playtest",
        base_game="basew",
        generated_at="2026-06-30T00:00:00Z",
    )
    selected, missing = run_bot_playdepth_headless.selected_cases(
        plan,
        run_bot_playdepth_headless.DEFAULT_REQUIRED_CASES,
    )
    failures = [f"missing required headless case: {case_id}" for case_id in missing]
    results: list[dict[str, Any]] = []
    for index, case in enumerate(selected):
        command = run_bot_playdepth_headless.build_command(
            binary=pathlib.Path(".install") / "worr_ded_x86_64.exe",
            install_dir=pathlib.Path(".install"),
            base_game="basew",
            port=28100 + index,
            case=case,
            log_name=f"bot_playdepth_{case['id']}_proof",
            startup_wait=12,
            run_wait=34,
        )
        command_text = " ".join(command)
        if "+map" not in command or str(case.get("map", "")) not in command:
            failures.append(f"headless command does not map {case['id']}")
        if command.count("+botlist") != 2:
            failures.append(f"headless command must capture botlist twice: {case['id']}")
        if command[-1] != "+quit":
            failures.append(f"headless command must terminate server: {case['id']}")
        if "bot_min_players" not in command_text:
            failures.append(f"headless command omits bot_min_players: {case['id']}")

        bot_lines = [
            "Added bot B|Vanguard in slot 0.",
            "Added bot B|Vector in slot 1.",
            "  0 spawned   B|Vanguard",
            "  1 spawned   B|Vector",
        ]
        if case.get("id") == "ctf_objectives":
            bot_lines.extend(
                [
                    "Added bot B|Bulwark in slot 2.",
                    "Added bot B|Relay in slot 3.",
                    "Added bot B|Smoke in slot 4.",
                    "  2 spawned   B|Bulwark",
                    "  3 spawned   B|Relay",
                    "  4 spawned   B|Smoke",
                ]
            )
        combined = "\n".join(bot_lines)
        profiles_observed = run_bot_playdepth_headless.extract_profiles(combined)
        results.append(
            {
                "id": case["id"],
                "title": case.get("title", ""),
                "mode": case.get("mode", ""),
                "map": case.get("map", ""),
                "bot_target": int(case.get("bot_target", 0) or 0),
                "port": 28100 + index,
                "log_name": f"bot_playdepth_{case['id']}_proof",
                "command": command,
                "stdout_path": f".tmp/bot_playtest/headless/{case['id']}.stdout.txt",
                "stderr_path": f".tmp/bot_playtest/headless/{case['id']}.stderr.txt",
                "returncode": 0,
                "timed_out": False,
                "duration_seconds": 0.0,
                "botlist_lines": bot_lines,
                "bot_names": run_bot_playdepth_headless.extract_bot_names(combined),
                "profiles_observed": profiles_observed,
                "profile_coverage": run_bot_playdepth_headless.case_profile_coverage(
                    case,
                    profiles_observed,
                ),
                "failures": [],
                "status": "passed",
            }
        )

    report = run_bot_playdepth_headless.build_report(
        plan=plan,
        results=results,
        missing_cases=missing,
        output_dir=repo_root / ".tmp" / "bot_playtest" / "headless",
    )
    notes = run_bot_playdepth_headless.build_notes_from_results(
        plan,
        results,
        pathlib.Path(".tmp") / "bot_playtest" / "headless" / "bot_playdepth_headless_runs.json",
    )
    markdown = run_bot_playdepth_headless.render_markdown(report)
    summary = report.get("summary", {})
    if report.get("schema") != run_bot_playdepth_headless.SCHEMA:
        failures.append("headless play-depth schema mismatch")
    if summary.get("status") != "passed":
        failures.append(f"headless play-depth proof did not pass: {summary.get('status')}")
    if summary.get("profile_coverage_passed") != summary.get("profile_coverage_cases"):
        failures.append(
            "headless play-depth proof did not satisfy expected profile coverage"
        )
    if "Headless Play-Depth Runs" not in markdown:
        failures.append("headless play-depth markdown title missing")

    required_note_outcomes: list[str] = []
    for case in notes.get("cases", []):
        if case.get("id") not in run_bot_playdepth_headless.DEFAULT_REQUIRED_CASES:
            continue
        required_note_outcomes.append(str(case.get("outcome", "")))
        if case.get("outcome") != "pending":
            failures.append(f"clean headless run must leave manual review pending: {case.get('id')}")
        if not case.get("repro_steps"):
            failures.append(f"headless note missing repro artifacts: {case.get('id')}")

    metrics = {
        "cases": int(summary.get("cases", 0) or 0),
        "passed": int(summary.get("passed", 0) or 0),
        "botlist_captures": int(summary.get("botlist_captures", 0) or 0),
        "profile_coverage_passed": int(summary.get("profile_coverage_passed", 0) or 0),
        "profile_coverage_cases": int(summary.get("profile_coverage_cases", 0) or 0),
        "pending_review_notes": sum(1 for outcome in required_note_outcomes if outcome == "pending"),
    }
    artifacts = {
        "tool": "tools/bot_playtest/run_bot_playdepth_headless.py",
        "json": ".tmp/bot_playtest/headless/<stamp>/bot_playdepth_headless_runs.json",
        "notes": ".tmp/bot_playtest/headless/<stamp>/bot_multiplayer_playtest_headless_notes.json",
    }
    if failures:
        return fail(
            "playdepth_headless_tooling",
            "Duel/CTF headless play-depth runner tooling is incomplete",
            failures,
            metrics=metrics,
            artifacts=artifacts,
        )
    return ok(
        "playdepth_headless_tooling",
        "Duel/CTF headless play-depth runner tooling is ready",
        metrics=metrics,
        artifacts=artifacts,
    )


def check_perf_tooling(repo_root: pathlib.Path) -> CheckResult:
    perf_dir = repo_root / "tools" / "bot_perf"
    failures: list[str] = []
    artifacts: dict[str, str] = {}

    for name in REQUIRED_PERF_BUDGETS:
        path = perf_dir / name
        artifacts[name] = rel(path, repo_root)
        if not path.is_file():
            failures.append(f"missing perf budget: {rel(path, repo_root)}")
            continue
        try:
            analyze_bot_perf.load_budget(path)
        except SystemExit as exc:
            failures.append(f"invalid perf budget {rel(path, repo_root)}: {exc}")

    for name in REQUIRED_VARIANCE_BUDGETS:
        path = perf_dir / name
        artifacts[name] = rel(path, repo_root)
        if not path.is_file():
            failures.append(f"missing perf variance budget: {rel(path, repo_root)}")
            continue
        try:
            analyze_bot_perf.load_variance_budget(path)
        except SystemExit as exc:
            failures.append(f"invalid perf variance budget {rel(path, repo_root)}: {exc}")

    readme = perf_dir / "README.md"
    artifacts["readme"] = rel(readme, repo_root)
    if not readme.is_file():
        failures.append(f"missing perf README: {rel(readme, repo_root)}")
    else:
        text = readme.read_text(encoding="utf-8", errors="replace")
        for token in ("--variance-budget", "source_counter_variance_budget.json"):
            if token not in text:
                failures.append(f"tools/bot_perf/README.md should mention {token}")

    metrics = {
        "budgets": len(REQUIRED_PERF_BUDGETS),
        "variance_budgets": len(REQUIRED_VARIANCE_BUDGETS),
    }
    if failures:
        return fail(
            "perf_tooling",
            "bot perf budgets and variance gate are incomplete",
            failures,
            metrics=metrics,
            artifacts=artifacts,
        )
    return ok(
        "perf_tooling",
        "bot perf per-run and repeated-soak variance budgets are valid",
        metrics=metrics,
        artifacts=artifacts,
    )


def scenario_rows_from_payload(payload: dict[str, Any]) -> list[dict[str, Any]]:
    rows = payload.get("scenarios", [])
    if isinstance(rows, list) and rows:
        return [row for row in rows if isinstance(row, dict)]
    catalog = payload.get("catalog", [])
    if isinstance(catalog, list):
        return [row for row in catalog if isinstance(row, dict)]
    return []


def scenario_names_from_payload(payload: dict[str, Any]) -> set[str]:
    return {
        str(row.get("name"))
        for row in scenario_rows_from_payload(payload)
        if row.get("name")
    }


def supplemental_scenario_names_from_payload(payload: dict[str, Any]) -> set[str]:
    names: set[str] = set()
    for row in scenario_rows_from_payload(payload):
        name = row.get("name")
        if not name:
            continue
        failures = row.get("failures")
        if isinstance(failures, list) and failures:
            continue
        if "returncode" in row:
            try:
                if int(row.get("returncode") or 0) != 0:
                    continue
            except (TypeError, ValueError):
                continue
        if "duration_budget_passed" in row and not bool(row.get("duration_budget_passed")):
            continue
        names.add(str(name))
    return names


def discover_supplemental_scenario_names(
    repo_root: pathlib.Path,
    primary_report: pathlib.Path,
    required_names: set[str],
) -> tuple[set[str], list[pathlib.Path]]:
    root = repo_root / ".tmp" / "bot_scenarios"
    if not root.is_dir() or not required_names:
        return set(), []

    names: set[str] = set()
    reports: list[pathlib.Path] = []
    primary = primary_report.resolve()
    for path in sorted(root.rglob("*.json")):
        if not path.is_file():
            continue
        try:
            if path.resolve() == primary:
                continue
        except OSError:
            continue
        try:
            payload = load_json(path)
        except (OSError, json.JSONDecodeError):
            continue
        found = supplemental_scenario_names_from_payload(payload)
        if not found:
            continue
        relevant = found & required_names
        if not relevant:
            continue
        names.update(relevant)
        reports.append(path)
        if required_names <= names:
            break
    return names, reports


def scenario_report_score(path: pathlib.Path) -> tuple[int, int, int, float]:
    try:
        payload = load_json(path)
    except (OSError, json.JSONDecodeError):
        return (-1, -1, -1, 0.0)
    summary = payload.get("summary", {})
    total = int(summary.get("total", len(payload.get("scenarios", [])) or len(payload.get("catalog", [])) or 0) or 0)
    passed = int(summary.get("passed", 0) or 0)
    failed = int(summary.get("failed", 0) or 0)
    mtime = path.stat().st_mtime
    return (passed, total, -failed, mtime)


def discover_scenario_report(repo_root: pathlib.Path) -> pathlib.Path | None:
    root = repo_root / ".tmp" / "bot_scenarios"
    if not root.is_dir():
        return None
    candidates = [path for path in root.rglob("*.json") if path.is_file()]
    scored = [(scenario_report_score(path), path) for path in candidates]
    scored = [item for item in scored if item[0][1] > 0]
    if not scored:
        return None
    return max(scored, key=lambda item: item[0])[1]


def check_scenario_report(
    repo_root: pathlib.Path,
    report_path: pathlib.Path | None,
    *,
    min_implemented_rows: int,
    allow_missing: bool,
) -> CheckResult:
    if report_path is None:
        report_path = discover_scenario_report(repo_root)
    if report_path is None:
        message = "no bot scenario report was found"
        details = ["expected a JSON report under .tmp/bot_scenarios or --scenario-report"]
        if allow_missing:
            return warn("scenario_evidence", message, details)
        return fail("scenario_evidence", message, details)

    try:
        payload = load_json(report_path)
    except (OSError, json.JSONDecodeError) as exc:
        return fail(
            "scenario_evidence",
            "bot scenario report could not be read",
            [str(exc)],
            artifacts={"scenario_report": rel(report_path, repo_root)},
        )

    summary = payload.get("summary", {})
    scenarios = scenario_rows_from_payload(payload)
    scenario_names = {row.get("name") for row in scenarios if row.get("name")}
    required_missing_from_primary = set(REQUIRED_SCENARIOS) - scenario_names
    supplemental_names, supplemental_reports = discover_supplemental_scenario_names(
        repo_root,
        report_path,
        required_missing_from_primary,
    )
    combined_scenario_names = scenario_names | supplemental_names
    missing_required = sorted(set(REQUIRED_SCENARIOS) - combined_scenario_names)
    total = int(summary.get("total", len(scenarios)) or 0)
    passed = int(summary.get("passed", 0) or 0)
    failed = int(summary.get("failed", 0) or 0)
    timeout = int(summary.get("timeout", 0) or 0)
    errors = int(summary.get("error", 0) or 0)
    pending = int(summary.get("pending", 0) or 0)
    overall = summary.get("overall", "")
    movement_rows = sum(1 for row in scenarios if "movement" in row.get("selection_tags", []))

    failures: list[str] = []
    if total < min_implemented_rows:
        failures.append(
            f"scenario report has {total} rows; expected at least {min_implemented_rows}"
        )
    if failed or timeout or errors or pending or overall not in ("pass", "passed"):
        failures.append(
            "scenario summary is not clean: "
            f"overall={overall!r} failed={failed} timeout={timeout} "
            f"error={errors} pending={pending}"
        )
    for name in missing_required:
        failures.append(f"missing required scenario evidence: {name}")

    metrics = {
        "total": total,
        "passed": passed,
        "failed": failed,
        "timeout": timeout,
        "error": errors,
        "pending": pending,
        "movement_rows": movement_rows,
        "required_scenarios": len(REQUIRED_SCENARIOS),
        "supplemental_reports": len(supplemental_reports),
    }
    artifacts = {"scenario_report": rel(report_path, repo_root)}
    if supplemental_reports:
        artifacts["supplemental_scenario_reports"] = str(len(supplemental_reports))
    if failures:
        return fail(
            "scenario_evidence",
            "bot scenario evidence does not satisfy release gate",
            failures,
            metrics=metrics,
            artifacts=artifacts,
        )
    return ok(
        "scenario_evidence",
        "bot scenario evidence satisfies release gate",
        metrics=metrics,
        artifacts=artifacts,
    )


def check_movement_reference_audit(repo_root: pathlib.Path, audit_path: pathlib.Path | None) -> CheckResult:
    if audit_path is None:
        audit_path = repo_root / ".tmp" / "bot_scenarios" / "movement_reference_gap_audit.json"

    if not audit_path.is_file():
        return fail(
            "movement_reference_audit",
            "movement reference audit is missing",
            [f"missing movement reference audit: {rel(audit_path, repo_root)}"],
            artifacts={"movement_reference_audit": rel(audit_path, repo_root)},
        )

    try:
        payload = load_json(audit_path)
    except (OSError, json.JSONDecodeError) as exc:
        return fail(
            "movement_reference_audit",
            "movement reference audit could not be read",
            [str(exc)],
            artifacts={"movement_reference_audit": rel(audit_path, repo_root)},
        )

    summary = payload.get("summary", {})
    checks = [
        check for check in payload.get("checks", [])
        if isinstance(check, dict) and check.get("id")
    ]
    by_id = {str(check.get("id")): check for check in checks}
    failures: list[str] = []

    if payload.get("status") != "accepted":
        failures.append(f"movement reference audit status is {payload.get('status')!r}; expected 'accepted'")
    if int(summary.get("blocked", 0) or 0) != 0:
        failures.append(f"movement reference audit still has blocked checks: {summary.get('blocked')}")

    for check_id, expected_scenario in REQUIRED_MOVEMENT_AUDIT_CHECKS.items():
        check = by_id.get(check_id)
        if check is None:
            failures.append(f"movement reference audit missing check: {check_id}")
            continue
        if check.get("status") != "accepted":
            failures.append(
                f"movement reference audit check {check_id} is {check.get('status')!r}; expected 'accepted'"
            )
        scenario = check.get("scenario", {})
        if not isinstance(scenario, dict) or scenario.get("name") != expected_scenario:
            failures.append(
                f"movement reference audit check {check_id} should use scenario {expected_scenario!r}"
            )

    metrics = {
        "checks": int(summary.get("check_count", len(checks)) or 0),
        "blocked": int(summary.get("blocked", 0) or 0),
        "accepted": int(summary.get("accepted", 0) or 0),
        "required_checks": len(REQUIRED_MOVEMENT_AUDIT_CHECKS),
    }
    artifacts = {"movement_reference_audit": rel(audit_path, repo_root)}
    if failures:
        return fail(
            "movement_reference_audit",
            "movement reference audit has unresolved blockers",
            failures,
            metrics=metrics,
            artifacts=artifacts,
        )
    return ok(
        "movement_reference_audit",
        "movement reference audit accepts promoted crouch and hazard rows",
        metrics=metrics,
        artifacts=artifacts,
    )


def run_acceptance(
    repo_root: pathlib.Path,
    *,
    install_dir: pathlib.Path | None = None,
    base_game: str = "basew",
    scenario_report: pathlib.Path | None = None,
    movement_reference_audit: pathlib.Path | None = None,
    min_implemented_rows: int = 114,
    allow_missing_scenario_report: bool = False,
) -> dict[str, Any]:
    repo_root = repo_root.resolve()
    install_dir = (install_dir or repo_root / ".install").resolve()
    if scenario_report is not None:
        scenario_report = scenario_report.resolve()
    if movement_reference_audit is not None:
        movement_reference_audit = movement_reference_audit.resolve()

    checks = [
        check_surface(repo_root),
        check_profiles(repo_root),
        check_bots_txt(repo_root),
        check_authored_botfiles(repo_root),
        check_staged_botfiles(repo_root, install_dir, base_game),
        check_staged_aas(repo_root, install_dir, base_game),
        check_user_docs(repo_root),
        check_playtest_plan(repo_root),
        check_playtest_triage(repo_root),
        check_playdepth_evidence_tooling(repo_root),
        check_playdepth_headless_tooling(repo_root),
        check_m3_multiplayer_gate_tooling(repo_root),
        check_perf_tooling(repo_root),
        check_scenario_report(
            repo_root,
            scenario_report,
            min_implemented_rows=min_implemented_rows,
            allow_missing=allow_missing_scenario_report,
        ),
        check_movement_reference_audit(repo_root, movement_reference_audit),
    ]
    failures = [check for check in checks if check.status == "fail"]
    warnings = [check for check in checks if check.status == "warn" or check.warnings]
    return {
        "schema_version": 1,
        "repo_root": str(repo_root),
        "install_dir": str(install_dir),
        "base_game": base_game,
        "summary": {
            "status": "failed" if failures else "passed",
            "checks": len(checks),
            "passed": sum(1 for check in checks if check.status == "pass"),
            "failed": len(failures),
            "warnings": len(warnings),
        },
        "checks": [check.to_json() for check in checks],
    }


def format_text(report: dict[str, Any]) -> str:
    summary = report["summary"]
    lines = [
        f"bot release acceptance: {summary['status']}",
        (
            f"checks={summary['checks']} passed={summary['passed']} "
            f"failed={summary['failed']} warnings={summary['warnings']}"
        ),
    ]
    for check in report["checks"]:
        lines.append("")
        lines.append(f"[{check['status']}] {check['name']}: {check['message']}")
        if check["metrics"]:
            metrics = ", ".join(f"{key}={value}" for key, value in check["metrics"].items())
            lines.append(f"  metrics: {metrics}")
        if check["artifacts"]:
            artifacts = ", ".join(f"{key}={value}" for key, value in check["artifacts"].items())
            lines.append(f"  artifacts: {artifacts}")
        for failure in check["failures"]:
            lines.append(f"  failure: {failure}")
        for warning in check["warnings"]:
            lines.append(f"  warning: {warning}")
    return "\n".join(lines) + "\n"


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=pathlib.Path, default=REPO_ROOT)
    parser.add_argument("--install-dir", type=pathlib.Path)
    parser.add_argument("--base-game", default="basew")
    parser.add_argument("--scenario-report", type=pathlib.Path)
    parser.add_argument("--movement-reference-audit", type=pathlib.Path)
    parser.add_argument("--min-implemented-rows", type=int, default=114)
    parser.add_argument(
        "--allow-missing-scenario-report",
        action="store_true",
        help="Warn instead of failing when no scenario report artifact exists.",
    )
    parser.add_argument("--format", choices=("text", "json"), default="text")
    parser.add_argument("--output", type=pathlib.Path)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    report = run_acceptance(
        args.repo_root,
        install_dir=args.install_dir,
        base_game=args.base_game,
        scenario_report=args.scenario_report,
        movement_reference_audit=args.movement_reference_audit,
        min_implemented_rows=args.min_implemented_rows,
        allow_missing_scenario_report=args.allow_missing_scenario_report,
    )
    output = (
        json.dumps(report, indent=2, sort_keys=True) + "\n"
        if args.format == "json"
        else format_text(report)
    )
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output, encoding="utf-8")
    else:
        print(output, end="")
    return 0 if report["summary"]["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
