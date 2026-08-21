"""Rollback-capable publication for fully staged directory trees."""

from __future__ import annotations

import os
import pathlib
import shutil
import stat
import tempfile
import time
from typing import Any, Callable


WINDOWS_TRANSIENT_REPLACE_ERRORS = frozenset({5, 32, 33})
REPLACE_RETRY_DELAYS_SECONDS = (0.05, 0.1, 0.2, 0.4, 0.8, 1.0, 1.0, 1.0)


def replace_path_with_transient_retry(
    source: pathlib.Path,
    destination: pathlib.Path,
) -> None:
    """Replace one path, tolerating bounded Windows scanner/share races."""

    for retry_index in range(len(REPLACE_RETRY_DELAYS_SECONDS) + 1):
        try:
            os.replace(source, destination)
            return
        except OSError as error:
            if (
                os.name != "nt"
                or getattr(error, "winerror", None)
                not in WINDOWS_TRANSIENT_REPLACE_ERRORS
                or retry_index == len(REPLACE_RETRY_DELAYS_SECONDS)
            ):
                raise
            time.sleep(REPLACE_RETRY_DELAYS_SECONDS[retry_index])


def remove_tree_with_readonly_retry(path: pathlib.Path) -> None:
    """Remove a private staging tree, clearing Windows read-only attributes."""

    def retry_readonly(
        function: Callable[[str], Any], name: str, error: object
    ) -> None:
        del error
        os.chmod(name, stat.S_IRWXU)
        function(name)

    shutil.rmtree(path, onerror=retry_readonly)


def publish_staged_directory_atomically(
    staged_dir: pathlib.Path,
    output_dir: pathlib.Path,
) -> None:
    """Replace a tree as one directory unit and restore it on publish failure."""
    if not staged_dir.is_dir() or staged_dir.is_symlink():
        raise SystemExit(f"Staged tree is not a directory: {staged_dir}")
    if output_dir.is_symlink():
        raise SystemExit(f"Refusing to replace symbolic-link output directory: {output_dir}")
    if output_dir.exists() and not output_dir.is_dir():
        raise SystemExit(f"Output path is not a directory: {output_dir}")

    backup_dir: pathlib.Path | None = None
    committed = False
    try:
        if output_dir.exists():
            backup_dir = pathlib.Path(
                tempfile.mkdtemp(
                    prefix=f".{output_dir.name}.backup.",
                    dir=output_dir.parent,
                )
            )
            backup_dir.rmdir()
            replace_path_with_transient_retry(output_dir, backup_dir)

        try:
            replace_path_with_transient_retry(staged_dir, output_dir)
            committed = True
        except BaseException as publish_error:
            if backup_dir is not None:
                try:
                    replace_path_with_transient_retry(backup_dir, output_dir)
                    backup_dir = None
                except OSError as rollback_error:
                    raise RuntimeError(
                        "Directory publication failed and rollback was incomplete; "
                        f"recover the old tree from {backup_dir}: {rollback_error}"
                    ) from publish_error
            raise
    finally:
        if committed and backup_dir is not None:
            remove_tree_with_readonly_retry(backup_dir)
