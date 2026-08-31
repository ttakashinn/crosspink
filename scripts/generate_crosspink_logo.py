#!/usr/bin/env python3
"""Generate CrossPink's 120 px monochrome boot logo and source assets.

The firmware uses a packed 1-bit header; the matching SVG and PNG are kept for
documentation and the user-guide EPUB. The mark is an Xteink X3-inspired
reader silhouette made entirely from orthogonal edges, so it stays clean on
monochrome e-ink displays without anti-aliasing.
"""

from __future__ import annotations

import struct
import zlib
from pathlib import Path


WIDTH = HEIGHT = 120
ROOT = Path(__file__).resolve().parents[1]
IMAGES = ROOT / "src" / "images"

# Xteink X3-inspired reader: an outer case, inset display, and two elongated
# physical buttons. Every edge maps exactly to the 1-bit pixel grid.
# Rectangles use half-open coordinates: (left, top, right, bottom).
LAYERS = (
    (True, (22, 8, 98, 112)),  # reader case
    (False, (27, 13, 93, 107)),  # reader face
    (True, (29, 17, 91, 77)),  # display bezel
    (False, (33, 21, 87, 73)),  # display
    (True, (32, 86, 56, 101)),  # left button
    (False, (36, 89, 52, 98)),  # left button face
    (True, (64, 86, 88, 101)),  # right button
    (False, (68, 89, 84, 98)),  # right button face
)


def pixels() -> list[list[bool]]:
    bitmap = [[False] * WIDTH for _ in range(HEIGHT)]
    for black, (left, top, right, bottom) in LAYERS:
        for y in range(top, bottom):
            for x in range(left, right):
                bitmap[y][x] = black
    return bitmap


def write_svg() -> None:
    layers = "\n".join(
        f'  <rect x="{left}" y="{top}" width="{right - left}" height="{bottom - top}" '
        f'fill="{"black" if black else "white"}"/>'
        for black, (left, top, right, bottom) in LAYERS
    )
    (IMAGES / "logo.svg").write_text(
        "<svg width=\"120\" height=\"120\" viewBox=\"0 0 120 120\" fill=\"none\" "
        "xmlns=\"http://www.w3.org/2000/svg\">\n"
        "  <title>CrossPink Xteink X3 reader mark</title>\n"
        "  <rect width=\"120\" height=\"120\" fill=\"white\"/>\n"
        f"{layers}\n"
        "</svg>\n",
        encoding="utf-8",
    )


def png_chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def write_png(bitmap: list[list[bool]]) -> None:
    raw = bytearray()
    for row in bitmap:
        raw.append(0)  # PNG filter: None
        for black in row:
            raw.extend((0, 0, 0) if black else (255, 255, 255))
    png = b"\x89PNG\r\n\x1a\n"
    png += png_chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 2, 0, 0, 0))
    png += png_chunk(b"IDAT", zlib.compress(bytes(raw), level=9))
    png += png_chunk(b"IEND", b"")
    (IMAGES / "Logo120.png").write_bytes(png)


def write_header(bitmap: list[list[bool]]) -> None:
    lines = [
        "#pragma once",
        "#include <cstdint>",
        "",
        "// CrossPink Xteink X3 reader mark, 120x120 px, packed 1-bit MSB-first.",
        "static const uint8_t Logo120[] = {",
    ]
    for row in bitmap:
        packed = []
        for byte_index in range(0, WIDTH, 8):
            value = 0
            for bit in range(8):
                # The renderer treats one bits as white and zero bits as black.
                if not row[byte_index + bit]:
                    value |= 1 << (7 - bit)
            packed.append(f"0x{value:02x}")
        lines.append("    " + ", ".join(packed) + ",")
    lines.append("};")
    lines.append("")
    (IMAGES / "Logo120.h").write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    bitmap = pixels()
    write_svg()
    write_png(bitmap)
    write_header(bitmap)


if __name__ == "__main__":
    main()
