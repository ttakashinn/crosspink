#pragma once

#include <expat.h>

#include <cstring>
#include <string_view>

// Safely tear down an expat parser: stop processing, clear callbacks, free, and null the pointer.
inline void destroyXmlParser(XML_Parser& parser) {
  if (!parser) return;
  XML_StopParser(parser, XML_FALSE);
  XML_SetElementHandler(parser, nullptr, nullptr);
  XML_SetCharacterDataHandler(parser, nullptr);
  XML_ParserFree(parser);
  parser = nullptr;
}

inline const char* xmlLocalName(const char* qName) {
  if (!qName) return "";
  const char* const separator = std::strchr(qName, ':');
  return separator ? separator + 1 : qName;
}

inline bool xmlLocalNameEquals(const char* qName, const char* expected) {
  return std::strcmp(xmlLocalName(qName), expected) == 0;
}

inline bool hasSpaceSeparatedToken(const std::string_view value, const std::string_view token) {
  size_t start = 0;
  while (start < value.size()) {
    while (start < value.size() &&
           (value[start] == ' ' || value[start] == '\t' || value[start] == '\r' || value[start] == '\n')) {
      ++start;
    }
    size_t end = start;
    while (end < value.size() && value[end] != ' ' && value[end] != '\t' && value[end] != '\r' && value[end] != '\n') {
      ++end;
    }
    if (value.substr(start, end - start) == token) return true;
    start = end;
  }
  return false;
}
