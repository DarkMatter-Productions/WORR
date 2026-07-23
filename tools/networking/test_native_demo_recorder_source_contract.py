#!/usr/bin/env python3
"""Source/lifecycle contract for the bounded production WDM1 recorder."""

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

    shadow = (root / "src/client/snapshot_shadow.cpp").read_text(
        encoding="utf-8"
    )
    parse = (root / "src/client/parse.cpp").read_text(encoding="utf-8")
    demo = (root / "src/client/demo.cpp").read_text(encoding="utf-8")
    recorder = (root / "src/client/native_demo_recorder.cpp").read_text(
        encoding="utf-8"
    )

    accept = function_body(
        shadow, 'extern "C" bool CL_SnapshotShadowAcceptFrameEx('
    )
    require_order(
        accept,
        "record_native_expectation(view, hashes, ref, promotion_eligible);",
        "snapshot_record_observer(&view, &hashes, ref);",
        "snapshot_consumer->ConsumeCanonicalSnapshot(",
    )
    promote = function_body(
        shadow, 'extern "C" bool CL_SnapshotShadowPromoteLatestFrame('
    )
    require_order(
        promote,
        "record_native_expectation(",
        "snapshot_record_observer(&view, &hashes, shadow.latest_ref);",
        "snapshot_consumer->ConsumeCanonicalSnapshot(",
    )

    serverdata = function_body(parse, "static void CL_ParseServerData(")
    require_order(
        serverdata,
        "CL_NativeDemoRecorderMapBoundary();",
        "CL_NativeReadinessPilotServerDataReset();",
        "CL_ClearState();",
    )
    cleanup = function_body(demo, "void CL_CleanupDemos(void)")
    require_order(
        cleanup,
        "CL_NativeDemoRecorderCleanup();",
        "if (cls.demo.recording)",
        "if (cls.demo.playback)",
    )

    for required in (
        "FS_MODE_WRITE | FS_FLAG_EXCL",
        "path_has_parent_component_or_root(input)",
        "FS_ValidatePath(normalized) == PATH_INVALID",
        "FS_FileExists(recorder.final_path)",
        '".wdm.tmp"',
        "Worr_NativeDemoStreamScanV1(",
        "FS_RenameFile(recorder.temp_path, recorder.final_path)",
        "WORR_NET_CAP_NATIVE_SNAPSHOT_PUBLIC_MASK",
        "CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_AVAILABLE",
    ):
        assert required in recorder, required
    for forbidden in (
        "CL_WriteDemoMessage",
        '".dm2"',
        "q2proto_init_servercontext_demo",
        "CL_GTV_WriteMessage",
        "SV_MvdEndFrame",
    ):
        assert forbidden not in recorder, forbidden

    print("native demo recorder source contract: pass")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
