#!/usr/bin/env python3
"""Generate the deterministic occluded player-rim bloom parity map."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from generate_bmodel_first_frame_fixture import build_bsp


MAP_NAME = "worr_fr01_model_rim_occluded.bsp"
BACKGROUND_TEXTURE = "parity/fr01_to_background"
# Third-person angle zero places the camera 60 units behind the player on -X.
# This native BSP wall occupies X=-45..19, immediately in front of that
# camera and across the player at the origin. Its material is deliberately
# non-emissive.
OCCLUDER_ORIGIN = (-269.0, 144.0, 0.0)


def generated_outputs(asset_root: Path) -> dict[Path, bytes]:
    return {
        asset_root / "maps" / MAP_NAME: build_bsp(
            background_texture=BACKGROUND_TEXTURE,
            bmodel_texture=BACKGROUND_TEXTURE,
            bmodel_entity_origin=OCCLUDER_ORIGIN,
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
        "schema": "worr.renderer-parity.model-rim-occluded-fixture.v1",
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
        f"{len(outputs)} model-rim-occluded fixture output(s)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
