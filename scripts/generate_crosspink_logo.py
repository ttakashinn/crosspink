#!/usr/bin/env python3
"""Generate CrossPink's 120 px monochrome boot logo and source assets.

The firmware uses a packed 1-bit header; the matching SVG and PNG are kept for
documentation and the user-guide EPUB.  The mark uses only orthogonal edges so
it remains clean on monochrome e-ink displays without anti-aliasing.
"""

from __future__ import annotations

import struct
import zlib
from pathlib import Path


WIDTH = HEIGHT = 120
ROOT = Path(__file__).resolve().parents[1]
IMAGES = ROOT / "src" / "images"

# A square-ended cross over an open book. The four book-page borders are
# deliberately rectangular: every edge maps exactly to the 1-bit pixel grid.
# Rectangles use half-open coordinates: (left, top, right, bottom).
RECTANGLES = (
    (48, 16, 72, 76),  # vertical cross stroke
    (26, 38, 94, 62),  # horizontal cross stroke
    (22, 80, 58, 104),  # left page outer border
    (62, 80, 98, 104),  # right page outer border
    (27, 85, 54, 100),  # left page inset, erased below
    (66, 85, 93, 100),  # right page inset, erased below
    (57, 78, 63, 106),  # book spine
)

PAGE_INSETS = RECTANGLES[4:6]


def pixels() -> list[list[bool]]:
    bitmap = [[False] * WIDTH for _ in range(HEIGHT)]
    for left, top, right, bottom in RECTANGLES[:4] + RECTANGLES[6:]:
        for y in range(top, bottom):
            for x in range(left, right):
                bitmap[y][x] = True
    for left, top, right, bottom in PAGE_INSETS:
        for y in range(top, bottom):
            for x in range(left, right):
                bitmap[y][x] = False
    return bitmap


def write_svg() -> None:
    outer = "\n".join(
        f'  <rect x="{left}" y="{top}" width="{right - left}" height="{bottom - top}" fill="black"/>'
        for left, top, right, bottom in RECTANGLES[:4] + RECTANGLES[6:]
    )
    insets = "\n".join(
        f'  <rect x="{left}" y="{top}" width="{right - left}" height="{bottom - top}" fill="white"/>'
        for left, top, right, bottom in PAGE_INSETS
    )
    (IMAGES / "logo.svg").write_text(
        "<svg width=\"120\" height=\"120\" viewBox=\"0 0 120 120\" fill=\"none\" "
        "xmlns=\"http://www.w3.org/2000/svg\">\n"
        "  <title>CrossPink cross and open-book mark</title>\n"
        "  <rect width=\"120\" height=\"120\" fill=\"white\"/>\n"
        f"{outer}\n{insets}\n"
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
        "// CrossPink cross and open-book mark, 120x120 px, packed 1-bit MSB-first.",
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
