#!/bin/bash

set -euo pipefail

cd "$(dirname "$0")"

PYTHON_BIN="${PYTHON:-python3}"
INSTANCE_DIR="instanced_fonts/builtin-reader"
READER_FONT_STYLES=("Regular" "Italic" "Bold" "BoldItalic")
READER_FONT_SIZES=(12 14 16 18)
SOURCE_SERIF_REGULAR_WEIGHT="${SOURCE_SERIF_REGULAR_WEIGHT:-450}"

if ! [[ "$SOURCE_SERIF_REGULAR_WEIGHT" =~ ^[0-9]+$ ]] ||
  ((SOURCE_SERIF_REGULAR_WEIGHT < 200 || SOURCE_SERIF_REGULAR_WEIGHT > 900)); then
  echo "SOURCE_SERIF_REGULAR_WEIGHT must be an integer from 200 to 900" >&2
  exit 2
fi

# Source Serif/Sans and their legacy Noto coverage fallbacks omit several
# literary symbols. Pull only this reviewed set from Inter at build time; this
# is not a runtime fallback and does not import whole symbol blocks into flash.
READER_SYMBOL_INTERVALS=(
  --additional-intervals 0x2190,0x2190  # leftwards arrow
  --additional-intervals 0x2192,0x2192  # rightwards arrow
  --additional-intervals 0x2194,0x2194  # left right arrow (StarDict definition separator)
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

# Dictionary definitions commonly use IPA even when the selected book font is
# otherwise Latin-only. Keep the same complete phonetic range the Noto Sans
# built-in family provided. Source Sans is first priority; matching Noto Sans
# faces fill its uncommon IPA gaps at conversion time, so runtime coverage does
# not regress and no second font needs to be loaded on the device.
READER_PHONETIC_INTERVALS=(
  --additional-intervals 0x0250,0x02E9  # IPA Extensions + IPA spacing modifier letters
  --additional-intervals 0x03B2,0x03B2  # Greek beta
  --additional-intervals 0x03B8,0x03B8  # Greek theta
  --additional-intervals 0x03C7,0x03C7  # Greek chi
)

mkdir -p "$INSTANCE_DIR"

for family in SourceSerif SourceSans; do
  family_slug=$(echo "$family" | tr '[:upper:]' '[:lower:]')
  additional_intervals=("${READER_SYMBOL_INTERVALS[@]}")
  if [[ "$family" == "SourceSans" ]]; then
    additional_intervals+=("${READER_PHONETIC_INTERVALS[@]}")
  fi

  for size in "${READER_FONT_SIZES[@]}"; do
    for style in "${READER_FONT_STYLES[@]}"; do
      style_slug=$(echo "$style" | tr '[:upper:]' '[:lower:]')
      if [[ "$style" == *Italic ]]; then
        source_variant="Italic"
      else
        source_variant="Roman"
      fi
      if [[ "$style" == Bold* ]]; then
        weight=700
        symbol_path="../builtinFonts/source/Inter/Inter-Bold.ttf"
      else
        symbol_path="../builtinFonts/source/Inter/Inter-Regular.ttf"
        if [[ "$family" == "SourceSerif" ]]; then
          weight="$SOURCE_SERIF_REGULAR_WEIGHT"
        else
          weight=450
        fi
      fi

      if [[ "$family" == "SourceSerif" ]]; then
        if [[ "$source_variant" == "Italic" ]]; then
          variable_path="../builtinFonts/source/SourceSerif4/SourceSerif4-Italic-Variable.ttf"
        else
          variable_path="../builtinFonts/source/SourceSerif4/SourceSerif4-Variable.ttf"
        fi
        legacy_path="../builtinFonts/source/NotoSerif/NotoSerif-${style}.ttf"
        instance_path="$INSTANCE_DIR/${family_slug}_${size}_${style_slug}_opsz${size}_wght${weight}.ttf"
        "$PYTHON_BIN" instantiate-variable-font.py "$variable_path" "$instance_path" \
          --axis "opsz=$size" --axis "wght=$weight"
      else
        if [[ "$source_variant" == "Italic" ]]; then
          variable_path="../builtinFonts/source/SourceSans3/SourceSans3-Italic-Variable.ttf"
        else
          variable_path="../builtinFonts/source/SourceSans3/SourceSans3-Variable.ttf"
        fi
        legacy_path="../builtinFonts/source/NotoSans/NotoSans-${style}.ttf"
        instance_path="$INSTANCE_DIR/${family_slug}_${size}_${style_slug}_wght${weight}.ttf"
        "$PYTHON_BIN" instantiate-variable-font.py "$variable_path" "$instance_path" --axis "wght=$weight"
      fi

      font_name="${family_slug}_${size}_${style_slug}"
      output_path="../builtinFonts/${font_name}.h"
      "$PYTHON_BIN" fontconvert.py "$font_name" "$size" "$instance_path" "$legacy_path" "$symbol_path" \
        --base-font-count 2 --2bit --compress --pnum --zopfli --darken-aa \
        --kerning-intervals 0x20,0x7E --kerning-base-aliases \
        "${additional_intervals[@]}" > "$output_path"
      echo "Generated $output_path"
    done
  done
done

"$PYTHON_BIN" verify-reader-symbol-fonts.py
