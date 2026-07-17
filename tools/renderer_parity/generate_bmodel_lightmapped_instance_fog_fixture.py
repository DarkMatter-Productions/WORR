#!/usr/bin/env python3
"""Generate a fogged dense inline-BSP grid with authored lightmaps."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from generate_bmodel_first_frame_fixture import build_bsp
from generate_bmodel_instance_fixture import INSTANCE_ENTITIES
from generate_bmodel_lightmapped_instance_fixture import BMODEL_LIGHTMAP_RGB


MAP_NAME = "worr_fr01_bmodel_instances_lightmapped_fog.bsp"
FOG_WORLDSPAWN_PROPERTIES = (
    '"fog_color" "0.30 0.50 0.70"\n',
    '"fog_density" "0.50"\n',
    '"fog_sky_factor" "0.60"\n',
)


def generated_outputs(asset_root: Path) -> dict[Path, bytes]:
    return {
        asset_root / "maps" / MAP_NAME: build_bsp(
            worldspawn_properties=FOG_WORLDSPAWN_PROPERTIES,
            extra_entities=INSTANCE_ENTITIES,
            bmodel_lightmap_rgb=BMODEL_LIGHTMAP_RGB,
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
        "schema": "worr.renderer-parity.bmodel-lightmapped-instance-fog-fixture.v1",
        "instances": len(INSTANCE_ENTITIES) + 1,
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
        f"{len(outputs)} fogged lightmapped bmodel-instancing fixture output(s)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
