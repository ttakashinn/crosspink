#!/usr/bin/env python3
"""Verify Source reader faces preserve the complete legacy Noto coverage."""

from __future__ import annotations

from pathlib import Path
import re
import struct
import sys
import unicodedata


SCRIPT_DIR = Path(__file__).resolve().parent
FONT_DIR = SCRIPT_DIR.parent / "builtinFonts"
SOURCE_DIR = FONT_DIR / "source"

# Keep this aligned with fontconvert.py's built-in ranges. The verifier models
# the old Noto + additional-only Inter stack and requires every generated Source
# face to be a superset of the glyphs that stack emitted.
BASE_INTERVALS = (
    (0x0000, 0x007F),
    (0x0080, 0x00FF),
    (0x0100, 0x017F),
    (0x01A0, 0x01A1),
    (0x01AF, 0x01B0),
    (0x01C4, 0x021F),
    (0x1EA0, 0x1EF9),
    (0x2000, 0x206F),
    (0x2010, 0x203A),
    (0x2040, 0x205F),
    (0x20A0, 0x20CF),
    (0x0300, 0x036F),
    (0x0400, 0x04FF),
    (0x2070, 0x209F),
    (0x2200, 0x22FF),
    (0x2190, 0x21FF),
    (0xFB00, 0xFB06),
    (0xFFFD, 0xFFFD),
)
SYMBOL_INTERVALS = (
    (0x2190, 0x2190),
    (0x2192, 0x2192),
    (0x2194, 0x2194),
    (0x25A0, 0x25A1),
    (0x25C6, 0x25C7),
    (0x25CB, 0x25CB),
    (0x25CF, 0x25CF),
    (0x2605, 0x2606),
    (0x2661, 0x2661),
    (0x2665, 0x2665),
    (0x2713, 0x2713),
    (0x2717, 0x2717),
)
PHONETIC_INTERVALS = ((0x0250, 0x02E9), (0x03B2, 0x03B2), (0x03B8, 0x03B8), (0x03C7, 0x03C7))
IPA_SAMPLES = tuple(map(ord, "ɪʊəæʌɑɔɒɜŋβθχðʃʒɡɹˈˌːʰ"))
VIETNAMESE = tuple(range(0x1EA0, 0x1EFA)) + (
    0x0102,
    0x0103,
    0x0110,
    0x0111,
    0x01A0,
    0x01A1,
    0x01AF,
    0x01B0,
    0x0300,
    0x0301,
    0x0303,
    0x0309,
    0x0323,
    0x0302,
    0x0306,
    0x031B,
)


def codepoints(intervals: tuple[tuple[int, int], ...]) -> set[int]:
    return {codepoint for first, last in intervals for codepoint in range(first, last + 1)}


def _u16(data: bytes, offset: int) -> int:
    return struct.unpack_from(">H", data, offset)[0]


def _u32(data: bytes, offset: int) -> int:
    return struct.unpack_from(">I", data, offset)[0]


def _format4_codepoints(data: bytes, offset: int) -> set[int]:
    length = _u16(data, offset + 2)
    end = min(offset + length, len(data))
    seg_count = _u16(data, offset + 6) // 2
    end_codes = offset + 14
    start_codes = end_codes + 2 * seg_count + 2
    deltas = start_codes + 2 * seg_count
    range_offsets = deltas + 2 * seg_count
    result: set[int] = set()
    for index in range(seg_count):
        first = _u16(data, start_codes + 2 * index)
        last = _u16(data, end_codes + 2 * index)
        delta = _u16(data, deltas + 2 * index)
        range_offset = _u16(data, range_offsets + 2 * index)
        for codepoint in range(first, last + 1):
            if codepoint == 0xFFFF:
                continue
            if range_offset == 0:
                glyph_id = (codepoint + delta) & 0xFFFF
            else:
                glyph_offset = range_offsets + 2 * index + range_offset + 2 * (codepoint - first)
                glyph_id = _u16(data, glyph_offset) if glyph_offset + 2 <= end else 0
                if glyph_id:
                    glyph_id = (glyph_id + delta) & 0xFFFF
            if glyph_id:
                result.add(codepoint)
    return result


