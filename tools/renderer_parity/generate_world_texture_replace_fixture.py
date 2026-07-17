#!/usr/bin/env python3
"""Generate unlightmapped static-world texture-replace parity fixtures."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from generate_bmodel_first_frame_fixture import build_bsp
from generate_global_fog_fixture import FOG_WORLDSPAWN_PROPERTIES


MAP_NAME = "worr_fr01_world_texture_replace.bsp"
FOG_MAP_NAME = "worr_fr01_world_texture_replace_fog.bsp"


def generated_outputs(asset_root: Path) -> dict[Path, bytes]:
    return {
        asset_root / "maps" / MAP_NAME: build_bsp(world_lightmap_rgb=None),
        asset_root / "maps" / FOG_MAP_NAME: build_bsp(
            worldspawn_properties=FOG_WORLDSPAWN_PROPERTIES,
            world_lightmap_rgb=None,
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
        "schema": "worr.renderer-parity.world-texture-replace-fixture.v1",
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
        f"{len(outputs)} world texture-replace fixture output(s)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
