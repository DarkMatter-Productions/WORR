#!/usr/bin/env python3

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import pathlib
import subprocess


def run_git(*args: str) -> str:
    proc = subprocess.run(
        ["git", *args],
        check=True,
        capture_output=True,
        encoding="utf-8",
    )
    return proc.stdout.strip()


def find_previous_tag(channel: str, current_tag: str) -> str:
    if channel == "nightly":
        pattern = "nightly-*"
    else:
        pattern = "v*"

    tags = run_git("tag", "--list", pattern, "--sort=-creatordate").splitlines()
    for tag in tags:
        if tag and tag != current_tag:
            return tag
    return ""


def changelog_lines(range_spec: str) -> list[str]:
    text = run_git("log", "--date=short", "--pretty=format:%h%x09%s%x09%an%x09%ad", range_spec)
    if not text:
        return []
    lines = []
    for row in text.splitlines():
        parts = row.split("\t")
        if len(parts) != 4:
            continue
        sha, subject, author, date = parts
        lines.append(f"- `{sha}` {subject} ({author}, {date})")
    return lines


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate nightly/stable release notes with full changelog.")
    parser.add_argument("--repo", required=True, help="GitHub owner/repo")
    parser.add_argument("--channel", required=True, help="Release channel")
    parser.add_argument("--version", required=True, help="Release version")
    parser.add_argument("--tag", required=True, help="Release tag")
    parser.add_argument("--commit-sha", required=True, help="Release commit")
    parser.add_argument("--run-id", default="", help="Workflow run id")
    parser.add_argument("--run-url", default="", help="Workflow run URL")
    parser.add_argument("--index-path", required=True, help="Release index JSON path")
    parser.add_argument(
        "--trust-policy-path",
        help="Published release trust policy whose fingerprint is shown in the notes",
    )
    parser.add_argument("--output", required=True, help="Markdown output path")
    args = parser.parse_args()

    index = json.loads(pathlib.Path(args.index_path).read_text(encoding="utf-8"))
    previous_tag = find_previous_tag(args.channel, args.tag)
    range_spec = f"{previous_tag}..HEAD" if previous_tag else "HEAD"
    changelog = changelog_lines(range_spec)
    now = dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%d %H:%M:%SZ")

    lines: list[str] = []
    lines.append(f"# {args.channel.capitalize()} Release {args.version}")
    lines.append("")
    lines.append("## Build Metadata")
    lines.append(f"- Channel: `{args.channel}`")
    lines.append(f"- Version: `{args.version}`")
    lines.append(f"- Tag: `{args.tag}`")
    lines.append(f"- Commit: `{args.commit_sha}`")
    lines.append(f"- Generated: `{now}`")
    if args.run_url:
        run_label = args.run_id if args.run_id else "workflow"
        lines.append(f"- Workflow: [{run_label}]({args.run_url})")
    if previous_tag:
        compare_url = f"https://github.com/{args.repo}/compare/{previous_tag}...{args.tag}"
        lines.append(f"- Compare: [{previous_tag}...{args.tag}]({compare_url})")
    if args.trust_policy_path:
        trust_policy_path = pathlib.Path(args.trust_policy_path)
        trust_policy_payload = trust_policy_path.read_bytes()
        trust_policy = json.loads(trust_policy_payload.decode("utf-8", errors="strict"))
        lines.append("")
        lines.append("## Release Trust Policy")
        lines.append(f"- Asset: `{trust_policy_path.name}`")
        lines.append(
            "- SHA-256: `"
            + hashlib.sha256(trust_policy_payload).hexdigest()
            + "`"
        )
        lines.append(f"- Signature threshold: `{trust_policy.get('threshold', 'unknown')}`")
        key_ids = sorted(
            key.get("key_id", "")
            for key in trust_policy.get("keys", [])
            if isinstance(key, dict) and key.get("key_id")
        )
        if key_ids:
            lines.append("- Authorized key IDs: " + ", ".join(f"`{key_id}`" for key_id in key_ids))
    lines.append("")
    lines.append("## Full Changelog")
    if changelog:
        lines.extend(changelog)
    else:
        lines.append("- No commits detected in the selected range.")
    lines.append("")
    lines.append("## Platform Assets")
    for target in index.get("targets", []):
        lines.append(f"### {target['platform_id']}")
        lines.append(f"- Autoupdater mode: `{target.get('autoupdater', {}).get('mode', 'unknown')}`")
        for item in target.get("files", []):
            lines.append(f"- `{item['name']}` ({item['kind']}, {item['size']} bytes)")
        lines.append("")

    output = pathlib.Path(args.output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")
    print(f"Wrote {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
