#!/usr/bin/env python3
"""Generate the deterministic multi-joint GPU-MD5 renderer-parity map."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from generate_bmodel_first_frame_fixture import build_bsp


MAP_NAME = "worr_fr01_model_md5_multijoint.bsp"
# The registered rerelease player replacement has 17 joints and 198 frames.
# A non-zero source frame exercises its normal joint-palette resolve rather
# than relying only on the existing one-joint static dmspot lane.
MODEL_ENTITY = (
    '{\n'
    '"classname" "misc_model"\n'
    '"model" "players/male/tris.md2"\n'
    '"origin" "256 -128 -22"\n'
    '"scale" "1"\n'
    '"angle" "180"\n'
    '"frame" "100"\n'
    '}\n',
)

BACKGROUND_TEXTURE = "parity/fr01_to_background"


def generated_outputs(asset_root: Path) -> dict[Path, bytes]:
    return {
        asset_root / "maps" / MAP_NAME: build_bsp(
            extra_entities=MODEL_ENTITY,
            background_texture=BACKGROUND_TEXTURE,
        ),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--asset-root", type=Path, default=Path("assets"))
    parser.add_argument("--validate", action="store_true")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    outputs = generated_outputs(args.asset_root)
    mismatches = [
        str(path)
        for path, expected in outputs.items()
        if not path.is_file() or path.read_bytes() != expected
    ]
    if args.validate:
        if mismatches:
            raise SystemExit("generated fixture mismatch: " + ", ".join(mismatches))
    else:
        for path, data in outputs.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)

    report = {
        "schema": "worr.renderer-parity.model-md5-multijoint-fixture.v1",
        "outputs": [
            {
                "path": str(path),
                "bytes": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
            }
            for path, data in outputs.items()
        ],
    }
    print(
        json.dumps(report, indent=2, sort_keys=True)
        if args.json
        else f"{'validated' if args.validate else 'generated'} "
        f"{len(outputs)} multi-joint MD5 fixture output(s)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