def _format12_codepoints(data: bytes, offset: int) -> set[int]:
    length = _u32(data, offset + 4)
    end = min(offset + length, len(data))
    group_count = _u32(data, offset + 12)
    result: set[int] = set()
    for index in range(group_count):
        group = offset + 16 + 12 * index
        if group + 12 > end:
            break
        first = _u32(data, group)
        last = _u32(data, group + 4)
        first_glyph_id = _u32(data, group + 8)
        for codepoint in range(first, last + 1):
            if first_glyph_id + codepoint - first:
                result.add(codepoint)
    return result


def cmap(path: Path) -> set[int]:
    """Read Unicode cmap coverage using only the Python standard library."""
    data = path.read_bytes()
    table_count = _u16(data, 4)
    cmap_offset = None
    for index in range(table_count):
        entry = 12 + 16 * index
        if data[entry : entry + 4] == b"cmap":
            cmap_offset = _u32(data, entry + 8)
            break
    if cmap_offset is None:
        raise ValueError(f"không tìm thấy cmap trong {path.name}")

    result: set[int] = set()
    subtable_count = _u16(data, cmap_offset + 2)
    seen: set[int] = set()
    for index in range(subtable_count):
        record = cmap_offset + 4 + 8 * index
        platform_id = _u16(data, record)
        encoding_id = _u16(data, record + 2)
        if platform_id != 0 and not (platform_id == 3 and encoding_id in (1, 10)):
            continue
        offset = cmap_offset + _u32(data, record + 4)
        if offset in seen:
            continue
        seen.add(offset)
        cmap_format = _u16(data, offset)
        if cmap_format == 4:
            result.update(_format4_codepoints(data, offset))
        elif cmap_format == 12:
            result.update(_format12_codepoints(data, offset))
    if not result:
        raise ValueError(f"không đọc được Unicode cmap trong {path.name}")
    return result


def header_codepoints(path: Path, font_name: str) -> set[int]:
    text = path.read_text(encoding="utf-8")
    match = re.search(
        rf"static const EpdUnicodeInterval {re.escape(font_name)}Intervals\[\] = \{{(.*?)\n\}};",
        text,
        re.DOTALL,
    )
    if not match:
        raise ValueError(f"không đọc được interval array trong {path.name}")
    intervals = tuple(
        (int(first, 16), int(last, 16))
        for first, last in re.findall(r"\{\s*0x([0-9A-Fa-f]+),\s*0x([0-9A-Fa-f]+),", match.group(1))
    )
    return codepoints(intervals)


def header_kerning_classes(text: str, font_name: str, side: str) -> dict[int, int]:
    def values(suffix: str) -> list[int]:
        match = re.search(
            rf"static const uint(?:8|16)_t {re.escape(font_name)}Kern{side}{suffix}\[\] = \{{(.*?)\n\}};",
            text,
            re.DOTALL,
        )
        if not match:
            raise ValueError(f"không đọc được bảng kerning {side}{suffix} của {font_name}.h")
        return [int(value, 0) for value in re.findall(r"0x[0-9A-Fa-f]+|\b\d+\b", match.group(1))]

    cps = values("Codepoints")
    class_ids = values("ClassIds")
    if len(cps) != len(class_ids):
        raise ValueError(f"bảng kerning {side} lệch codepoint/class ID trong {font_name}.h")
    return dict(zip(cps, class_ids))


def latin_base(codepoint: int) -> int | None:
    manual_bases = {
        0x0110: 0x0044,
        0x0111: 0x0064,
        0x0131: 0x0069,
        0x0141: 0x004C,
        0x0142: 0x006C,
    }
    if codepoint in manual_bases:
        return manual_bases[codepoint]
    decomposed = unicodedata.normalize("NFD", chr(codepoint))
    if decomposed and ord(decomposed[0]) < 0x80 and decomposed[0].isalpha():
        return ord(decomposed[0])
    return None


def missing_names(required: set[int] | tuple[int, ...], available: set[int]) -> str:
    return ", ".join(f"U+{codepoint:04X}" for codepoint in sorted(set(required) - available))


