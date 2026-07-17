#!/usr/bin/env python3
"""Generate the checked-in horizontal or vertical 4^3 LUT parity fixture."""

from __future__ import annotations

import argparse
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT = ROOT / "assets" / "renderer_parity" / "fr01_color_lut_4x16.tga"


def build_tga(vertical: bool = False) -> bytes:
    """Return an uncompressed top-origin BGR TGA for a non-identity 4^3 LUT."""
    size = 4
    width = size if vertical else size * size
    height = size * size if vertical else size
    header = bytearray(18)
    header[2] = 2  # Uncompressed true-colour.
    header[12:14] = width.to_bytes(2, "little")
    header[14:16] = height.to_bytes(2, "little")
    header[16] = 24
    header[17] = 0x20  # Top-left origin.

    pixels = bytearray()
    for row in range(height):
        for column in range(width):
            if vertical:
                blue = row // size
                green = row % size
                red = column
            else:
                green = row
                blue = column // size
                red = column % size
                # The red and blue inversions make a premature intra-slice
                # divide observably wrong while keeping bilinear interpolation
                # simple and deterministic.
            r = 255 - red * 85
            g = green * 85
            b = 255 - blue * 85
            pixels.extend((b, g, r))
    return bytes(header + pixels)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--vertical", action="store_true")
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(build_tga(args.vertical))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
