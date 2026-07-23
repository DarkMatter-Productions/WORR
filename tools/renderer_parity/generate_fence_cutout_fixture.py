#!/usr/bin/env python3
"""Generate a deterministic binary-alpha fence cutout parity scene."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path

try:
    from .generate_bmodel_first_frame_fixture import build_bsp
except ImportError:
    from generate_bmodel_first_frame_fixture import build_bsp


SURF_ALPHATEST = 1 << 25
MAP_NAME = "worr_fr02_fence_cutout.bsp"
FENCE_TEXTURE = "parity/fr02_fence"
BACKDROP_TEXTURE = "parity/fr02_fence_backdrop"
BOX_TEXTURE = "parity/fr02_fence_box"
FENCE_RGB = (184, 104, 32)
BACKDROP_RGB = (24, 64, 112)
BOX_RGB = (48, 220, 96)
TEXTURE_SIZE = 64
CELL_SIZE = 8


def fence_pixels() -> bytes:
    """Return repeated opaque/transparent cells with a stable wide cutoff."""
    pixels = bytearray()
    for y in range(TEXTURE_SIZE):
        for x in range(TEXTURE_SIZE):
            opaque = ((x // CELL_SIZE) + (y // CELL_SIZE)) % 2 == 0
            pixels.extend((*FENCE_RGB, 255 if opaque else 0))
    return bytes(pixels)


def solid_pixels(rgb: tuple[int, int, int]) -> bytes:
    return bytes((*rgb, 255)) * (TEXTURE_SIZE * TEXTURE_SIZE)


def tga(rgba: bytes) -> bytes:
    header = struct.pack(
        "<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0,
        TEXTURE_SIZE, TEXTURE_SIZE, 32, 0x28,
    )
    bgra = bytearray()
    for offset in range(0, len(rgba), 4):
        r, g, b, a = rgba[offset:offset + 4]
        bgra.extend((b, g, r, a))
    return header + bytes(bgra)


def png(rgba: bytes) -> bytes:
    def chunk(kind: bytes, payload: bytes) -> bytes:
        return (
            struct.pack(">I", len(payload)) + kind + payload +
            struct.pack(">I", zlib.crc32(kind + payload) & 0xffffffff)
        )

    rows = b"".join(
        b"\0" + rgba[row * TEXTURE_SIZE * 4:(row + 1) * TEXTURE_SIZE * 4]
        for row in range(TEXTURE_SIZE)
    )
    return (
        b"\x89PNG\r\n\x1a\n" +
        chunk(b"IHDR", struct.pack(">IIBBBBB", TEXTURE_SIZE, TEXTURE_SIZE,
                                    8, 6, 0, 0, 0)) +
        chunk(b"IDAT", zlib.compress(rows, level=9)) +
        chunk(b"IEND", b"")
    )


def generated_outputs(asset_root: Path) -> dict[Path, bytes]:
    textures = {
        FENCE_TEXTURE: fence_pixels(),
        BACKDROP_TEXTURE: solid_pixels(BACKDROP_RGB),
        BOX_TEXTURE: solid_pixels(BOX_RGB),
    }
    outputs = {
        asset_root / "maps" / MAP_NAME: build_bsp(
            background_texture=FENCE_TEXTURE,
            bmodel_texture=BOX_TEXTURE,
            background_surface_flags=SURF_ALPHATEST,
            world_backdrop_texture=BACKDROP_TEXTURE,
            world_lightmap_rgb=None,
        ),
    }
    for name, rgba in textures.items():
        outputs[asset_root / "textures" / f"{name}.tga"] = tga(rgba)
        outputs[asset_root / "textures" / f"{name}.png"] = png(rgba)
    return outputs


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--asset-root", type=Path, default=Path("assets"))
    parser.add_argument("--validate", action="store_true")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    outputs = generated_outputs(args.asset_root)
    mismatches = [str(path) for path, data in outputs.items()
                  if not path.is_file() or path.read_bytes() != data]
    if args.validate:
        if mismatches:
            raise SystemExit("generated fixture mismatch: " + ", ".join(mismatches))
    else:
        for path, data in outputs.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)

    report = {
        "schema": "worr.renderer-parity.fence-cutout-fixture.v1",
        "outputs": [
            {"path": str(path), "bytes": len(data),
             "sha256": hashlib.sha256(data).hexdigest()}
            for path, data in outputs.items()
        ],
    }
    print(json.dumps(report, indent=2, sort_keys=True) if args.json else
          f"{'validated' if args.validate else 'generated'} {len(outputs)} fence cutout output(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
