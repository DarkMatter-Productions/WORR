#!/usr/bin/env python3
"""Generate a sprite overlap fixture for the legacy no-depth-write contract."""

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


MAP_NAME = "worr_fr02_sprite_depth_write.bsp"
OPAQUE_SPRITE_MODEL = "worr_fr02_sprite_opaque.sp2"
ALPHA_SPRITE_MODEL = "worr_fr02_sprite_alpha.sp2"
OPAQUE_SPRITE_IMAGE = "textures/parity/fr02_sprite_opaque.png"
ALPHA_SPRITE_IMAGE = "textures/parity/fr02_sprite_alpha.png"
BACKDROP_TEXTURE = "parity/fr02_sdepth_bg"
TEXTURE_SIZE = 64
OPAQUE_RGB = (32, 200, 80)
ALPHA_RGB = (224, 64, 32)
BACKDROP_RGB = (24, 64, 112)

# The opaque sprite is closer than the translucent sprite. OpenGL disables
# depth writes for both, allowing the later alpha pass to blend over the
# opaque sprite. The arrangement exposes an accidental Vulkan depth write.
SPRITE_ENTITIES = (
    '{\n'
    '"classname" "misc_model"\n'
    f'"model" "sprites/{OPAQUE_SPRITE_MODEL}"\n'
    '"origin" "256 0 -22"\n'
    '"scale" "4"\n'
    '}\n',
    '{\n'
    '"classname" "misc_model"\n'
    f'"model" "sprites/{ALPHA_SPRITE_MODEL}"\n'
    '"origin" "384 0 -22"\n'
    '"scale" "4"\n'
    '"alpha" "0.5"\n'
    '"renderFX" "32"\n'
    '}\n',
)


def _png(rgb: tuple[int, int, int]) -> bytes:
    def chunk(kind: bytes, payload: bytes) -> bytes:
        return (
            struct.pack(">I", len(payload)) + kind + payload
            + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
        )

    row = b"\0" + bytes((*rgb, 255)) * TEXTURE_SIZE
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", TEXTURE_SIZE, TEXTURE_SIZE,
                                       8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(row * TEXTURE_SIZE, level=9))
        + chunk(b"IEND", b"")
    )


def sprite_model(image: str) -> bytes:
    image_name = image.encode("ascii")
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
        asset_root / "sprites" / OPAQUE_SPRITE_MODEL: sprite_model(
            OPAQUE_SPRITE_IMAGE
        ),
        asset_root / "sprites" / ALPHA_SPRITE_MODEL: sprite_model(ALPHA_SPRITE_IMAGE),
        asset_root / "textures" / "parity" / "fr02_sprite_opaque.png": _png(
            OPAQUE_RGB
        ),
        asset_root / "textures" / "parity" / "fr02_sprite_alpha.png": _png(ALPHA_RGB),
        asset_root / "textures" / f"{BACKDROP_TEXTURE}.png": _png(BACKDROP_RGB),
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
        "schema": "worr.renderer-parity.sprite-depth-write-fixture.v1",
        "outputs": [
            {"path": str(path), "bytes": len(data),
             "sha256": hashlib.sha256(data).hexdigest()}
            for path, data in outputs.items()
        ],
    }
    print(json.dumps(report, indent=2, sort_keys=True) if args.json else
          f"{'validated' if args.validate else 'generated'} "
          f"{len(outputs)} sprite-depth-write fixture output(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
