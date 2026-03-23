#!/usr/bin/env python3
import os
import subprocess
import sys


def strip_thin_flags(argv):
    removed_thin = False
    filtered = [arg for arg in argv if arg not in ("-T", "--thin")]
    removed_thin = len(filtered) != len(argv)
    if not filtered:
        return filtered, removed_thin
    flags = filtered[0]
    if flags and not flags.startswith("-") and "T" in flags:
        filtered[0] = flags.replace("T", "")
        removed_thin = True
        if not filtered[0]:
            filtered = filtered[1:]
    elif flags.startswith("-") and flags[1:].isalpha() and "T" in flags:
        filtered[0] = flags.replace("T", "")
        removed_thin = True
        if filtered[0] == "-":
            filtered = filtered[1:]
    return filtered, removed_thin


def maybe_delete_existing_archive(argv, removed_thin):
    if not removed_thin or len(argv) < 2:
        return
    archive_path = argv[1]
    if archive_path.startswith("@"):
        return
    if os.path.exists(archive_path):
        os.remove(archive_path)


def main():
    args, removed_thin = strip_thin_flags(sys.argv[1:])
    maybe_delete_existing_archive(args, removed_thin)
    llvm_ar = os.environ.get("LLVM_AR", "llvm-ar")
    return subprocess.call([llvm_ar] + args)


if __name__ == "__main__":
    raise SystemExit(main())
