#!/usr/bin/env python3
"""Generate the deterministic colour-shell bloom renderer-parity map."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from generate_bmodel_first_frame_fixture import build_bsp


MAP_NAME = "worr_fr01_model_shell.bsp"
# A direct translucent shell receiver avoids client-game powerup policy while
# still exercising the exact OpenGL GLS_BLOOM_SHELL material path.  The blue
# shell has enough contrast to prove native emission survives a scene
# threshold.
SHELL_ENTITY = (
    '{\n'
    '"classname" "misc_model"\n'
    '"model" "models/objects/dmspot/tris.md2"\n'
    '"origin" "256 -128 -22"\n'
    '"scale" "3"\n'
    '"angle" "180"\n'
    '"alpha" "0.7"\n'
    '"renderFX" "4128"\n'
    '}\n',
)

# This matches the isolated companion background used by the authored skin
# fixture, avoiding a wall-material emitter in this shell-only capture.
BACKGROUND_TEXTURE = "parity/fr01_to_background"


def generated_outputs(asset_root: Path) -> dict[Path, bytes]:
    return {
        asset_root / "maps" / MAP_NAME: build_bsp(
            extra_entities=SHELL_ENTITY,
            background_texture=BACKGROUND_TEXTURE,
            bmodel_texture=BACKGROUND_TEXTURE,
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
        "schema": "worr.renderer-parity.model-shell-fixture.v1",
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
        f"{len(outputs)} model-shell fixture output(s)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
