#!/usr/bin/env python3
"""Source contract for the allocation-free WDM1 snapshot playback core."""

from __future__ import annotations

import argparse
from pathlib import Path


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


def require_order(source: str, *needles: str) -> None:
    positions = [source.index(needle) for needle in needles]
    assert positions == sorted(positions), needles
    assert len(set(positions)) == len(positions), needles


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, required=True)
    root = parser.parse_args().repo_root.resolve()
    header = (root / "inc/common/net/native_demo_playback.h").read_text(
        encoding="utf-8"
    )
    source = (root / "src/common/net/native_demo_playback.c").read_text(
        encoding="utf-8"
    )

    cursor_start = header.index(
        "typedef struct worr_native_demo_playback_cursor_v1_s"
    )
    cursor_end = header.index(
        "} worr_native_demo_playback_cursor_v1;", cursor_start
    )
    cursor = header[cursor_start:cursor_end]
    assert "*" not in cursor

    for forbidden in (
        "malloc(",
        "calloc(",
        "realloc(",
        "free(",
        "fopen(",
        "FS_",
        "q2proto",
        "src/client",
        "src/server",
    ):
        assert forbidden not in source, forbidden

    binding = function_body(source, "static worr_native_demo_playback_result_v1 revalidate_binding(")
    require_order(
        binding,
        "stream_fingerprint(&cursor->scan_config, encoded, encoded_bytes)",
        "Worr_NativeDemoStreamScanV1(",
        "entry_count_u64 != scan.record_count",
        "index_fingerprint(entries, entry_count)",
        "validate_index(&scan, entries, entry_count)",
    )

    decode = function_body(source, "static worr_native_demo_playback_result_v1 decode_selected(")
    require_order(
        decode,
        "Worr_NativeDemoRecordDecodeV1(",
        "record_matches_entry(&record, entry)",
        "Worr_NativeCodecSnapshotMetadataV1(",
        "metadata.snapshot.server_time_us != entry->time_us",
        "validate_output_regions(",
        "Worr_NativeCodecSnapshotDecodeProjectionV1(",
        "*cursor = next;",
        "*frame_out = frame;",
    )
    assert "cursor->next_entry_index" in source
    assert "cursor->reset_generation == UINT32_MAX" in source
    assert "WORR_NATIVE_DEMO_PLAYBACK_FRAME_RESET_REQUIRED" in source

    print("native demo playback source contract: pass")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
