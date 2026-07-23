#!/usr/bin/env python3
"""Generate a mixed visible/off-screen inline-BSP culling workload."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from generate_bmodel_first_frame_fixture import build_bsp


MAP_NAME = "worr_fr01_bmodel_cull.bsp"


def _instance_entity(y: int, z: int) -> str:
    return (
        "{\n"
        '"classname" "func_wall"\n'
        '"model" "*1"\n'
        f'"origin" "0 {y} {z}"\n'
        "}\n"
    )


# At the capture origin, the source brush is 224 units forward. The central
# grid remains inside the 120-degree horizontal view, while the side grids are
# still inside the fixture BSP's large leaf but beyond that same frustum. This
# makes culling observable in native Vulkan telemetry without changing any
# visible receiver pixels.
VISIBLE_ENTITIES = tuple(
    _instance_entity(144 + y, z)
    for z in (-120, 0, 120)
    for y in (-140, 0, 140)
)
OFFSCREEN_ENTITIES = tuple(
    _instance_entity(y, z)
    for y in (-780, 1000)
    for z in (-240, -160, -80, 0, 80, 160, 240)
    for _ in range(3)
)
INSTANCE_ENTITIES = VISIBLE_ENTITIES + OFFSCREEN_ENTITIES


def generated_outputs(asset_root: Path) -> dict[Path, bytes]:
    return {
        asset_root / "maps" / MAP_NAME: build_bsp(
            extra_entities=INSTANCE_ENTITIES
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
        "schema": "worr.renderer-parity.bmodel-cull-fixture.v1",
        "visible_instances": len(VISIBLE_ENTITIES) + 1,
        "offscreen_instances": len(OFFSCREEN_ENTITIES),
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
        f"{len(outputs)} bmodel-culling fixture output(s)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
