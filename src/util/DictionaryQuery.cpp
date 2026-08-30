#include "DictionaryQuery.h"

#include <Utf8.h>

namespace DictionaryQuery {

std::string clean(const std::string_view text) {
  const std::string normalized = utf8CleanLookupWord(std::string(text));
  if (normalized.empty()) return {};

  std::string result;
  result.reserve(normalized.size());
  const auto* cursor = reinterpret_cast<const unsigned char*>(normalized.c_str());
  while (*cursor) utf8AppendCodepoint(utf8LowerVietnamese(utf8NextCodepoint(&cursor)), result);
  return result;
}

}  // namespace DictionaryQuery
