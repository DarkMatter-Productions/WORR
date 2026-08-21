#!/usr/bin/env python3

from __future__ import annotations

import argparse
import datetime as dt
import os
import subprocess
from pathlib import Path


def run_git(*args: str) -> str:
    proc = subprocess.run(
        ["git", *args],
        check=True,
        capture_output=True,
        encoding="utf-8",
    )
    return proc.stdout.strip()


def write_github_output(path: Path, key: str, value: str) -> None:
    with path.open("a", encoding="utf-8", newline="\n") as handle:
        handle.write(f"{key}={value}\n")


def parse_utc_timestamp(value: str) -> dt.datetime:
    normalized = value[:-1] + "+00:00" if value.endswith("Z") else value
    try:
        timestamp = dt.datetime.fromisoformat(normalized)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(
            "UTC timestamp must be an RFC 3339 date-time"
        ) from exc
    if timestamp.tzinfo is None:
        raise argparse.ArgumentTypeError("UTC timestamp must include a timezone")
    return timestamp.astimezone(dt.timezone.utc)


def main() -> int:
    parser = argparse.ArgumentParser(description="Prepare nightly metadata for GitHub Actions.")
    parser.add_argument("--github-output", help="Path to GITHUB_OUTPUT file")
    parser.add_argument("--force", action="store_true", help="Force nightly release even without commits today")
    time_source = parser.add_mutually_exclusive_group()
    time_source.add_argument(
        "--utc-timestamp",
        type=parse_utc_timestamp,
        help="Workflow-run creation timestamp (RFC 3339); stable across reruns",
    )
    time_source.add_argument(
        "--utc-date", help="Override UTC date (YYYY-MM-DD) for testing"
    )
    args = parser.parse_args()

    if args.utc_timestamp:
        now = args.utc_timestamp
    elif args.utc_date:
        now = dt.datetime.strptime(args.utc_date, "%Y-%m-%d").replace(tzinfo=dt.timezone.utc)
    else:
        now = dt.datetime.now(dt.timezone.utc)

    day_start = now.replace(hour=0, minute=0, second=0, microsecond=0)
    day_start_iso = day_start.strftime("%Y-%m-%dT%H:%M:%SZ")

    commits_today = int(run_git("rev-list", "--count", f"--since={day_start_iso}", "HEAD") or "0")
    should_run = args.force or commits_today > 0

    nightly_date = now.strftime("%Y%m%d")
    nightly_label = now.strftime("%Y-%m-%d")
    nightly_tag = f"nightly-{nightly_date}"
    nightly_name = f"WORR Nightly {nightly_label}"
    head_sha = run_git("rev-parse", "HEAD")

    outputs = {
        "should_run": "true" if should_run else "false",
        "nightly_date": nightly_date,
        "nightly_tag": nightly_tag,
        "nightly_name": nightly_name,
        "since_utc": day_start_iso,
        "commits_today": str(commits_today),
        "head_sha": head_sha,
    }

    github_output = args.github_output or os.environ.get("GITHUB_OUTPUT")
    if github_output:
        output_path = Path(github_output)
        for key, value in outputs.items():
            write_github_output(output_path, key, value)
    else:
        for key, value in outputs.items():
            print(f"{key}={value}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
