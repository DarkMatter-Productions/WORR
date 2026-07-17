#!/usr/bin/env python3
"""Generate the deterministic FR-01-T10 flowing turbulent-water fixture."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

from generate_bmodel_first_frame_fixture import (
    BSP_VERSION,
    HEADER_LUMPS,
    IBSP_IDENT,
    LUMP_AREAS,
    LUMP_EDGES,
    LUMP_ENTITIES,
    LUMP_FACES,
    LUMP_LEAFFACES,
    LUMP_LEAFS,
    LUMP_MODELS,
    LUMP_NODES,
    LUMP_PLANES,
    LUMP_SURFEDGES,
    LUMP_TEXINFO,
    LUMP_VERTICES,
    _face,
    _leaf,
    _model,
    _plane,
    _tga,
)


MAP_NAME = "worr_fr01_warp_flow.bsp"
BACKGROUND_TEXTURE = "parity/fr01_wf_background"
WARP_TEXTURE = "parity/fr01_wf_turbulent"
SURF_WARP = 1 << 3
SURF_TRANS33 = 1 << 4
SURF_FLOWING = 1 << 6


def _texinfo(name: str, flags: int) -> bytes:
    axes = (0.0, 1.0, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0)
    return struct.pack("<8fii32si", *axes, flags, 0,
                       name.encode("ascii").ljust(32, b"\0"), -1)


def _quad(x: float) -> tuple[tuple[float, float, float], ...]:
    return ((x, -1024.0, -1024.0), (x, -1024.0, 1024.0),
            (x, 1024.0, 1024.0), (x, 1024.0, -1024.0))


def _checker_tga() -> bytes:
    size = 64
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0,
                         0, 0, size, size, 24, 0x20)
    pixels = bytearray()
    colors = ((26, 210, 226), (232, 64, 38))
    for y in range(size):
        for x in range(size):
            rgb = colors[((x // 8) + (y // 8)) & 1]
            pixels.extend((rgb[2], rgb[1], rgb[0]))
    return header + bytes(pixels)


def build_bsp(
    worldspawn_properties: tuple[str, ...] = (),
    warp_flags: int = SURF_WARP | SURF_FLOWING,
    view_leaf_contents: int = 0,
) -> bytes:
    properties = "".join(worldspawn_properties)
    entities = (
        '{\n"classname" "worldspawn"\n'
        '"message" "WORR FR-01-T10 warp-flow parity"\n'
        f"{properties}}}\n"
        '{\n"classname" "info_player_start"\n'
        '"origin" "0 0 -22"\n"angle" "0"\n}\n\0'
    ).encode("ascii")
    vertices = (*_quad(640.0), *_quad(320.0))
    edges = [(0, 0)]
    surfedges: list[int] = []
    firstedges: list[int] = []
    for base in (0, 4):
        winding = tuple(reversed((base, base + 1, base + 2, base + 3)))
        firstedges.append(len(surfedges))
        for index, vertex in enumerate(winding):
            edges.append((vertex, winding[(index + 1) % 4]))
            surfedges.append(len(edges) - 1)

    lumps = [b"" for _ in range(HEADER_LUMPS)]
    lumps[LUMP_ENTITIES] = entities
    lumps[LUMP_PLANES] = (_plane((1.0, 0.0, 0.0), 0.0) +
                          _plane((-1.0, 0.0, 0.0), -640.0))
    lumps[LUMP_VERTICES] = b"".join(struct.pack("<3f", *vertex)
                                      for vertex in vertices)
    lumps[LUMP_NODES] = struct.pack("<iii3h3h2H", 0, -2, -1,
                                    -2048, -2048, -2048,
                                    2048, 2048, 2048, 0, 2)
    lumps[LUMP_TEXINFO] = (_texinfo(BACKGROUND_TEXTURE, 0) +
                            _texinfo(WARP_TEXTURE, warp_flags))
    lumps[LUMP_FACES] = b"".join(_face(1, firstedge, index)
                                  for index, firstedge in enumerate(firstedges))
    lumps[LUMP_LEAFS] = b"".join((
        _leaf(1, (-2048, -2048, -2048), (-1, 2048, 2048), 0, 0),
        _leaf(view_leaf_contents, (0, -2048, -2048), (2048, 2048, 2048), 0, 2),
    ))
    lumps[LUMP_LEAFFACES] = struct.pack("<2H", 0, 1)
    lumps[LUMP_EDGES] = b"".join(struct.pack("<HH", *edge) for edge in edges)
    lumps[LUMP_SURFEDGES] = b"".join(struct.pack("<i", edge) for edge in surfedges)
    lumps[LUMP_MODELS] = _model((-2048.0, -2048.0, -2048.0),
                                (2048.0, 2048.0, 2048.0), 0, 0, 2)
    lumps[LUMP_AREAS] = struct.pack("<ii", 0, 0)

    cursor = 8 + HEADER_LUMPS * 8
    body = bytearray()
    descriptors: list[tuple[int, int]] = []
    for lump in lumps:
        padding = (-cursor) & 3
        body.extend(b"\0" * padding)
        cursor += padding
        descriptors.append((cursor, len(lump)))
        body.extend(lump)
        cursor += len(lump)

    header = bytearray(struct.pack("<II", IBSP_IDENT, BSP_VERSION))
    for offset, length in descriptors:
        header.extend(struct.pack("<II", offset, length))
    return bytes(header + body)


def outputs(root: Path) -> dict[Path, bytes]:
    return {
        root / "maps" / MAP_NAME: build_bsp(),
        root / "textures" / f"{BACKGROUND_TEXTURE}.tga": _tga((20, 32, 80)),
        root / "textures" / f"{WARP_TEXTURE}.tga": _checker_tga(),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--asset-root", type=Path, default=Path("assets"))
    parser.add_argument("--validate", action="store_true")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    generated = outputs(args.asset_root)
    mismatches = [str(path) for path, data in generated.items()
                  if not path.is_file() or path.read_bytes() != data]
    if args.validate:
        if mismatches:
            raise SystemExit("generated fixture mismatch: " + ", ".join(mismatches))
    else:
        for path, data in generated.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    report = {
        "schema": "worr.renderer-parity.warp-flow.v1",
        "outputs": [
            {"path": str(path), "sha256": hashlib.sha256(data).hexdigest(),
             "bytes": len(data)}
            for path, data in generated.items()
        ],
    }
    print(json.dumps(report, indent=2, sort_keys=True) if args.json else
          f"{'validated' if args.validate else 'generated'} {len(generated)} warp-flow fixture outputs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
