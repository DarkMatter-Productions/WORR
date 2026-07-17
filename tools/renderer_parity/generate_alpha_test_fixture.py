#!/usr/bin/env python3
"""Generate a deterministic alpha-cutout world and inline-BSP parity scene."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path

from generate_bmodel_first_frame_fixture import build_bsp


SURF_ALPHATEST = 1 << 25
MAP_NAME = "worr_fr01_alpha_test.bsp"
BACKGROUND_TEXTURE = "parity/fr01_alpha_bg"
BMODEL_TEXTURE = "parity/fr01_alpha_box"
BACKGROUND_RGB = (208, 88, 32)
BACKDROP_TEXTURE = "parity/fr01_alpha_backdrop"
BACKDROP_RGB = (16, 36, 72)
BMODEL_RGB = (48, 220, 96)
PARTIAL_ALPHA = 32


def alpha_cutout_tga(
    rgb: tuple[int, int, int], alpha: int, size: int = 16
) -> bytes:
    """Return a uniform-alpha material texture with a stable sampled cutoff."""
    if not 0 <= alpha <= 255:
        raise ValueError("alpha must fit in one byte")
    header = struct.pack(
        "<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, size, size, 32, 0x28
    )
    pixel = bytes((rgb[2], rgb[1], rgb[0], alpha))
    return header + pixel * (size * size)


def alpha_cutout_png(
    rgb: tuple[int, int, int], alpha: int, size: int = 16
) -> bytes:
    """Return the same RGBA material as PNG for renderer override loading."""
    if not 0 <= alpha <= 255:
        raise ValueError("alpha must fit in one byte")

    def chunk(kind: bytes, payload: bytes) -> bytes:
        return (
            struct.pack(">I", len(payload)) + kind + payload +
            struct.pack(">I", zlib.crc32(kind + payload) & 0xffffffff)
        )

    pixel = bytes((*rgb, alpha))
    rows = b"".join(b"\0" + pixel * size for _ in range(size))
    return (
        b"\x89PNG\r\n\x1a\n" +
        chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)) +
        chunk(b"IDAT", zlib.compress(rows, level=9)) +
        chunk(b"IEND", b"")
    )


def generated_outputs(asset_root: Path) -> dict[Path, bytes]:
    return {
        asset_root / "maps" / MAP_NAME: build_bsp(
            background_texture=BACKGROUND_TEXTURE,
            bmodel_texture=BMODEL_TEXTURE,
            background_surface_flags=SURF_ALPHATEST,
            bmodel_surface_flags=SURF_ALPHATEST,
            world_backdrop_texture=BACKDROP_TEXTURE,
            world_lightmap_rgb=None,
        ),
        asset_root / "textures" / f"{BACKGROUND_TEXTURE}.tga":
            alpha_cutout_tga(BACKGROUND_RGB, PARTIAL_ALPHA),
        asset_root / "textures" / f"{BMODEL_TEXTURE}.tga":
            alpha_cutout_tga(BMODEL_RGB, 255),
        asset_root / "textures" / f"{BACKDROP_TEXTURE}.tga":
            alpha_cutout_tga(BACKDROP_RGB, 255),
        asset_root / "textures" / f"{BACKGROUND_TEXTURE}.png":
            alpha_cutout_png(BACKGROUND_RGB, PARTIAL_ALPHA),
        asset_root / "textures" / f"{BMODEL_TEXTURE}.png":
            alpha_cutout_png(BMODEL_RGB, 255),
        asset_root / "textures" / f"{BACKDROP_TEXTURE}.png":
            alpha_cutout_png(BACKDROP_RGB, 255),
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
        "schema": "worr.renderer-parity.alpha-test-fixture.v1",
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
        f"{len(outputs)} alpha-test fixture output(s)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
