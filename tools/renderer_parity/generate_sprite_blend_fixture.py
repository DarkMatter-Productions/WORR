#!/usr/bin/env python3
"""Generate deterministic truecolour-alpha and entity-alpha sprite fixtures."""

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


MAP_NAME = "worr_fr02_sprite_blend.bsp"
SPRITE_MODEL = "worr_fr02_truecolor_alpha.sp2"
SPRITE_IMAGE = "textures/parity/fr02_sprite_truecolor_alpha.png"
BACKDROP_TEXTURE = "parity/fr02_sblend_bg"
TEXTURE_SIZE = 64
TRUECOLOUR_RGB = (232, 88, 40)
BACKDROP_RGB = (24, 64, 112)

# Both entities enter the normal game snapshot.  The first takes the
# non-paletted source-alpha route; the second uses the ordinary map entity
# alpha/renderFX fields, which the client converts to RF_TRANSLUCENT.
SPRITE_ENTITIES = (
    '{\n'
    '"classname" "misc_model"\n'
    f'"model" "sprites/{SPRITE_MODEL}"\n'
    '"origin" "256 -112 -22"\n'
    '"scale" "4"\n'
    '}\n',
    '{\n'
    '"classname" "misc_model"\n'
    '"model" "sprites/s_bfg1.sp2"\n'
    '"origin" "256 112 -22"\n'
    '"scale" "4"\n'
    '"alpha" "0.5"\n'
    '"renderFX" "32"\n'
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


def truecolour_sprite_pixels() -> bytes:
    """Return a truecolour texture with transparent, half-alpha, and opaque texels."""
    pixels = bytearray()
    for y in range(TEXTURE_SIZE):
        for x in range(TEXTURE_SIZE):
            inset = min(x, y, TEXTURE_SIZE - 1 - x, TEXTURE_SIZE - 1 - y)
            alpha = 0 if inset < 8 else 128 if inset < 16 else 255
            pixels.extend((*TRUECOLOUR_RGB, alpha))
    return bytes(pixels)


def backdrop_pixels() -> bytes:
    return bytes((*BACKDROP_RGB, 255)) * (TEXTURE_SIZE * TEXTURE_SIZE)


def sprite_model() -> bytes:
    ident = int.from_bytes(b"IDS2", "little")
    image_name = SPRITE_IMAGE.encode("ascii")
    if len(image_name) >= 64:
        raise ValueError("sprite image path exceeds SP2 frame limit")
    return struct.pack(
        "<III4I64s",
        ident,
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
        asset_root / "textures" / f"{BACKDROP_TEXTURE}.png": _png(backdrop_pixels()),
        asset_root / "textures" / "parity" / "fr02_sprite_truecolor_alpha.png": _png(
            truecolour_sprite_pixels()
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
        for path, data in outputs.items()
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
        "schema": "worr.renderer-parity.sprite-blend-fixture.v1",
        "outputs": [
            {"path": str(path), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
            for path, data in outputs.items()
        ],
    }
    print(
        json.dumps(report, indent=2, sort_keys=True)
        if args.json
        else f"{'validated' if args.validate else 'generated'} {len(outputs)} sprite-blend fixture output(s)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
