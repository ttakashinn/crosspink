#pragma once

#include <cstddef>
#include <string>

struct DictionaryReviewCard {
  std::string word;
  std::string phonetic;
  std::string meaning;
  std::string example;
  std::string collocation;

  // A review card must remain useful with compact or Vietnamese-only
  // dictionaries. Pronunciation, examples and collocations are enhancements,
  // not a reason to reject a word the user actually looked up.
  bool usable() const { return !word.empty() && !meaning.empty(); }
};

namespace dictionary_review {

// Sleep review only needs a compact card. Capping the definition at the lookup
// boundary avoids materialising a normal 64 KiB entry and another 64 KiB HTML
// conversion buffer on the ESP32-C3.
inline constexpr size_t MAX_SOURCE_BYTES = 8 * 1024;

// Extract a compact, display-safe card from the dictionary's plain-text
// representation. Fields absent from the source stay empty; the renderer
// collapses those regions instead of showing misleading placeholders.
DictionaryReviewCard extractCard(const std::string& word, const std::string& plainDefinition);

}  // namespace dictionary_review
