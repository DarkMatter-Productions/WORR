#!/usr/bin/env python3
"""Generate the deterministic FR-10-T10 real-BSP collision fixture.

The output is a collision-only Quake II IBSP v38 with a bounded world-water
leaf, two independent inline brush-model hulls, one connected gameplay area,
and one all-visible world cluster.  It deliberately contains no render
geometry: the production server collision loader does not require it.  The
connected area and visibility cluster keep ordinary PVS/PHS gameplay carriers
available to headless networking acceptance while the same fixture exercises
the immutable rewind collision boundary.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


IBSP_IDENT = int.from_bytes(b"IBSP", "little")
BSP_VERSION = 38
HEADER_LUMPS = 19

LUMP_ENTITIES = 0
LUMP_PLANES = 1
LUMP_VISIBILITY = 3
LUMP_NODES = 4
LUMP_TEXINFO = 5
LUMP_LEAFS = 8
LUMP_LEAFBRUSHES = 10
LUMP_MODELS = 13
LUMP_BRUSHES = 14
LUMP_BRUSHSIDES = 15
LUMP_AREAS = 17

CONTENTS_SOLID = 1 << 0
CONTENTS_WATER = 1 << 5
SURF_SLICK = 1 << 1
SURF_WARP = 1 << 3

SOLID_MINS = (-16.0, -24.0, -8.0)
SOLID_MAXS = (16.0, 24.0, 8.0)
WATER_MINS = (-20.0, -12.0, -10.0)
# The water volume is deliberately tall enough to intersect a normal player
# beam muzzle while its narrow X/Y footprint remains isolated from the mover
# and player-history fixture lanes.
WATER_MAXS = (20.0, 12.0, 48.0)


def _plane(normal: tuple[float, float, float], distance: float) -> bytes:
    # The on-disk plane type is ignored and recalculated by BSP_Load.
    return struct.pack("<4fi", *normal, distance, 0)


def _box_planes(
    mins: tuple[float, float, float], maxs: tuple[float, float, float]
) -> list[bytes]:
    return [
        _plane((1.0, 0.0, 0.0), maxs[0]),
        _plane((-1.0, 0.0, 0.0), -mins[0]),
        _plane((0.0, 1.0, 0.0), maxs[1]),
        _plane((0.0, -1.0, 0.0), -mins[1]),
        _plane((0.0, 0.0, 1.0), maxs[2]),
        _plane((0.0, 0.0, -1.0), -mins[2]),
    ]


def _texinfo(name: str, flags: int, value: int) -> bytes:
    encoded = name.encode("ascii")
    if len(encoded) >= 32:
        raise ValueError(f"texture name is too long: {name}")
    axes = (1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0)
    return struct.pack(
        "<8fii32si", *axes, flags, value, encoded.ljust(32, b"\0"), -1
    )


def _leaf(
    contents: int,
    cluster: int,
    area: int,
    first_leaf_brush: int,
    num_leaf_brushes: int,
    mins: tuple[int, int, int],
    maxs: tuple[int, int, int],
) -> bytes:
    # Area zero is reserved by cmodel and fails CM_AreasConnected by design.
    # Every fixture leaf therefore belongs to the one usable gameplay area;
    # world gameplay leaves share cluster zero while inline-model-only leaves
    # retain cluster -1. There are no render leaf faces.
    return struct.pack(
        "<ihh3h3h4H",
        contents,
        cluster,
        area,
        *mins,
        *maxs,
        0,
        0,
        first_leaf_brush,
        num_leaf_brushes,
    )


def _node_children(plane: int, front_child: int, back_child: int) -> bytes:
    return struct.pack(
        "<iii3h3h2H",
        plane,
        front_child,
        back_child,
        -512,
        -512,
        -512,
        512,
        512,
        512,
        0,
        0,
    )


def _node(plane: int, front_leaf: int, back_leaf: int) -> bytes:
    return _node_children(plane, -1 - front_leaf, -1 - back_leaf)


def _model(
    mins: tuple[float, float, float],
    maxs: tuple[float, float, float],
    headnode: int,
) -> bytes:
    return struct.pack(
        "<9fiii", *mins, *maxs, 0.0, 0.0, 0.0, headnode, 0, 0
    )


def build_fixture() -> bytes:
    entities = (
        '{\n"classname" "worldspawn"\n'
        '"message" "WORR rewind collision real-BSP parity fixture"\n}\n'
        '{\n"classname" "info_player_deathmatch"\n'
        '"origin" "128 128 64"\n}\n'
        '{\n"classname" "info_player_deathmatch"\n'
        '"origin" "256 128 64"\n}\n'
        '{\n"classname" "info_player_deathmatch"\n'
        '"origin" "128 256 64"\n}\n'
        '{\n"classname" "info_player_deathmatch"\n'
        '"origin" "256 256 64"\n}\n'
        '{\n"classname" "func_rotating"\n"model" "*1"\n'
        '"spawnflags" "1"\n"speed" "45"\n"dmg" "0"\n}\n'
        '{\n"classname" "func_water"\n"model" "*2"\n}\n\0'
    ).encode("ascii")

    planes = [
        _plane((1.0, 0.0, 0.0), 0.0),
        *_box_planes(SOLID_MINS, SOLID_MAXS),
        *_box_planes(WATER_MINS, WATER_MAXS),
        _plane((1.0, 0.0, 0.0), WATER_MAXS[0]),
        _plane((-1.0, 0.0, 0.0), -WATER_MINS[0]),
    ]

    # The world tree reserves a bounded water leaf so gi.pointContents() enters
    # the real Thunderbolt underwater-discharge branch.  Each inline model then
    # owns a disjoint node/leaf tree, so BSP_ValidateTree can prove there are no
    # cycles or cross-model parent aliases.  The two leaves of an inline model
    # both reference its convex brush; CM's per-trace checkcount suppresses
    # duplicate brush work when a sweep crosses the split plane.
    leafs = [
        _leaf(CONTENTS_SOLID, -1, 1, 0, 0,
              (-512, -512, -512), (0, 512, 512)),
        _leaf(0, 0, 1, 0, 0,
              (0, -512, -512), (512, 512, 512)),
        _leaf(CONTENTS_WATER, 0, 1, 0, 1,
              (-21, -13, -11), (21, 13, 49)),
        _leaf(CONTENTS_SOLID, -1, 1, 1, 1,
              (-17, -25, -9), (17, 25, 9)),
        _leaf(CONTENTS_SOLID, -1, 1, 2, 1,
              (-17, -25, -9), (17, 25, 9)),
        _leaf(CONTENTS_WATER, -1, 1, 3, 1,
              (-21, -13, -11), (21, 13, 49)),
        _leaf(CONTENTS_WATER, -1, 1, 4, 1,
              (-21, -13, -11), (21, 13, 49)),
    ]

    lumps: list[bytes] = [b"" for _ in range(HEADER_LUMPS)]
    lumps[LUMP_ENTITIES] = entities
    lumps[LUMP_PLANES] = b"".join(planes)
    # One cluster sees and hears itself.  Both PVS and PHS offsets point to the
    # same one-byte compressed row (bit zero set).
    lumps[LUMP_VISIBILITY] = struct.pack("<III", 1, 12, 12) + b"\x01"
    lumps[LUMP_NODES] = b"".join(
        (
            _node_children(13, -1 - 1, 1),
            _node(14, 0, 2),
            _node(1, 3, 4),
            _node(7, 5, 6),
        )
    )
    lumps[LUMP_TEXINFO] = b"".join(
        (
            _texinfo("fixture/solid", SURF_SLICK, 101),
            _texinfo("fixture/water", SURF_WARP, 202),
        )
    )
    lumps[LUMP_LEAFS] = b"".join(leafs)
    lumps[LUMP_LEAFBRUSHES] = struct.pack("<5H", 0, 1, 1, 2, 2)
    lumps[LUMP_MODELS] = b"".join(
        (
            _model((-512.0, -512.0, -512.0), (512.0, 512.0, 512.0), 0),
            _model(SOLID_MINS, SOLID_MAXS, 2),
            _model(WATER_MINS, WATER_MAXS, 3),
        )
    )
    lumps[LUMP_BRUSHES] = b"".join(
        (
            struct.pack("<iii", 6, 6, CONTENTS_WATER),
            struct.pack("<iii", 0, 6, CONTENTS_SOLID),
            struct.pack("<iii", 6, 6, CONTENTS_WATER),
        )
    )
    lumps[LUMP_BRUSHSIDES] = b"".join(
        struct.pack("<HH", plane_index, texinfo)
        for plane_index, texinfo in [
            *((index, 0) for index in range(1, 7)),
            *((index, 1) for index in range(7, 13)),
        ]
    )
    # cmodel reserves area zero and floods connections starting at area one.
    # With no portals, the single usable area is self-connected.
    lumps[LUMP_AREAS] = struct.pack("<4i", 0, 0, 0, 0)

    header_size = 8 + HEADER_LUMPS * 8
    cursor = header_size
    body = bytearray()
    descriptors: list[tuple[int, int]] = []
    for lump in lumps:
        padding = (-cursor) & 3
        if padding:
            body.extend(b"\0" * padding)
            cursor += padding
        descriptors.append((cursor, len(lump)))
        body.extend(lump)
        cursor += len(lump)

    header = bytearray(struct.pack("<II", IBSP_IDENT, BSP_VERSION))
    for offset, length in descriptors:
        header.extend(struct.pack("<II", offset, length))
    assert len(header) == header_size
    return bytes(header + body)


def write_fixture(path: Path) -> dict[str, object]:
    data = build_fixture()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return {
        "schema": "worr.rewind-collision-real-bsp-fixture.v4",
        "path": str(path),
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "inline_models": 2,
        "deathmatch_spawns": 4,
        "gameplay_areas": 1,
        "visibility_clusters": 1,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    report = write_fixture(args.output)
    if args.json:
        print(json.dumps(report, sort_keys=True))
    else:
        print(
            f"generated {report['path']} ({report['bytes']} bytes, "
            f"sha256={report['sha256']})"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
