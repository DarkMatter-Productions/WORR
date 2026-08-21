#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import pathlib
import shutil
import stat


SECRET_DIRECTORY_PREFIX = "worr-release-signing-"


def validate_secret_directory(
    secret_directory: pathlib.Path, temp_root: pathlib.Path
) -> pathlib.Path:
    try:
        resolved_temp_root = temp_root.resolve(strict=True)
        temp_status = resolved_temp_root.stat()
        secret_status = secret_directory.lstat()
    except OSError as exc:
        raise SystemExit(f"Cannot inspect release-signing cleanup path ({exc})") from exc

    reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
    if not stat.S_ISDIR(temp_status.st_mode):
        raise SystemExit("Runner temporary root is not a directory")
    if (
        not stat.S_ISDIR(secret_status.st_mode)
        or stat.S_ISLNK(secret_status.st_mode)
        or getattr(secret_status, "st_file_attributes", 0) & reparse_flag
    ):
        raise SystemExit("Release-signing secret path is not a regular directory")
    if not secret_directory.name.startswith(SECRET_DIRECTORY_PREFIX):
        raise SystemExit("Refusing to remove a directory without the signing-secret prefix")
    if secret_directory.parent.resolve(strict=True) != resolved_temp_root:
        raise SystemExit("Release-signing secret directory is outside RUNNER_TEMP")
    return secret_directory


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Remove the private temporary directory used for release signing."
    )
    parser.add_argument(
        "--secret-dir",
        default=os.environ.get("WORR_RELEASE_SIGNING_SECRET_DIR", ""),
    )
    parser.add_argument(
        "--temp-root", default=os.environ.get("RUNNER_TEMP", "")
    )
    args = parser.parse_args()
    if not args.secret_dir or not args.temp_root:
        # An earlier materialization step may have failed before exporting a path.
        return 0

    secret_directory = validate_secret_directory(
        pathlib.Path(args.secret_dir).absolute(), pathlib.Path(args.temp_root)
    )
    shutil.rmtree(secret_directory)
    if secret_directory.exists():
        raise SystemExit("Release-signing secret directory still exists after cleanup")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
