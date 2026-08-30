#!/bin/bash

set -e

cd "$(dirname "$0")"

PYTHON_BIN="${PYTHON:-python3}"

READER_FONT_STYLES=("Regular" "Italic" "Bold" "BoldItalic")
READER_FONT_SIZES=(12 14 16 18)

# Noto Serif/Sans deliberately omit several literary symbols, including the
# black star U+2605. Pull only this reviewed set from Inter at build time; this
# is not a runtime fallback and does not import whole symbol blocks into flash.
READER_SYMBOL_INTERVALS=(
  --additional-intervals 0x25A0,0x25A1  # black/white square
  --additional-intervals 0x25C6,0x25C7  # black/white diamond
  --additional-intervals 0x25CB,0x25CB  # white circle
  --additional-intervals 0x25CF,0x25CF  # black circle
  --additional-intervals 0x2605,0x2606  # black/white star
  --additional-intervals 0x2661,0x2661  # white heart
  --additional-intervals 0x2665,0x2665  # black heart
  --additional-intervals 0x2713,0x2713  # check mark
  --additional-intervals 0x2717,0x2717  # ballot X
)

for family in NotoSerif NotoSans; do
  family_slug=$(echo "$family" | tr '[:upper:]' '[:lower:]')
  for size in "${READER_FONT_SIZES[@]}"; do
    for style in "${READER_FONT_STYLES[@]}"; do
      font_name="${family_slug}_${size}_$(echo "$style" | tr '[:upper:]' '[:lower:]')"
      font_path="../builtinFonts/source/${family}/${family}-${style}.ttf"
      if [[ "$style" == Bold* ]]; then
        symbol_path="../builtinFonts/source/Inter/Inter-Bold.ttf"
      else
        symbol_path="../builtinFonts/source/Inter/Inter-Regular.ttf"
      fi
      output_path="../builtinFonts/${font_name}.h"
      "$PYTHON_BIN" fontconvert.py "$font_name" "$size" "$font_path" "$symbol_path" \
        --fallback-only-additional --2bit --compress --pnum --zopfli --darken-aa \
        "${READER_SYMBOL_INTERVALS[@]}" > "$output_path"
      echo "Generated $output_path"
    done
  done
done

"$PYTHON_BIN" verify-reader-symbol-fonts.py
