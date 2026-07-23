#!/usr/bin/env python3
"""Production ordering contract for native state at a server map boundary."""

from __future__ import annotations

import argparse
from pathlib import Path


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening_brace = source.index("{", start)
    depth = 0
    for index in range(opening_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"unterminated function body for {signature}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, required=True)
    args = parser.parse_args()
    root = args.repo_root.resolve()

    init = (root / "src/server/init.c").read_text(encoding="utf-8")
    reset = function_body(init, "void SV_ClientReset(client_t *client)")

    quiesce = "SV_NativeShadowPeerQuiesceMapV1("
    assert reset.count(quiesce) == 1
    quiesce_position = reset.index(quiesce)

    # The native peer and netchan survive gamemap, while both authoritative
    # legacy command-state objects are map-local.  The cancellation floor must
    # therefore be published before either object's initialized latch or
    # storage can be cleared.
    cleared_state = (
        "client->worr_command_parser_initialized = false;",
        "client->worr_command_stream_initialized = false;",
        "memset(&client->worr_command_parser, 0,",
        "memset(&client->worr_command_stream, 0,",
    )
    for marker in cleared_state:
        assert quiesce_position < reset.index(marker), (
            f"native map quiesce must precede command-state clear: {marker}"
        )

    print(
        "server_native_map_quiesce_source_contract "
        "client_reset=quiesce-before-command-parser-and-stream-clear"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
