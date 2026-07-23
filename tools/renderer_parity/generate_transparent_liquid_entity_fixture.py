#!/usr/bin/env python3
"""Generate the deterministic liquid/entity transparent-ordering fixture."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from generate_warp_flow_fixture import (
    SURF_FLOWING,
    SURF_TRANS33,
    SURF_WARP,
    build_bsp,
)


MAP_NAME = "worr_fr01_liquid_entity_ordering.bsp"
TRANSPARENT_LIQUID_FLAGS = SURF_WARP | SURF_TRANS33 | SURF_FLOWING

# The renderer's alpha-phase boundary is selected by the renderer-specific
# draw-order threshold, not by geometric depth. The paired configs set that
# threshold to 0.5: the 0.75 rear sprite must be recorded before the
# refractive liquid pass, while the 0.25 front sprite must be emitted after it
# and remain sharp. Both use the ordinary map-entity alpha route rather than a
# renderer-only test primitive.
TRANSPARENT_SPRITE_ENTITIES = (
    '{\n'
    '"classname" "misc_model"\n'
    '"model" "sprites/s_bfg1.sp2"\n'
    '"origin" "480 -128 -22"\n'
    '"scale" "4"\n'
    '"alpha" "0.75"\n'
    '"renderFX" "32"\n'
    '}\n',
    '{\n'
    '"classname" "misc_model"\n'
    '"model" "sprites/s_bfg1.sp2"\n'
    '"origin" "160 128 -22"\n'
    '"scale" "4"\n'
    '"alpha" "0.25"\n'
    '"renderFX" "32"\n'
    '}\n',
)


def generated_outputs(asset_root: Path) -> dict[Path, bytes]:
    return {
        asset_root / "maps" / MAP_NAME: build_bsp(
            warp_flags=TRANSPARENT_LIQUID_FLAGS,
            extra_entities=TRANSPARENT_SPRITE_ENTITIES,
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
        "schema": "worr.renderer-parity.liquid-entity-ordering.v1",
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
        f"{len(outputs)} liquid-entity-ordering fixture output(s)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
