#!/usr/bin/env python3
"""Generate the deterministic model-skin glowmap renderer-parity map."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from generate_bmodel_first_frame_fixture import build_bsp


MAP_NAME = "worr_fr01_model_glowmap.bsp"
# This stock rerelease MD2 has a skin and paired skin_glow replacement. It is
# resolved through the normal game model registration path rather than a
# renderer-specific test hook.
MODEL_ENTITY = (
    '{\n'
    '"classname" "misc_model"\n'
    '"model" "models/objects/dmspot/tris.md2"\n'
    '"origin" "256 -128 -22"\n'
    '"scale" "3"\n'
    '"angle" "180"\n'
    '}\n',
)

# Keep this model-emission scene independent from the wall-glow companion used
# by the shared bmodel fixture.  Bloom captures can then isolate the MD2 skin
# contribution without a full-screen world-material emitter in the same frame.
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
        "schema": "worr.renderer-parity.model-glowmap-fixture.v1",
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
        f"{len(outputs)} model-glowmap fixture output(s)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
