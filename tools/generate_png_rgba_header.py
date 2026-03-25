#!/usr/bin/env python3

from __future__ import annotations

import argparse
import pathlib
import struct
import zlib


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def paeth_predictor(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def decode_rgba_png(path: pathlib.Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        raise SystemExit(f"Not a PNG file: {path}")

    offset = len(PNG_SIGNATURE)
    width = height = 0
    bit_depth = color_type = interlace = -1
    idat_chunks: list[bytes] = []

    while offset + 8 <= len(data):
        length = struct.unpack(">I", data[offset:offset + 4])[0]
        chunk_type = data[offset + 4:offset + 8]
        chunk_data = data[offset + 8:offset + 8 + length]
        offset += 12 + length

        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, compression, filtering, interlace = struct.unpack(
                ">IIBBBBB",
                chunk_data,
            )
            if compression != 0 or filtering != 0:
                raise SystemExit("Unsupported PNG compression/filter method")
        elif chunk_type == b"IDAT":
            idat_chunks.append(chunk_data)
        elif chunk_type == b"IEND":
            break

    if bit_depth != 8 or color_type != 6 or interlace != 0:
        raise SystemExit(
            f"Unsupported PNG format in {path}: bit_depth={bit_depth}, "
            f"color_type={color_type}, interlace={interlace}"
        )

    stride = width * 4
    decompressed = zlib.decompress(b"".join(idat_chunks))
    expected = height * (stride + 1)
    if len(decompressed) != expected:
        raise SystemExit(
            f"Unexpected decoded byte count for {path}: {len(decompressed)} != {expected}"
        )

    decoded = bytearray()
    previous = bytearray(stride)
    cursor = 0
    for _ in range(height):
        filter_type = decompressed[cursor]
        cursor += 1
        row = bytearray(decompressed[cursor:cursor + stride])
        cursor += stride

        for i in range(stride):
            left = row[i - 4] if i >= 4 else 0
            up = previous[i]
            up_left = previous[i - 4] if i >= 4 else 0
            if filter_type == 0:
                pass
            elif filter_type == 1:
                row[i] = (row[i] + left) & 0xFF
            elif filter_type == 2:
                row[i] = (row[i] + up) & 0xFF
            elif filter_type == 3:
                row[i] = (row[i] + ((left + up) // 2)) & 0xFF
            elif filter_type == 4:
                row[i] = (row[i] + paeth_predictor(left, up, up_left)) & 0xFF
            else:
                raise SystemExit(f"Unsupported PNG filter type {filter_type} in {path}")

        decoded.extend(row)
        previous = row

    return width, height, bytes(decoded)


def format_byte_lines(data: bytes, width: int = 12) -> str:
    lines: list[str] = []
    for start in range(0, len(data), width):
        chunk = data[start:start + width]
        lines.append("  " + ", ".join(f"0x{value:02x}" for value in chunk) + ",")
    return "\n".join(lines)


def write_header(source: pathlib.Path, output: pathlib.Path) -> None:
    width, height, pixels = decode_rgba_png(source)
    header = (
        "#pragma once\n"
        "#include <stdint.h>\n\n"
        "namespace worr::updater::generated {\n"
        f"inline constexpr int kBootstrapLogoWidth = {width};\n"
        f"inline constexpr int kBootstrapLogoHeight = {height};\n"
        f"inline constexpr uint8_t kBootstrapLogoRgba[{len(pixels)}] = {{\n"
        f"{format_byte_lines(pixels)}\n"
        "};\n"
        "} // namespace worr::updater::generated\n"
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(header, encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate a C++ RGBA header from a PNG.")
    parser.add_argument("input", help="Input PNG path")
    parser.add_argument("output", help="Output header path")
    args = parser.parse_args()

    write_header(pathlib.Path(args.input), pathlib.Path(args.output))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
