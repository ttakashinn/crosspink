#include "ClippingPageTools.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <string_view>

namespace ClippingPageTools {

bool isSelectableToken(const char* text, const size_t length) {
  if (!text) return false;
  for (size_t i = 0; i < length;) {
    const uint8_t byte = static_cast<uint8_t>(text[i]);
    if (byte < 0x80) {
      if (std::isalnum(byte)) return true;
      ++i;
      continue;
    }
    // U+2000..U+206F is punctuation/spacing, not a selectable word by itself.
    if (byte == 0xE2 && i + 2 < length &&
        (static_cast<uint8_t>(text[i + 1]) == 0x80 || static_cast<uint8_t>(text[i + 1]) == 0x81)) {
      i += 3;
      continue;
    }
    return true;
  }
  return false;
}

void collectWords(const Page& page, GfxRenderer& renderer, const int fontId, const int marginLeft, const int marginTop,
                  std::vector<WordRef>& words, uint16_t& rowCount) {
  words.clear();
  words.reserve(128);
  rowCount = 0;
  std::string pageText;
  pageText.reserve(2048);
  uint8_t styleMask = 0;
  bool full = false;

  for (const auto& element : page.elements) {
    if (!element || element->getTag() != TAG_PageLine) continue;
    const auto& line = static_cast<const PageLine&>(*element);
    const auto& block = line.getBlock();
    if (!block || !block->valid()) continue;
    bool rowHasWords = false;
    const int rubyShift = block->getRubyShift(renderer.getFontAscenderSize(fontId));
    for (uint16_t i = 0; i < block->wordCount(); ++i) {
      const uint16_t length = block->wordTextLen(i);
      const char* text = block->wordText(i);
      if (!isSelectableToken(text, length)) continue;
      if (words.size() == MAX_WORDS_PER_PAGE) {
        full = true;
        break;
      }
      words.push_back({static_cast<int16_t>(line.xPos + block->wordXpos(i) + marginLeft),
                       static_cast<int16_t>(line.yPos + marginTop + rubyShift), 0, rowCount,
                       static_cast<uint16_t>(words.size()), text, length, block->wordStyle(i)});
      pageText.append(text, length);
      pageText.push_back(' ');
      styleMask |= static_cast<uint8_t>(1U << (static_cast<uint8_t>(block->wordStyle(i)) & 0x03));
      rowHasWords = true;
    }
    if (rowHasWords && rowCount < UINT16_MAX) ++rowCount;
    if (full) break;
  }
  if (styleMask == 0) styleMask = 1;
  renderer.ensureSdCardFontReady(fontId, pageText.c_str(), styleMask);
  for (auto& word : words) {
    word.width = static_cast<int16_t>(std::max(1, renderer.getTextAdvanceX(fontId, word.text, word.style)));
  }
}

bool selectionText(const std::vector<WordRef>& words, uint16_t start, uint16_t end, std::string& text) {
  text.clear();
  if (words.empty()) return false;
  if (start > end) std::swap(start, end);
  if (end >= words.size()) return false;
  for (uint16_t i = start; i <= end; ++i) {
    const size_t extra = words[i].textLength + (text.empty() ? 0 : 1);
    if (extra > ClippingCodec::MAX_TEXT_BYTES - text.size()) {
      text.clear();
      return false;
    }
    if (!text.empty()) text.push_back(' ');
    text.append(words[i].text, words[i].textLength);
  }
  return !text.empty();
}

void HighlightPlan::drawUnderline(const GfxRenderer& renderer) const {
  for (size_t i = 0; i < count; ++i) {
    renderer.drawLine(lines[i].left, lines[i].bottom, lines[i].right, lines[i].bottom, 2, true);
  }
}

void HighlightPlan::clearGrayscale(const GfxRenderer& renderer) const {
  for (size_t i = 0; i < count; ++i) {
    renderer.fillRect(lines[i].left, std::max<int>(0, lines[i].bottom - 1), lines[i].right - lines[i].left + 1, 2,
                      true);
  }
}

namespace {

bool textEquals(const WordRef& word, const std::string_view token) {
  return word.textLength == token.size() && std::char_traits<char>::compare(word.text, token.data(), token.size()) == 0;
}

size_t quoteTokenCount(const std::string_view quote) {
  size_t count = 0;
  size_t cursor = 0;
  while (cursor < quote.size()) {
    while (cursor < quote.size() && quote[cursor] == ' ') ++cursor;
    if (cursor == quote.size()) break;
    ++count;
    const size_t separator = quote.find(' ', cursor);
    if (separator == std::string_view::npos) break;
    cursor = separator + 1;
  }
  return count;
}

bool quoteMatchesAt(const std::vector<WordRef>& words, const size_t first, const std::string_view quote,
                    const size_t tokenCount) {
  if (tokenCount == 0 || first > words.size() || tokenCount > words.size() - first) return false;
  size_t cursor = 0;
  for (size_t tokenIndex = 0; tokenIndex < tokenCount; ++tokenIndex) {
    while (cursor < quote.size() && quote[cursor] == ' ') ++cursor;
    const size_t separator = quote.find(' ', cursor);
    const size_t finish = separator == std::string_view::npos ? quote.size() : separator;
    if (finish == cursor || !textEquals(words[first + tokenIndex], quote.substr(cursor, finish - cursor))) return false;
    cursor = finish;
  }
  while (cursor < quote.size() && quote[cursor] == ' ') ++cursor;
  return cursor == quote.size();
}

bool findQuote(const std::vector<WordRef>& words, const ClippingCodec::Record& record, uint16_t& start, uint16_t& end) {
  const std::string_view quote(record.text);
  const size_t tokenCount = quoteTokenCount(quote);
  if (tokenCount == 0 || tokenCount > words.size()) return false;

  int bestDistance = INT_MAX;
  int best = -1;
  for (size_t first = 0; first + tokenCount <= words.size(); ++first) {
    if (!quoteMatchesAt(words, first, quote, tokenCount)) continue;
    const int distance = std::abs(static_cast<int>(first) - static_cast<int>(record.startWordIndex));
    if (distance < bestDistance) {
      bestDistance = distance;
      best = static_cast<int>(first);
    }
  }
  if (best < 0) return false;
  start = static_cast<uint16_t>(best);
  end = static_cast<uint16_t>(best + tokenCount - 1);
  return true;
}

bool exactAnchorMatches(const std::vector<WordRef>& words, const ClippingCodec::Record& record) {
  if (record.startWordIndex > record.endWordIndex || record.endWordIndex >= words.size()) return false;
  const size_t tokenCount = static_cast<size_t>(record.endWordIndex - record.startWordIndex) + 1;
  return quoteMatchesAt(words, record.startWordIndex, record.text, tokenCount);
}

bool recordCouldResolveOnPage(const ClippingCodec::Record& record, const uint16_t pageIndex,
                              const uint32_t pageVisibleOffset, const std::optional<uint32_t> nextPageVisibleOffset) {
  if (record.pageHint == pageIndex && record.pageVisibleOffset == pageVisibleOffset) return true;
  if (nextPageVisibleOffset.has_value()) {
    return record.pageVisibleOffset >= pageVisibleOffset && record.pageVisibleOffset < *nextPageVisibleOffset;
  }
  // The final page has no following offset. Keep the old page hint as a safe
  // fallback; searching neighbouring pages can paint the same quote multiple
  // times when common text repeats.
  return record.pageHint == pageIndex;
}

bool addLine(HighlightPlan& plan, const HighlightLine& candidate) {
  if (plan.count > 0) {
    auto& previous = plan.lines[plan.count - 1];
    if (previous.top == candidate.top && candidate.left <= previous.right + 8) {
      previous.right = std::max(previous.right, candidate.right);
      return true;
    }
  }
  if (plan.count == plan.lines.size()) {
    plan.truncated = true;
    return false;
  }
  plan.lines[plan.count++] = candidate;
  return true;
}

}  // namespace

size_t resolveClippings(const std::vector<WordRef>& words, const std::vector<ClippingCodec::Record>& records,
                        const uint16_t spineIndex, const uint16_t pageIndex, const uint32_t pageVisibleOffset,
                        const std::optional<uint32_t> nextPageVisibleOffset,
                        std::array<ResolvedClipping, MAX_RESOLVED_CLIPPINGS>& resolved) {
  size_t count = 0;
  for (const auto& record : records) {
    if (record.spineIndex != spineIndex ||
        !recordCouldResolveOnPage(record, pageIndex, pageVisibleOffset, nextPageVisibleOffset)) {
      continue;
    }
    uint16_t start = 0;
    uint16_t end = 0;
    const bool exactAnchor = record.pageHint == pageIndex && record.pageVisibleOffset == pageVisibleOffset &&
                             exactAnchorMatches(words, record);
    if (exactAnchor) {
      start = record.startWordIndex;
      end = record.endWordIndex;
    } else if (!findQuote(words, record, start, end)) {
      continue;
    }
    if (count == resolved.size()) break;
    resolved[count++] = {record.id, start, end};
  }
  return count;
}

HighlightPlan buildHighlightPlan(GfxRenderer& renderer, const Page& page, const int fontId, const int marginLeft,
                                 const int marginTop, const int lineHeight,
                                 const std::vector<ClippingCodec::Record>& records, const uint16_t spineIndex,
                                 const uint16_t pageIndex, const uint32_t pageVisibleOffset,
                                 const std::optional<uint32_t> nextPageVisibleOffset) {
  HighlightPlan plan;
  if (records.empty()) return plan;
  const bool hasCandidate = std::any_of(records.begin(), records.end(), [&](const auto& record) {
    return record.spineIndex == spineIndex &&
           recordCouldResolveOnPage(record, pageIndex, pageVisibleOffset, nextPageVisibleOffset);
  });
  // This avoids page-word collection, a 2KB text batch and font measurement on
  // the overwhelmingly common pages that contain no highlight anchor.
  if (!hasCandidate) return plan;
  std::vector<WordRef> words;
  uint16_t rows = 0;
  collectWords(page, renderer, fontId, marginLeft, marginTop, words, rows);
  const int highlightHeight = std::max(1, lineHeight);
  plan.resolvedCount =
      resolveClippings(words, records, spineIndex, pageIndex, pageVisibleOffset, nextPageVisibleOffset, plan.resolved);
  for (size_t resolvedIndex = 0; resolvedIndex < plan.resolvedCount; ++resolvedIndex) {
    const uint16_t start = plan.resolved[resolvedIndex].startWordIndex;
    const uint16_t end = plan.resolved[resolvedIndex].endWordIndex;
    for (uint16_t i = start; i <= end; ++i) {
      const auto& word = words[i];
      if (!addLine(plan, {word.x, static_cast<int16_t>(word.x + word.width - 1), word.y,
                          static_cast<int16_t>(word.y + highlightHeight - 1)})) {
        return plan;
      }
    }
  }
  return plan;
}

}  // namespace ClippingPageTools
