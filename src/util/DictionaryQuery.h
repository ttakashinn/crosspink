#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace DictionaryQuery {

// Normalize a token for lookup/history: trim edge punctuation, compose NFD
// Vietnamese and lowercase ASCII plus precomposed Vietnamese letters.
std::string clean(std::string_view text);

// Unicode-codepoint Levenshtein distance with Vietnamese case folding. The
// result is maxDistance+1 when the bounded comparison cannot match. Unlike a
// byte-based edit distance, one accented character counts as one edit.
uint8_t editDistance(std::string_view left, std::string_view right, uint8_t maxDistance);

}  // namespace DictionaryQuery
