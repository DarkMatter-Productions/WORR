#!/usr/bin/env python3
"""Generate a BSPX LIGHTGRID_OCTREE model-lighting parity fixture."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

from generate_bmodel_first_frame_fixture import build_bsp


MAP_NAME = "worr_fr01_lightgrid.bsp"
NO_RECEIVER_MAP_NAME = "worr_fr01_lightgrid_no_receiver.bsp"
INLINE_RECEIVER_MAP_NAME = "worr_fr01_lightgrid_inline_receiver.bsp"
LIGHTGRID_LUMP_NAME = b"LIGHTGRID_OCTREE"
LIGHTGRID_SAMPLE_RGB = (0, 200, 0)
STATIC_FALLBACK_RGB = (200, 0, 0)
INLINE_STATIC_FALLBACK_RGB = (0, 0, 200)

# An ordinary MD2 ensures the renderer uses its regular CPU sampled entity
# light path. Its origin lands in lightgrid sample 1. The primary fixture adds
# a hidden authored world-light receiver beneath it; the sibling map omits that
# receiver to keep the GL white no-static-receiver contract under test.
MODEL_ENTITY = (
    '{\n'
    '"classname" "misc_model"\n'
    '"model" "models/objects/dmspot/tris.md2"\n'
    '"origin" "256 0 -22"\n'
    '"scale" "3"\n'
    '"angle" "180"\n'
    '}\n',
)


def lightgrid_lump() -> bytes:
    """Return a 2x1x1 single-leaf grid with a visibly green second sample."""
    # BSP_ParseLightgrid inverts the three stored cell extents into scale.
    header = struct.pack(
        "<3f3I3fB3I",
        256.0, 256.0, 256.0,
        2, 1, 1,
        # The one-cell Y/Z axes must be sampled at their minima. Keep the
    # MD2 at cell (1, 0, 0), where its authored green grid sample lives.
        0.0, 0.0, -22.0,
        1,  # styles/sample
        0x80000000,  # root leaf 0
        0,  # nodes
        1,  # leafs
    )
    leaf = struct.pack("<6I", 0, 0, 0, 2, 1, 1)
    sample_zero = bytes((1, 0, 0, 0, 0))
    sample_one = bytes((1, 0, *LIGHTGRID_SAMPLE_RGB))
    return header + leaf + sample_zero + sample_one


def append_bspx_lightgrid(base_bsp: bytes) -> bytes:
    """Append the one supported BSPX entry without changing legacy lumps."""
    padding = (-len(base_bsp)) & 3
    header_offset = len(base_bsp) + padding
    entry_size = 24 + 4 + 4
    data = lightgrid_lump()
    data_offset = header_offset + 8 + entry_size
    entry = struct.pack(
        "<24sII", LIGHTGRID_LUMP_NAME.ljust(24, b"\0"), data_offset,
        len(data),
    )
    return base_bsp + b"\0" * padding + b"BSPX" + struct.pack("<I", 1) + entry + data


def build_fixture(*, include_static_receiver: bool = True) -> bytes:
    return append_bspx_lightgrid(build_bsp(
        extra_entities=MODEL_ENTITY,
        include_default_bmodel=False,
        world_lightmap_rgb=None,
        world_light_receiver_rgb=(STATIC_FALLBACK_RGB
                                  if include_static_receiver else None),
    ))


def build_inline_receiver_fixture() -> bytes:
    """Return a hidden inline BSP whose -Z lightmap receives the MD2."""
    return append_bspx_lightgrid(build_bsp(
        extra_entities=MODEL_ENTITY,
        world_lightmap_rgb=None,
        bmodel_lightmap_rgb=INLINE_STATIC_FALLBACK_RGB,
        bmodel_light_receiver=True,
        # RF_VIEWERMODEL leaves the brush model in the refdef (so both
        # light-point implementations trace it) but skips its draw in both
        # renderers. Entity translucency cannot hide a bmodel in OpenGL: the
        # bmodel pass is intentionally scheduled before alpha entities.
        bmodel_entity_properties=(
            '"renderFX" "2"\n',  # RF_VIEWERMODEL
        ),
    ))


def generated_outputs(asset_root: Path) -> dict[Path, bytes]:
    return {
        asset_root / "maps" / MAP_NAME: build_fixture(),
        asset_root / "maps" / NO_RECEIVER_MAP_NAME: build_fixture(
            include_static_receiver=False),
        asset_root / "maps" / INLINE_RECEIVER_MAP_NAME: build_inline_receiver_fixture(),
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
        "schema": "worr.renderer-parity.lightgrid-fixture.v1",
        "outputs": [
            {
                "path": str(path),
                "bytes": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
            }
            for path, data in outputs.items()
        ],
    }
    print(json.dumps(report, indent=2, sort_keys=True) if args.json else
          f"{'validated' if args.validate else 'generated'} {len(outputs)} lightgrid fixture output(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
