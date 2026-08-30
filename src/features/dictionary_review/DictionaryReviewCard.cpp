#include "DictionaryReviewCard.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string_view>

namespace {

constexpr size_t MAX_WORD_BYTES = 96;
constexpr size_t MAX_PHONETIC_BYTES = 160;
constexpr size_t MAX_MEANING_BYTES = 900;
constexpr size_t MAX_EXAMPLE_BYTES = 420;
constexpr size_t MAX_COLLOCATION_BYTES = 360;

std::string trimAndCollapse(const std::string_view input) {
  std::string out;
  out.reserve(std::min<size_t>(input.size(), 512));
  bool pendingSpace = false;
  for (const unsigned char c : input) {
    if (std::isspace(c)) {
      pendingSpace = !out.empty();
      continue;
    }
    if (pendingSpace) out.push_back(' ');
    pendingSpace = false;
    out.push_back(static_cast<char>(c));
  }
  return out;
}

std::string asciiLower(const std::string_view input) {
  std::string out(input);
  for (char& c : out) {
    const unsigned char value = static_cast<unsigned char>(c);
    if (value < 0x80) c = static_cast<char>(std::tolower(value));
  }
  return out;
}

size_t matchedLabelLength(const std::string& value, const std::initializer_list<const char*> labels) {
  for (const char* label : labels) {
    const size_t length = strlen(label);
    if (value.compare(0, length, label) != 0) continue;
    if (value.size() == length) return length;
    const char next = value[length];
    if (next == ':' || next == '-' || std::isspace(static_cast<unsigned char>(next))) return length;
  }
  return std::string::npos;
}

bool startsWithAnyLabel(const std::string& value, const std::initializer_list<const char*> labels) {
  return matchedLabelLength(value, labels) != std::string::npos;
}

void truncateUtf8(std::string& value, const size_t limit) {
  if (value.size() <= limit) return;
  size_t end = limit;
  while (end > 0 && (static_cast<unsigned char>(value[end]) & 0xC0) == 0x80) --end;
  value.resize(end);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
  value += "…";
}

std::string payloadAfterLabel(const std::string& line, const std::string& lower,
                              const std::initializer_list<const char*> labels) {
  size_t offset = matchedLabelLength(lower, labels);
  if (offset == std::string::npos) return {};
  while (offset < line.size()) {
    const unsigned char c = static_cast<unsigned char>(line[offset]);
    if (c != ':' && c != '-' && !std::isspace(c)) break;
    ++offset;
  }
  return trimAndCollapse(std::string_view(line).substr(offset));
}

void appendMeaning(std::string& meaning, const std::string& line) {
  if (!meaning.empty()) meaning += " · ";
  meaning += line;
}

}  // namespace

DictionaryReviewCard dictionary_review::extractCard(const std::string& word, const std::string& plainDefinition) {
  DictionaryReviewCard card;
  card.word = trimAndCollapse(word);
  truncateUtf8(card.word, MAX_WORD_BYTES);

  const size_t sourceSize = std::min(plainDefinition.size(), dictionary_review::MAX_SOURCE_BYTES);
  size_t lineCount = 0;
  size_t start = 0;
  while (start < sourceSize && lineCount < 48) {
    const size_t newline = plainDefinition.find('\n', start);
    const size_t nul = plainDefinition.find('\0', start);
    const size_t end = std::min(newline, nul);
    const size_t boundedEnd = end == std::string::npos || end > sourceSize ? sourceSize : end;
    std::string line = trimAndCollapse(std::string_view(plainDefinition).substr(start, boundedEnd - start));
    if (!line.empty()) {
      ++lineCount;
      const std::string lower = asciiLower(line);
      const auto phoneticLabels = {"ipa", "pronunciation", "phonetic", "phiên âm"};
      const auto collocationLabels = {"collocation", "word partner", "cụm từ", "kết hợp từ"};
      const auto exampleLabels = {"example", "e.g.", "eg", "ví dụ"};
      const auto partOfSpeechLabels = {"noun",    "verb",    "adjective", "adverb",
                                       "danh từ", "động từ", "tính từ",   "trạng từ"};
      const bool phoneticLabel = startsWithAnyLabel(lower, phoneticLabels);
      const bool slashPhonetic = line.size() > 2 && ((line.front() == '/' && line.find('/', 1) != std::string::npos) ||
                                                     (line.front() == '[' && line.find(']', 1) != std::string::npos));
      if (card.phonetic.empty() && (phoneticLabel || slashPhonetic)) {
        card.phonetic = phoneticLabel ? payloadAfterLabel(line, lower, phoneticLabels) : line;
      } else if (card.collocation.empty() && startsWithAnyLabel(lower, collocationLabels)) {
        card.collocation = payloadAfterLabel(line, lower, collocationLabels);
      } else if (card.example.empty() && startsWithAnyLabel(lower, exampleLabels)) {
        card.example = payloadAfterLabel(line, lower, exampleLabels);
      } else if (startsWithAnyLabel(lower, partOfSpeechLabels)) {
        const std::string payload = payloadAfterLabel(line, lower, partOfSpeechLabels);
        if (!payload.empty() && card.meaning.size() < MAX_MEANING_BYTES) appendMeaning(card.meaning, payload);
      } else if (card.meaning.size() < MAX_MEANING_BYTES) {
        // The first real content lines form the definition block. Optional
        // labelled fields above are deliberately excluded from the meaning.
        appendMeaning(card.meaning, line);
      }
    }
    if (boundedEnd >= sourceSize) break;
    start = boundedEnd + 1;
  }

  truncateUtf8(card.phonetic, MAX_PHONETIC_BYTES);
  truncateUtf8(card.meaning, MAX_MEANING_BYTES);
  truncateUtf8(card.example, MAX_EXAMPLE_BYTES);
  truncateUtf8(card.collocation, MAX_COLLOCATION_BYTES);
  return card;
}
