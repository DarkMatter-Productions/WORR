#!/usr/bin/env python3
"""Generate a deterministic legacy-PCX-to-PNG sprite replacement fixture."""

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


MAP_NAME = "worr_fr02_sprite_replacement.bsp"
SPRITE_MODEL = "worr_fr02_sprite_replacement.sp2"
SPRITE_SOURCE = "textures/parity/fr02_sprite_replace.pcx"
BACKDROP_TEXTURE = "parity/fr02_sreplace_bg"
TEXTURE_SIZE = 64
SOURCE_PCX_RGB = (16, 220, 64)
REPLACEMENT_RGB = (188, 40, 232)
BACKDROP_RGB = (24, 64, 112)

SPRITE_ENTITIES = (
    '{\n'
    '"classname" "misc_model"\n'
    f'"model" "sprites/{SPRITE_MODEL}"\n'
    '"origin" "256 0 -22"\n'
    '"scale" "4"\n'
    '}\n',
)


def _png(rgba: bytes) -> bytes:
    def chunk(kind: bytes, payload: bytes) -> bytes:
        return (
            struct.pack(">I", len(payload)) + kind + payload
            + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
        )

    rows = b"".join(
        b"\0" + rgba[row * TEXTURE_SIZE * 4:(row + 1) * TEXTURE_SIZE * 4]
        for row in range(TEXTURE_SIZE)
    )
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(
            b"IHDR",
            struct.pack(">IIBBBBB", TEXTURE_SIZE, TEXTURE_SIZE, 8, 6, 0, 0, 0),
        )
        + chunk(b"IDAT", zlib.compress(rows, level=9))
        + chunk(b"IEND", b"")
    )


def replacement_pixels() -> bytes:
    pixels = bytearray()
    for y in range(TEXTURE_SIZE):
        for x in range(TEXTURE_SIZE):
            inset = min(x, y, TEXTURE_SIZE - 1 - x, TEXTURE_SIZE - 1 - y)
            pixels.extend((*REPLACEMENT_RGB, 0 if inset < 8 else 255))
    return bytes(pixels)


def _pcx(index: int, rgb: tuple[int, int, int]) -> bytes:
    """Return a valid 8-bit PCX whose green source differs from its PNG override."""
    header = struct.pack(
        "<4B6H48s2B4H54s",
        10, 5, 1, 8,
        0, 0, TEXTURE_SIZE - 1, TEXTURE_SIZE - 1,
        TEXTURE_SIZE, TEXTURE_SIZE,
        b"\0" * 48,
        0, 1, TEXTURE_SIZE, 1, TEXTURE_SIZE, TEXTURE_SIZE,
        b"\0" * 54,
    )
    # PCX RLE run counts are six-bit, so each 64-pixel row is 63 + 1.
    body = b"".join(bytes((0xFF, index, index)) for _ in range(TEXTURE_SIZE))
    palette = bytearray(768)
    palette[index * 3:index * 3 + 3] = bytes(rgb)
    return header + body + b"\x0c" + bytes(palette)


def sprite_model() -> bytes:
    image_name = SPRITE_SOURCE.encode("ascii")
    return struct.pack(
        "<III4I64s",
        int.from_bytes(b"IDS2", "little"),
        2,
        1,
        TEXTURE_SIZE,
        TEXTURE_SIZE,
        TEXTURE_SIZE // 2,
        TEXTURE_SIZE // 2,
        image_name.ljust(64, b"\0"),
    )


def generated_outputs(asset_root: Path) -> dict[Path, bytes]:
    return {
        asset_root / "maps" / MAP_NAME: build_bsp(
            extra_entities=SPRITE_ENTITIES,
            background_texture=BACKDROP_TEXTURE,
            bmodel_texture=BACKDROP_TEXTURE,
            bmodel_entity_origin=(0.0, 2048.0, 0.0),
            world_lightmap_rgb=None,
        ),
        asset_root / "sprites" / SPRITE_MODEL: sprite_model(),
        asset_root / "textures" / "parity" / "fr02_sprite_replace.pcx": _pcx(
            1, SOURCE_PCX_RGB
        ),
        asset_root / "textures" / "parity" / "fr02_sprite_replace.png": _png(
            replacement_pixels()
        ),
        asset_root / "textures" / f"{BACKDROP_TEXTURE}.png": _png(
            bytes((*BACKDROP_RGB, 255)) * (TEXTURE_SIZE * TEXTURE_SIZE)
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
        str(path) for path, data in outputs.items()
        if not path.is_file() or path.read_bytes() != data
    ]
    if args.validate:
        if mismatches:
            raise SystemExit("generated fixture mismatch: " + ", ".join(mismatches))
    else:
        for path, data in outputs.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)

    report = {
        "schema": "worr.renderer-parity.sprite-replacement-fixture.v1",
        "outputs": [
            {"path": str(path), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
            for path, data in outputs.items()
        ],
    }
    print(json.dumps(report, indent=2, sort_keys=True) if args.json else
          f"{'validated' if args.validate else 'generated'} {len(outputs)} sprite-replacement fixture output(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
