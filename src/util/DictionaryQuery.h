#pragma once

#include <string>
#include <string_view>

namespace DictionaryQuery {

// Normalize a token for lookup/history: trim edge punctuation, compose NFD
// Vietnamese and lowercase ASCII plus precomposed Vietnamese letters.
std::string clean(std::string_view text);

}  // namespace DictionaryQuery
