#!/usr/bin/env python3
"""Build the complete vendored q2proto compile/link compatibility suite."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shutil
import signal
import stat
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Mapping, Sequence

try:
    from tools.networking import headless_process
except ModuleNotFoundError:  # Direct execution adds tools/, not the repo root.
    from networking import headless_process  # type: ignore[no-redef]


EXPECTED_TARGETS = (
    "build_vanilla",
    "build_vanilla_deflate",
    "build_q2pro_ext",
    "build_q2pro_ext_deflate",
    "build_q2pro_ext_v2",
    "build_q2pro_ext_v2_deflate",
    "build_q2repro",
    "build_q2repro_deflate",
    "build_single_source",
)


class CompatibilityError(RuntimeError):
    """Raised when the compatibility gate cannot prove a complete build."""


def _is_within(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
    except ValueError:
        return False
    return True


def _paths_overlap(first: Path, second: Path) -> bool:
    return _is_within(first, second) or _is_within(second, first)


def _display_path(path: Path) -> str:
    try:
        return path.relative_to(Path.cwd().resolve()).as_posix()
    except ValueError:
        return path.as_posix()


def _hash_file(path: Path) -> tuple[str, int]:
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as handle:
        while chunk := handle.read(1024 * 1024):
            digest.update(chunk)
            size += len(chunk)
    return digest.hexdigest(), size


def _snapshot_tree(root: Path) -> dict[str, Any]:
    """Return a deterministic content fingerprint without following links."""

    root = root.resolve()
    if not root.is_dir():
        raise CompatibilityError(f"vendor directory is missing: {root}")

    digest = hashlib.sha256()
    regular_files = 0
    total_bytes = 0
    entries = 0

    for directory, dirnames, filenames in os.walk(root, followlinks=False):
        directory_path = Path(directory)
        dirnames.sort()
        filenames.sort()

        retained_dirs: list[str] = []
        for name in dirnames:
            path = directory_path / name
            relative = path.relative_to(root).as_posix()
            if path.is_symlink():
                target = os.readlink(path)
                digest.update(f"L\0{relative}\0{target}\0".encode("utf-8"))
            else:
                digest.update(f"D\0{relative}\0".encode("utf-8"))
                retained_dirs.append(name)
            entries += 1
        dirnames[:] = retained_dirs

        for name in filenames:
            path = directory_path / name
            relative = path.relative_to(root).as_posix()
            metadata = path.stat(follow_symlinks=False)
            if stat.S_ISLNK(metadata.st_mode):
                target = os.readlink(path)
                digest.update(f"L\0{relative}\0{target}\0".encode("utf-8"))
            elif stat.S_ISREG(metadata.st_mode):
                file_digest, size = _hash_file(path)
                digest.update(
                    f"F\0{relative}\0{size}\0{file_digest}\0".encode("utf-8")
                )
                regular_files += 1
                total_bytes += size
            else:
                raise CompatibilityError(
                    f"unsupported special entry in vendor tree: {path}"
                )
            entries += 1

    return {
        "sha256": digest.hexdigest(),
        "entries": entries,
        "regular_files": regular_files,
        "bytes": total_bytes,
    }


def _configure_command(meson: str, source_dir: Path, build_dir: Path) -> list[str]:
    return [
        meson,
        "setup",
        "--fatal-meson-warnings",
        "--wrap-mode=nodownload",
        "--buildtype=debugoptimized",
        "-Dwerror=true",
        str(build_dir),
        str(source_dir),
    ]


def _introspect_command(meson: str, build_dir: Path) -> list[str]:
    return [meson, "introspect", "--targets", str(build_dir)]


def _compile_command(
    meson: str, build_dir: Path, jobs: int, targets: Sequence[str]
) -> list[str]:
    return [
        meson,
        "compile",
        "-C",
        str(build_dir),
        "--jobs",
        str(jobs),
        *targets,
    ]


def _start_process(
    command: Sequence[str], env: Mapping[str, str]
) -> subprocess.Popen[str]:
    kwargs: dict[str, Any] = {
        "stdin": subprocess.DEVNULL,
        "stdout": subprocess.PIPE,
        "stderr": subprocess.STDOUT,
        "text": True,
        "encoding": "utf-8",
        "errors": "replace",
        "env": dict(env),
    }
    if os.name == "nt":
        return headless_process.start_headless_process(list(command), **kwargs)
    return subprocess.Popen(list(command), start_new_session=True, **kwargs)


def _posix_group_exists(process_group: int) -> bool:
    try:
        os.killpg(process_group, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def _terminate_posix_process_tree(process: subprocess.Popen[str]) -> None:
    """Terminate the dedicated session, including descendants of an exited root."""

    process_group = process.pid
    try:
        os.killpg(process_group, signal.SIGTERM)
    except ProcessLookupError:
        pass

    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        pass

    if _posix_group_exists(process_group):
        try:
            os.killpg(process_group, signal.SIGKILL)
        except ProcessLookupError:
            pass

    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired as exc:
        raise CompatibilityError(
            f"process group {process_group} did not terminate"
        ) from exc
    cleanup_deadline = time.monotonic() + 5
    while _posix_group_exists(process_group):
        if time.monotonic() >= cleanup_deadline:
            raise CompatibilityError(
                f"process group {process_group} still exists after forced cleanup"
            )
        time.sleep(0.05)


def _terminate_process_tree(process: subprocess.Popen[str]) -> None:
    if os.name == "nt":
        headless_process.terminate_process_tree(process)
        if process.poll() is None:
            raise CompatibilityError(
                f"Windows process tree {process.pid} did not terminate"
            )
        return
    _terminate_posix_process_tree(process)


def _run_command(
    name: str,
    command: Sequence[str],
    results_dir: Path,
    timeout_seconds: float,
    *,
    echo_output: bool,
    env: Mapping[str, str],
) -> tuple[dict[str, Any], str]:
    started = time.monotonic()
    timed_out = False
    returncode: int | None = None
    output = ""
    error: str | None = None

    process: subprocess.Popen[str] | None = None
    try:
        process = _start_process(command, env)
        try:
            output, _ = process.communicate(timeout=max(timeout_seconds, 0.001))
            returncode = process.returncode
        except subprocess.TimeoutExpired:
            timed_out = True
            error = "command exceeded the shared compatibility timeout"
            _terminate_process_tree(process)
            output, _ = process.communicate()
            returncode = process.returncode
    except OSError as exc:
        error = f"unable to launch command: {exc}"
    except CompatibilityError as exc:
        error = f"unable to contain command process tree: {exc}"
    finally:
        if process is not None and not timed_out:
            try:
                _terminate_process_tree(process)
            except CompatibilityError as exc:
                error = f"unable to contain command process tree: {exc}"
            returncode = process.returncode
    if error and not timed_out and returncode == 0:
        returncode = -1

    duration = time.monotonic() - started
    if error:
        if output and not output.endswith("\n"):
            output += "\n"
        output += f"ERROR: {error}\n"
    (results_dir / f"{name}.log").write_text(output, encoding="utf-8")

    if echo_output and output:
        print(output, end="" if output.endswith("\n") else "\n", flush=True)

    step = {
        "name": name,
        "command": list(command),
        "returncode": returncode,
        "timed_out": timed_out,
        "duration_seconds": round(duration, 3),
        "log": f"{name}.log",
    }
    if error:
        step["error"] = error
    return step, output


def _validate_targets(
    raw_targets: Any, build_dir: Path
) -> tuple[list[str], dict[str, dict[str, Any]]]:
    if not isinstance(raw_targets, list):
        raise CompatibilityError("Meson target introspection did not return a list")

    by_name: dict[str, dict[str, Any]] = {}
    executable_names: list[str] = []
    for target in raw_targets:
        if not isinstance(target, dict):
            raise CompatibilityError("Meson target introspection contains a non-object")
        name = target.get("name")
        if not isinstance(name, str) or not name:
            raise CompatibilityError("Meson target introspection contains an invalid name")
        if name in by_name:
            raise CompatibilityError(f"Meson target introspection duplicates {name!r}")
        by_name[name] = target
        if target.get("type") == "executable":
            executable_names.append(name)

    expected = set(EXPECTED_TARGETS)
    observed = set(executable_names)
    missing = sorted(expected - observed)
    unexpected = sorted(observed - expected)
    if missing or unexpected:
        raise CompatibilityError(
            "q2proto compatibility target inventory changed: "
            f"missing={missing}, unexpected={unexpected}"
        )

    resolved_build = build_dir.resolve()
    for name in EXPECTED_TARGETS:
        target = by_name[name]
        if target.get("type") != "executable":
            raise CompatibilityError(f"compatibility target {name!r} is not executable")
        if target.get("build_by_default") is not True:
            raise CompatibilityError(
                f"compatibility target {name!r} is not built by default"
            )
        filenames = target.get("filename")
        if not isinstance(filenames, list) or not filenames:
            raise CompatibilityError(
                f"compatibility target {name!r} has no output filename"
            )
        for filename in filenames:
            if not isinstance(filename, str) or not filename:
                raise CompatibilityError(
                    f"compatibility target {name!r} has an invalid output filename"
                )
            if not _is_within(Path(filename).resolve(), resolved_build):
                raise CompatibilityError(
                    f"compatibility target {name!r} escapes the build directory"
                )

    return list(EXPECTED_TARGETS), by_name


def _collect_products(
    targets: Mapping[str, Mapping[str, Any]], build_dir: Path
) -> list[dict[str, Any]]:
    products: list[dict[str, Any]] = []
    resolved_build = build_dir.resolve()
    for name in EXPECTED_TARGETS:
        for raw_filename in targets[name]["filename"]:
            filename = Path(str(raw_filename)).resolve()
            if not _is_within(filename, resolved_build) or not filename.is_file():
                raise CompatibilityError(
                    f"compatibility target {name!r} did not produce {filename}"
                )
            digest, size = _hash_file(filename)
            products.append(
                {
                    "target": name,
                    "path": filename.relative_to(resolved_build).as_posix(),
                    "size": size,
                    "sha256": digest,
                }
            )
    return products


def _write_summary(report: Mapping[str, Any], results_dir: Path) -> None:
    status = str(report.get("status", "failed")).upper()
    lines = [
        "# q2proto compatibility gate",
        "",
        f"- Status: **{status}**",
        f"- Expected targets: {len(EXPECTED_TARGETS)}",
        f"- Vendor SHA-256: `{report.get('vendor_snapshot_after', {}).get('sha256', 'unavailable')}`",
    ]
    failure = report.get("failure")
    if failure:
        lines.append(f"- Failure: `{failure}`")
    lines.append("")
    (results_dir / "summary.md").write_text("\n".join(lines), encoding="utf-8")


def _run(args: argparse.Namespace) -> int:
    vendor_dir = args.vendor_dir.resolve()
    source_dir = args.source_dir.resolve()
    build_dir = args.build_dir.resolve()
    results_dir = args.results_dir.resolve()

    if not _is_within(source_dir, vendor_dir):
        raise CompatibilityError("--source-dir must be inside --vendor-dir")
    if _is_within(build_dir, vendor_dir):
        raise CompatibilityError("--build-dir must be outside the read-only vendor tree")
    if _is_within(results_dir, vendor_dir):
        raise CompatibilityError(
            "--results-dir must be outside the read-only vendor tree"
        )
    if _paths_overlap(build_dir, results_dir):
        raise CompatibilityError("build and result directories must not overlap")
    if not (source_dir / "meson.build").is_file():
        raise CompatibilityError(f"q2proto Meson suite is missing: {source_dir}")
    if build_dir.exists() and any(build_dir.iterdir()):
        raise CompatibilityError(f"build directory must start empty: {build_dir}")
    if results_dir.exists() and any(results_dir.iterdir()):
        raise CompatibilityError(f"results directory must start empty: {results_dir}")

    build_dir.mkdir(parents=True, exist_ok=True)
    results_dir.mkdir(parents=True, exist_ok=True)

    report: dict[str, Any] = {
        "schema_version": 1,
        "status": "failed",
        "vendor_dir": _display_path(vendor_dir),
        "source_dir": _display_path(source_dir),
        "build_dir": _display_path(build_dir),
        "results_dir": _display_path(results_dir),
        "timeout_seconds": args.timeout_seconds,
        "jobs": args.jobs,
        "expected_targets": list(EXPECTED_TARGETS),
        "platform": platform.platform(),
        "python_version": platform.python_version(),
        "requested_c_compiler": os.environ.get("CC", ""),
        "steps": [],
    }
    deadline = time.monotonic() + args.timeout_seconds
    environment = os.environ.copy()
    environment.setdefault("NO_COLOR", "1")
    before: dict[str, Any] | None = None
    failure: str | None = None

    def run_step(
        name: str, command: Sequence[str], *, echo_output: bool
    ) -> tuple[dict[str, Any], str]:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise CompatibilityError("shared compatibility timeout expired")
        step, output = _run_command(
            name,
            command,
            results_dir,
            remaining,
            echo_output=echo_output,
            env=environment,
        )
        report["steps"].append(step)
        if step["returncode"] != 0 or step["timed_out"]:
            raise CompatibilityError(f"{name} step failed; see {name}.log")
        return step, output

    try:
        before = _snapshot_tree(vendor_dir)
        report["vendor_snapshot_before"] = before

        run_step(
            "configure",
            _configure_command(args.meson, source_dir, build_dir),
            echo_output=True,
        )
        _, introspection_output = run_step(
            "introspect",
            _introspect_command(args.meson, build_dir),
            echo_output=False,
        )
        try:
            raw_targets = json.loads(introspection_output)
        except json.JSONDecodeError as exc:
            raise CompatibilityError(
                f"Meson target introspection is not valid JSON: {exc}"
            ) from exc
        observed_targets, targets_by_name = _validate_targets(raw_targets, build_dir)
        report["observed_targets"] = observed_targets
        (results_dir / "targets.json").write_text(
            json.dumps(raw_targets, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

        run_step(
            "compile",
            _compile_command(
                args.meson, build_dir, args.jobs, observed_targets
            ),
            echo_output=True,
        )
        report["products"] = _collect_products(targets_by_name, build_dir)
        report["status"] = "passed"
    except (CompatibilityError, OSError, ValueError) as exc:
        failure = str(exc)
    finally:
        try:
            after = _snapshot_tree(vendor_dir)
            report["vendor_snapshot_after"] = after
            if before is not None and before != after:
                report["status"] = "failed"
                immutability_error = "q2proto vendor tree changed while the gate ran"
                failure = (
                    f"{failure}; {immutability_error}" if failure else immutability_error
                )
        except (CompatibilityError, OSError) as exc:
            report["status"] = "failed"
            snapshot_error = f"unable to verify vendor immutability: {exc}"
            failure = f"{failure}; {snapshot_error}" if failure else snapshot_error

        meson_log = build_dir / "meson-logs" / "meson-log.txt"
        if meson_log.is_file():
            shutil.copyfile(meson_log, results_dir / "meson-log.txt")
        if failure:
            report["status"] = "failed"
            report["failure"] = failure
        report["duration_seconds"] = round(
            args.timeout_seconds - max(deadline - time.monotonic(), 0.0), 3
        )
        (results_dir / "report.json").write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        _write_summary(report, results_dir)

    if report["status"] == "passed":
        print(
            f"q2proto compatibility gate passed: {len(EXPECTED_TARGETS)} targets",
            flush=True,
        )
        return 0
    print(f"q2proto compatibility gate failed: {failure}", file=sys.stderr)
    return 1


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Configure and build every vendored q2proto compatibility target, "
            "verify the target inventory, and write uploadable result artifacts."
        )
    )
    parser.add_argument("--vendor-dir", type=Path, default=Path("q2proto"))
    parser.add_argument("--source-dir", type=Path, default=Path("q2proto/tests"))
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--results-dir", type=Path, required=True)
    parser.add_argument("--meson", default="meson")
    parser.add_argument("--timeout-seconds", type=int, default=300)
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args(argv)

    if not 1 <= args.timeout_seconds <= 1800:
        parser.error("--timeout-seconds must be between 1 and 1800")
    if not 1 <= args.jobs <= 32:
        parser.error("--jobs must be between 1 and 32")

    try:
        return _run(args)
    except CompatibilityError as exc:
        print(f"q2proto compatibility gate failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