def main() -> int:
    errors: list[str] = []
    inter = {
        "regular": cmap(SOURCE_DIR / "Inter/Inter-Regular.ttf"),
        "italic": cmap(SOURCE_DIR / "Inter/Inter-Regular.ttf"),
        "bold": cmap(SOURCE_DIR / "Inter/Inter-Bold.ttf"),
        "bolditalic": cmap(SOURCE_DIR / "Inter/Inter-Bold.ttf"),
    }
    families = {
        "sourceserif": {
            "legacy": "NotoSerif",
            "source": (
                SOURCE_DIR / "SourceSerif4/SourceSerif4-Variable.ttf",
                SOURCE_DIR / "SourceSerif4/SourceSerif4-Italic-Variable.ttf",
            ),
            "additional": SYMBOL_INTERVALS,
        },
        "sourcesans": {
            "legacy": "NotoSans",
            "source": (
                SOURCE_DIR / "SourceSans3/SourceSans3-Variable.ttf",
                SOURCE_DIR / "SourceSans3/SourceSans3-Italic-Variable.ttf",
            ),
            "additional": SYMBOL_INTERVALS + PHONETIC_INTERVALS,
        },
    }
    base_requested = codepoints(BASE_INTERVALS)

    for family, spec in families.items():
        additional_requested = codepoints(spec["additional"])
        requested = base_requested | additional_requested
        for style in ("regular", "italic", "bold", "bolditalic"):
            legacy_style = {
                "regular": "Regular",
                "italic": "Italic",
                "bold": "Bold",
                "bolditalic": "BoldItalic",
            }[style]
            legacy = cmap(SOURCE_DIR / spec["legacy"] / f"{spec['legacy']}-{legacy_style}.ttf")
            # This exactly models the old --fallback-only-additional stack:
            # Noto alone for base ranges, Noto + Inter for curated additions.
            expected = {
                codepoint
                for codepoint in requested
                if codepoint in legacy
                or (codepoint in additional_requested and codepoint in inter[style])
            }
            for size in (12, 14, 16, 18):
                name = f"{family}_{size}_{style}"
                path = FONT_DIR / f"{name}.h"
                if not path.is_file():
                    errors.append(f"thiếu {path.name}")
                    continue
                command = path.read_text(encoding="utf-8").split("*/", 1)[0]
                if "--base-font-count 2" not in command:
                    errors.append(f"{path.name} không khóa Inter fallback vào curated intervals")
                if "--kerning-intervals 0x20,0x7E" not in command or "--kerning-base-aliases" not in command:
                    errors.append(f"{path.name} không dùng bảng kerning Latin thu gọn có alias dấu")
                try:
                    available = header_codepoints(path, name)
                except ValueError as error:
                    errors.append(str(error))
                    continue
                missing = missing_names(expected, available)
                if missing:
                    errors.append(f"{path.name} giảm coverage so với built-in Noto: {missing}")
                try:
                    header_text = path.read_text(encoding="utf-8")
                    for side in ("Left", "Right"):
                        classes = header_kerning_classes(header_text, name, side)
                        for codepoint in VIETNAMESE:
                            base = latin_base(codepoint)
                            if base in classes and codepoint in available:
                                if classes.get(codepoint) != classes[base]:
                                    errors.append(
                                        f"{path.name} không alias kerning {side} U+{codepoint:04X} "
                                        f"về U+{base:04X}"
                                    )
                except ValueError as error:
                    errors.append(str(error))

    # Vietnamese must come from Source itself, including the combining marks
    # used after NFD normalization; relying on fallback here would mix faces in
    # ordinary Vietnamese prose.
    for source_path in (
        SOURCE_DIR / "SourceSerif4/SourceSerif4-Variable.ttf",
        SOURCE_DIR / "SourceSerif4/SourceSerif4-Italic-Variable.ttf",
        SOURCE_DIR / "SourceSans3/SourceSans3-Variable.ttf",
        SOURCE_DIR / "SourceSans3/SourceSans3-Italic-Variable.ttf",
    ):
        source_cmap = cmap(source_path)
        missing = missing_names(VIETNAMESE, source_cmap)
        if missing:
            errors.append(f"{source_path.name} thiếu glyph tiếng Việt/NFD: {missing}")

    for source_path in (
        SOURCE_DIR / "SourceSans3/SourceSans3-Variable.ttf",
        SOURCE_DIR / "SourceSans3/SourceSans3-Italic-Variable.ttf",
    ):
        missing = missing_names(IPA_SAMPLES, cmap(source_path))
        if missing:
            errors.append(f"{source_path.name} thiếu IPA thông dụng của từ điển: {missing}")

    if errors:
        print("Reader font verification failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print(
        "Verified all 32 Source reader faces preserve legacy Noto coverage; "
        "Source outlines cover Vietnamese/NFD and common dictionary IPA directly."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
