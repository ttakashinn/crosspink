#!/usr/bin/env python3
"""Validate a generated SD font pack against its configured source coverage."""

from __future__ import annotations

import argparse
from pathlib import Path
import runpy
import struct
import sys

import yaml


SCRIPT_DIR = Path(__file__).resolve().parent
EPDFONT_DIR = SCRIPT_DIR.parent
STYLE_NAMES = {0: "regular", 1: "bold", 2: "italic", 3: "bolditalic"}
TOC_FORMAT = "<B3xIIBhhHHBBBI4x"
TOC_SIZE = struct.calcsize(TOC_FORMAT)
GLYPH_FORMAT = "<BBHhhH2xI"
GLYPH_SIZE = struct.calcsize(GLYPH_FORMAT)

reader_verifier = runpy.run_path(str(SCRIPT_DIR / "verify-reader-symbol-fonts.py"))
cmap = reader_verifier["cmap"]
converter = runpy.run_path(str(SCRIPT_DIR / "fontconvert_sdcard.py"))
resolve_intervals = converter["resolve_intervals"]
CPFONT_VERSION = converter["CPFONT_VERSION"]


class VerificationError(RuntimeError):
    pass


def codepoints(intervals: list[tuple[int, int]]) -> set[int]:
    return {codepoint for first, last in intervals for codepoint in range(first, last + 1)}


def source_path(spec: dict[str, object]) -> Path:
    value = spec.get("path")
    if not isinstance(value, str) or not value:
        raise VerificationError("cp4 pack verifier requires local path-based source specs")
    path = EPDFONT_DIR / value
    if not path.is_file():
        raise VerificationError(f"missing source font: {path}")
    return path


def parse_cpfont(path: Path) -> dict[int, set[int]]:
    data = path.read_bytes()
    if len(data) < 32:
        raise VerificationError(f"{path.name}: truncated global header")
    magic, version, flags, style_count = struct.unpack_from("<8sHHB", data)
    if magic != b"CPFONT\0\0":
        raise VerificationError(f"{path.name}: invalid magic")
    if version != CPFONT_VERSION:
        raise VerificationError(f"{path.name}: version {version}, expected {CPFONT_VERSION}")
    if flags & 1 == 0:
        raise VerificationError(f"{path.name}: font is not 2-bit grayscale")
    if style_count != 4:
        raise VerificationError(f"{path.name}: has {style_count} styles, expected 4")
    toc_end = 32 + style_count * TOC_SIZE
    if len(data) < toc_end:
        raise VerificationError(f"{path.name}: truncated style TOC")

    entries: list[tuple[int, ...]] = []
    for index in range(style_count):
        entries.append(struct.unpack_from(TOC_FORMAT, data, 32 + index * TOC_SIZE))
    style_ids = [entry[0] for entry in entries]
    if style_ids != [0, 1, 2, 3]:
        raise VerificationError(f"{path.name}: style ids are {style_ids}, expected [0, 1, 2, 3]")

    coverage: dict[int, set[int]] = {}
    for index, entry in enumerate(entries):
        (
            style_id,
            interval_count,
            glyph_count,
            advance_y,
            _ascender,
            _descender,
            kern_left_entries,
            kern_right_entries,
            kern_left_classes,
            kern_right_classes,
            ligature_count,
            data_offset,
        ) = entry
        if advance_y == 0:
            raise VerificationError(f"{path.name}/{STYLE_NAMES[style_id]}: zero line advance")
        next_offset = entries[index + 1][11] if index + 1 < len(entries) else len(data)
        if data_offset < toc_end or next_offset <= data_offset or next_offset > len(data):
            raise VerificationError(f"{path.name}/{STYLE_NAMES[style_id]}: invalid section bounds")

        intervals_end = data_offset + interval_count * 12
        glyphs_end = intervals_end + glyph_count * GLYPH_SIZE
        kern_left_end = glyphs_end + kern_left_entries * 3
        kern_right_end = kern_left_end + kern_right_entries * 3
        matrix_end = kern_right_end + kern_left_classes * kern_right_classes
        bitmap_start = matrix_end + ligature_count * 8
        if bitmap_start > next_offset:
            raise VerificationError(f"{path.name}/{STYLE_NAMES[style_id]}: metadata overruns style section")

        actual: set[int] = set()
        expected_glyph_offset = 0
        previous_last = -1
        for interval_index in range(interval_count):
            first, last, glyph_offset = struct.unpack_from("<III", data, data_offset + interval_index * 12)
            if first > last or first <= previous_last or glyph_offset != expected_glyph_offset:
                raise VerificationError(
                    f"{path.name}/{STYLE_NAMES[style_id]}: invalid interval {interval_index}"
                )
            actual.update(range(first, last + 1))
            expected_glyph_offset += last - first + 1
            previous_last = last
        if expected_glyph_offset != glyph_count:
            raise VerificationError(
                f"{path.name}/{STYLE_NAMES[style_id]}: interval spans {expected_glyph_offset} glyphs, "
                f"TOC declares {glyph_count}"
            )

        bitmap_size = 0
        for glyph_index in range(glyph_count):
            glyph = struct.unpack_from(GLYPH_FORMAT, data, intervals_end + glyph_index * GLYPH_SIZE)
            data_length, bitmap_offset = glyph[5], glyph[6]
            bitmap_size = max(bitmap_size, bitmap_offset + data_length)
        if bitmap_start + bitmap_size != next_offset:
            raise VerificationError(
                f"{path.name}/{STYLE_NAMES[style_id]}: bitmap ends at {bitmap_start + bitmap_size}, "
                f"style ends at {next_offset}"
            )
        coverage[style_id] = actual
    return coverage


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config", type=Path)
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()

    config = yaml.safe_load(args.config.read_text(encoding="utf-8"))
    errors: list[str] = []
    verified_files = 0
    verified_glyph_sets = 0
    for family in config.get("families", []):
        name = family["name"]
        requested = codepoints(resolve_intervals(family["intervals"]))
        styles = family["styles"]
        fallbacks = family.get("fallbacks", {})
        expected_by_style: dict[int, set[int]] = {}
        for style_id, style_name in STYLE_NAMES.items():
            sources = [styles[style_name], *fallbacks.get(style_name, [])]
            supported: set[int] = set()
            for source in sources:
                supported.update(cmap(source_path(source)))
            expected_by_style[style_id] = requested & supported

        for size in family["sizes"]:
            path = args.output_dir / name / f"{name}_{size}.cpfont"
            if not path.is_file():
                errors.append(f"missing output: {path}")
                continue
            try:
                actual_by_style = parse_cpfont(path)
            except (OSError, struct.error, VerificationError) as error:
                errors.append(str(error))
                continue
            for style_id, expected in expected_by_style.items():
                missing = expected - actual_by_style[style_id]
                if missing:
                    sample = ", ".join(f"U+{cp:04X}" for cp in sorted(missing)[:12])
                    errors.append(
                        f"{path.name}/{STYLE_NAMES[style_id]}: missing {len(missing)} source glyphs ({sample})"
                    )
                verified_glyph_sets += 1
            verified_files += 1

    if errors:
        print("SD font pack verification failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print(
        f"Verified {verified_files} cpfont v{CPFONT_VERSION} files, four styles each; "
        f"{verified_glyph_sets} style/size coverage sets preserve every glyph supported by their source stacks."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
