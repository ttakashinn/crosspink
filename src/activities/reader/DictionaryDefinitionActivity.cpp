#include "DictionaryDefinitionActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "CrossPointSettings.h"
#include "DictionarySuggestionActivity.h"
#include "DictionaryUiLayout.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/DictHtmlPages.h"
#include "util/DictionaryHistoryStore.h"
#include "util/DictionaryTypography.h"
#include "util/HtmlToPlainText.h"

namespace {

// Longest measurable/drawable span. Wrapped lines stay under the screen width
// (far below this); only pathological unbreakable tokens are split at this cap.
constexpr size_t MAX_LINE_BYTES = 191;

// Body text left/right inset, matching the reader's default feel.
constexpr int SIDE_PADDING = 20;

// Styled-path ceiling: the laid-out Pages keep the whole definition resident
// (TextBlock arenas ≈ text + ~7 bytes/word plus per-line objects), roughly
// doubling the string's footprint while this activity is stacked over the
// reader and word-select. Bigger definitions take the span-based plain-text
// path, which holds no per-page copies.
constexpr size_t MAX_STYLED_HTML_BYTES = 16 * 1024;
constexpr size_t MAX_QUERY_BYTES = 240;
constexpr unsigned long POPUP_DURATION_MS = 1500;
constexpr unsigned long PHRASE_SELECT_HOLD_MS = 600;
constexpr unsigned long EXIT_ALL_HOLD_MS = 900;

bool selectableToken(const char* text, const size_t length) {
  for (size_t i = 0; i < length; ++i) {
    const uint8_t value = static_cast<uint8_t>(text[i]);
    if (value < 0x80) {
      if (std::isalnum(value)) return true;
    } else if (value == 0xE2 && i + 2 < length &&
               (static_cast<uint8_t>(text[i + 1]) == 0x80 || static_cast<uint8_t>(text[i + 1]) == 0x81)) {
      i += 2;
    } else {
      return true;
    }
  }
  return false;
}

void indexBuildYield(void*) { vTaskDelay(1); }

StrId lookupErrorMessage(const Dictionary::LookupResult result) {
  switch (result) {
    case Dictionary::LookupResult::LowMemory:
      return StrId::STR_DICT_LOW_MEMORY;
    case Dictionary::LookupResult::Decompress:
      return StrId::STR_DICT_DECOMPRESS_ERROR;
    case Dictionary::LookupResult::ReadError:
      return StrId::STR_DICT_READ_FAILED;
    case Dictionary::LookupResult::NotFound:
      return StrId::STR_DICT_NOT_FOUND;
    case Dictionary::LookupResult::Found:
    default:
      return StrId::STR_DICT_ERROR;
  }
}

}  // namespace

void DictionaryDefinitionActivity::onEnter() {
  Activity::onEnter();
  // Normalize StarDict multi-type separators so the wrap loop and the
  // C-string font APIs below both see the whole definition.
  std::replace(definition.begin(), definition.end(), '\0', '\n');
  if (!(htmlDefinition && definition.size() <= MAX_STYLED_HTML_BYTES && layoutHtmlPages())) {
    definition = htmlToPlainText(definition);
    wrapText();
  }
  requestUpdate();
}

void DictionaryDefinitionActivity::onExit() {
  Activity::onExit();
  if (auto* fontCache = renderer.getFontCacheManager()) {
    fontCache->releaseSdFontCaches();
  }
}

DictionaryDefinitionActivity::BodyArea DictionaryDefinitionActivity::bodyArea() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int topArea = metrics.topPadding + metrics.headerHeight;
  return {safe.width - 2 * SIDE_PADDING, safe.height - topArea - metrics.verticalSpacing};
}

// Styled path: lay the HTML definition out through the EPUB chapter parser
// into reader-identical Pages. Frees `definition` on success (the page arenas
// own the text); any failure leaves state untouched for the plain-text path.
bool DictionaryDefinitionActivity::layoutHtmlPages() {
  const BodyArea body = bodyArea();
  if (body.width <= 0 || body.height <= 0) return false;
  if (!buildDictionaryHtmlPages(renderer, definition, dictionary_typography::bodyFontId(SETTINGS.fontPointSize),
                                dictionary_typography::lineCompression(SETTINGS.lineSpacing),
                                static_cast<uint16_t>(body.width), static_cast<uint16_t>(body.height), pages)) {
    return false;
  }
  definition.clear();
  definition.shrink_to_fit();
  totalPages = static_cast<int>(pages.size());
  currentPage = 0;
  return true;
}

int DictionaryDefinitionActivity::measureSpan(const int fontId, const char* text, size_t len) const {
  char buf[MAX_LINE_BYTES + 1];
  len = std::min(len, MAX_LINE_BYTES);
  memcpy(buf, text, len);
  buf[len] = '\0';
  return renderer.getTextAdvanceX(fontId, buf, EpdFontFamily::REGULAR);
}

// Greedy word-wrap of `definition` into byte spans. '\n' breaks lines (blank
// lines survive as paragraph spacing; NULs from multi-type StarDict entries
// were normalized to newlines in onEnter); '\r' is dropped by treating it as
// a space at a token edge.
void DictionaryDefinitionActivity::wrapText() {
  lines.clear();
  lines.reserve(definition.size() / 32 + 8);

  const int fontId = dictionary_typography::bodyFontId(SETTINGS.fontPointSize);
  // SD-card fonts: merge every definition codepoint into the persistent
  // advance table up front. Otherwise each unseen codepoint measured below
  // falls back to an on-demand glyph load from SD (8-slot overflow ring).
  renderer.ensureSdCardFontReady(fontId, definition.c_str(), 0x01 /* REGULAR */);

  const BodyArea body = bodyArea();
  const int maxWidth = body.width;
  const int spaceWidth = renderer.getSpaceWidth(fontId, EpdFontFamily::REGULAR);
  const int lineHeight = renderer.getLineHeight(fontId);
  linesPerPage = std::max(1, body.height / lineHeight);

  const char* text = definition.c_str();
  const uint32_t n = static_cast<uint32_t>(definition.size());
  uint32_t lineStart = 0;
  uint32_t lineEnd = 0;  // one past the last token byte on the current line
  int lineWidth = 0;

  const auto flushLine = [&](uint32_t nextStart) {
    lines.push_back({lineStart, static_cast<uint16_t>(lineEnd - lineStart)});
    lineStart = nextStart;
    lineEnd = nextStart;
    lineWidth = 0;
  };

  uint32_t i = 0;
  while (i < n) {
    const char c = text[i];
    if (c == '\n' || c == '\0') {
      flushLine(i + 1);
      i++;
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\r') {
      i++;
      continue;
    }

    // Token: run of non-whitespace bytes, capped at the measure buffer.
    const uint32_t tokenStart = i;
    while (i < n && text[i] != ' ' && text[i] != '\t' && text[i] != '\r' && text[i] != '\n' && text[i] != '\0' &&
           i - tokenStart < MAX_LINE_BYTES) {
      i++;
    }
    // If the byte cap cut the token mid-UTF-8-sequence, back off to the last
    // complete codepoint so measure/draw never see a partial sequence. A
    // natural stop lands on whitespace or the terminating NUL, never on a
    // continuation byte, so this is a no-op there.
    while (i - tokenStart > 1 && (text[i] & 0xC0) == 0x80) i--;
    const uint32_t tokenLen = i - tokenStart;
    const int tokenWidth = measureSpan(fontId, text + tokenStart, tokenLen);

    if (lineEnd == lineStart) {
      lineStart = tokenStart;
      lineEnd = tokenStart + tokenLen;
      lineWidth = tokenWidth;
    } else if (lineWidth + spaceWidth + tokenWidth <= maxWidth &&
               tokenStart + tokenLen - lineStart <= UINT16_MAX) {  // span len must fit Line::len
      lineEnd = tokenStart + tokenLen;
      lineWidth += spaceWidth + tokenWidth;
    } else {
      flushLine(tokenStart);
      lineEnd = tokenStart + tokenLen;
      lineWidth = tokenWidth;
    }

    // An unbreakable token wider than the screen is now alone on the line
    // (any previous content was flushed above): split it at the widest
    // fitting UTF-8 boundary and carry the remainder forward.
    while (lineWidth > maxWidth && lineEnd - lineStart > 1) {
      const uint32_t len = lineEnd - lineStart;
      uint32_t lastFit = 0;
      for (uint32_t f = 1; f <= len; f++) {
        if (f == len || (text[lineStart + f] & 0xC0) != 0x80) {  // codepoint boundary
          if (measureSpan(fontId, text + lineStart, f) > maxWidth) break;
          lastFit = f;
        }
      }
      if (lastFit == 0) {
        // Even a single over-wide glyph must make progress; consume its whole
        // UTF-8 sequence rather than splitting it into invalid fragments.
        lastFit = 1;
        while (lastFit < len && (text[lineStart + lastFit] & 0xC0) == 0x80) lastFit++;
      }
      const uint32_t rest = lineStart + lastFit;
      lineEnd = rest;
      flushLine(rest);
      lineEnd = rest + (len - lastFit);
      lineWidth = measureSpan(fontId, text + lineStart, lineEnd - lineStart);
    }
  }
  if (lineEnd > lineStart) flushLine(n);

  // Trim trailing blank lines so the last page is not empty padding.
  while (!lines.empty() && lines.back().len == 0) lines.pop_back();

  totalPages = std::max(1, (static_cast<int>(lines.size()) + linesPerPage - 1) / linesPerPage);
  currentPage = 0;
}

void DictionaryDefinitionActivity::extractVisibleWords() {
  words.clear();
  words.reserve(96);
  rowCount = 0;
  const int fontId = dictionary_typography::bodyFontId(SETTINGS.fontPointSize);
  const int ascender = renderer.getFontAscenderSize(fontId);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int bodyX = safe.x + SIDE_PADDING;
  const int bodyY = safe.y + metrics.topPadding + metrics.headerHeight;

  if (!pages.empty()) {
    for (const auto& element : pages[currentPage]->elements) {
      if (element->getTag() != TAG_PageLine) continue;
      const auto* line = static_cast<const PageLine*>(element.get());
      const auto& block = line->getBlock();
      if (!block || !block->valid()) continue;
      bool hasWords = false;
      const int rubyShift = block->getRubyShift(ascender);
      for (uint16_t i = 0; i < block->wordCount(); ++i) {
        const char* text = block->wordText(i);
        const uint16_t length = block->wordTextLen(i);
        if (!selectableToken(text, length)) continue;
        WordBox word;
        word.x = static_cast<int16_t>(bodyX + line->xPos + block->wordXpos(i));
        word.y = static_cast<int16_t>(bodyY + line->yPos + rubyShift);
        word.text = text;
        word.length = length;
        word.style = block->wordStyle(i);
        word.width = static_cast<int16_t>(renderer.getTextAdvanceX(fontId, text, word.style));
        word.row = rowCount;
        words.push_back(word);
        hasWords = true;
      }
      if (hasWords) ++rowCount;
    }
  } else {
    const int firstLine = currentPage * linesPerPage;
    const int lastLine = std::min(firstLine + linesPerPage, static_cast<int>(lines.size()));
    const int lineHeight = renderer.getLineHeight(fontId);
    for (int lineIndex = firstLine; lineIndex < lastLine; ++lineIndex) {
      const Line& line = lines[lineIndex];
      const char* begin = definition.c_str() + line.start;
      const char* end = begin + line.len;
      const char* cursor = begin;
      bool hasWords = false;
      while (cursor < end) {
        while (cursor < end && std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
        const char* token = cursor;
        while (cursor < end && !std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
        const size_t length = static_cast<size_t>(cursor - token);
        if (length == 0 || !selectableToken(token, length)) continue;
        WordBox word;
        word.x = static_cast<int16_t>(bodyX + measureSpan(fontId, begin, static_cast<size_t>(token - begin)));
        word.y = static_cast<int16_t>(bodyY + (lineIndex - firstLine) * lineHeight);
        word.width = static_cast<int16_t>(measureSpan(fontId, token, length));
        word.row = rowCount;
        word.text = token;
        word.length = static_cast<uint16_t>(length);
        words.push_back(word);
        hasWords = true;
      }
      if (hasWords) ++rowCount;
    }
  }

  selected = 0;
  if (!words.empty()) {
    const int initial = closestInRow(rowCount / 2, renderer.getScreenWidth() / 2);
    if (initial >= 0) selected = initial;
  }
  selectionAnchor = selected;
}

int DictionaryDefinitionActivity::wordAt(const int x, const int y) const {
  constexpr int SLOP = 4;
  const int lineHeight = renderer.getLineHeight(dictionary_typography::bodyFontId(SETTINGS.fontPointSize));
  for (int i = 0; i < static_cast<int>(words.size()); ++i) {
    const auto& word = words[i];
    if (x >= word.x - SLOP && x < word.x + word.width + SLOP && y >= word.y - SLOP && y < word.y + lineHeight + SLOP) {
      return i;
    }
  }
  return -1;
}

int DictionaryDefinitionActivity::closestInRow(const uint16_t row, const int centerX) const {
  int best = -1;
  int distance = INT_MAX;
  for (int i = 0; i < static_cast<int>(words.size()); ++i) {
    if (words[i].row != row) continue;
    const int candidate = std::abs(words[i].x + words[i].width / 2 - centerX);
    if (candidate < distance) {
      distance = candidate;
      best = i;
    }
  }
  return best;
}

void DictionaryDefinitionActivity::moveVertical(const int direction) {
  if (words.empty()) return;
  const int target = static_cast<int>(words[selected].row) + direction;
  if (target < 0 || target >= rowCount) return;
  const int next = closestInRow(static_cast<uint16_t>(target), words[selected].x + words[selected].width / 2);
  if (next >= 0 && next != selected) {
    selected = next;
    requestUpdate();
  }
}

std::string DictionaryDefinitionActivity::selectedQuery() const {
  if (words.empty()) return {};
  const int first = rangeSelecting ? std::min(selectionAnchor, selected) : selected;
  const int last = rangeSelecting ? std::max(selectionAnchor, selected) : selected;
  std::string query;
  query.reserve(std::min<size_t>(MAX_QUERY_BYTES, static_cast<size_t>(last - first + 1) * 12));
  for (int i = first; i <= last; ++i) {
    const size_t separator = query.empty() ? 0 : 1;
    if (query.size() + separator + words[i].length > MAX_QUERY_BYTES) break;
    if (separator) query.push_back(' ');
    query.append(words[i].text, words[i].length);
  }
  return query;
}

void DictionaryDefinitionActivity::performLookup(const std::string& query, const bool offerSuggestions) {
  if (query.empty()) return;
  popup = Popup::Busy;
  if (!dictionaryOpenAttempted) {
    dictionaryOpenAttempted = true;
    dictionaryOpenOk = dictionary.open(dictionaryName.c_str());
    dictionaryNeedsIndex = dictionaryOpenOk && dictionary.needsIndex();
  }
  popupMessage = dictionaryNeedsIndex ? StrId::STR_DICT_INDEXING : StrId::STR_DICT_LOOKING_UP;
  requestUpdateAndWait();

  bool ok = dictionaryOpenOk;
  Dictionary::IndexResult indexResult = Dictionary::IndexResult::Ok;
  if (ok && dictionaryNeedsIndex) {
    ok = dictionary.buildIndex(&indexBuildYield, nullptr, &indexResult);
    dictionaryNeedsIndex = !ok;
  }
  std::string nextDefinition;
  std::string nextHeadword;
  Dictionary::LookupResult lookupResult = Dictionary::LookupResult::NotFound;
  const bool found = ok && dictionary.lookup(query.c_str(), nextDefinition, nextHeadword, &lookupResult);
  if (found) {
    DICTIONARY_HISTORY.record(query);
    if (!DICTIONARY_HISTORY.flush()) LOG_ERR("DDEF", "Could not persist dictionary history");
    auto activity = makeUniqueNoThrow<DictionaryDefinitionActivity>(renderer, mappedInput, std::move(nextHeadword),
                                                                    std::move(nextDefinition),
                                                                    dictionary.definitionsAreHtml(), dictionaryName);
    if (!activity) {
      popup = Popup::Error;
      popupMessage = StrId::STR_DICT_LOW_MEMORY;
      popupAt = millis();
      requestUpdate();
      return;
    }
    popup = Popup::None;
    selecting = false;
    rangeSelecting = false;
    touchRangeSelecting = false;
    startActivityForResult(std::move(activity), [this](const ActivityResult& result) {
      if (std::holds_alternative<DictionaryExitResult>(result.data) &&
          std::get<DictionaryExitResult>(result.data).exitAll) {
        setResult(DictionaryExitResult{true});
        finish();
      } else {
        requestUpdate();
      }
    });
    return;
  }
  if (ok && lookupResult == Dictionary::LookupResult::NotFound && offerSuggestions) {
    std::vector<std::string> suggestions;
    if (dictionary.suggest(query.c_str(), suggestions) && !suggestions.empty()) {
      popup = Popup::None;
      startActivityForResult(
          std::make_unique<DictionarySuggestionActivity>(renderer, mappedInput, std::move(suggestions)),
          [this](const ActivityResult& activityResult) {
            if (!activityResult.isCancelled && std::holds_alternative<LookupQueryResult>(activityResult.data)) {
              performLookup(std::get<LookupQueryResult>(activityResult.data).query, false);
            } else {
              requestUpdate();
            }
          });
      return;
    }
  }
  popup = lookupResult == Dictionary::LookupResult::NotFound ? Popup::NotFound : Popup::Error;
  if (!ok) {
    popupMessage =
        indexResult == Dictionary::IndexResult::LowMemory
            ? StrId::STR_DICT_LOW_MEMORY
            : (indexResult == Dictionary::IndexResult::ReadError ? StrId::STR_DICT_READ_FAILED : StrId::STR_DICT_ERROR);
  } else {
    popupMessage = lookupErrorMessage(lookupResult);
  }
  popupAt = millis();
  requestUpdate();
}

void DictionaryDefinitionActivity::changePage(const int delta) {
  const int next = currentPage + delta;
  if (next < 0 || next >= totalPages) return;
  currentPage = next;
  selecting = false;
  rangeSelecting = false;
  touchRangeSelecting = false;
  words.clear();
  requestUpdate();
}

void DictionaryDefinitionActivity::loop() {
  if (popup == Popup::NotFound || popup == Popup::Error) {
    if (millis() - popupAt >= POPUP_DURATION_MS) {
      popup = Popup::None;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Back, EXIT_ALL_HOLD_MS)) {
    setResult(DictionaryExitResult{true});
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (selecting) {
      selecting = false;
      rangeSelecting = false;
      touchRangeSelecting = false;
      requestUpdate();
      return;
    }
    finish();
    return;
  }

  if (selecting && !words.empty() &&
      mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, PHRASE_SELECT_HOLD_MS)) {
    rangeSelecting = !rangeSelecting;
    selectionAnchor = selected;
    requestUpdate();
    return;
  }
  if (mappedInput.consumeSuppressedRelease()) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!selecting) {
      selecting = true;
      rangeSelecting = false;
      extractVisibleWords();
      requestUpdate();
    } else if (!words.empty()) {
      performLookup(selectedQuery());
    }
    return;
  }

  // Same tap zones as the reader page turns: left third = previous page,
  // the rest = next. Back is the usual left-edge swipe.
  int tx = 0;
  int ty = 0;
  if (mappedInput.wasScreenLongPressStart(tx, ty)) {
    if (!selecting) {
      selecting = true;
      extractVisibleWords();
    }
    const int hit = wordAt(tx, ty);
    if (hit >= 0) {
      selected = hit;
      selectionAnchor = hit;
      rangeSelecting = true;
      touchRangeSelecting = true;
      requestUpdate();
    }
    return;
  }
  if (touchRangeSelecting) {
    if (mappedInput.isScreenTouchHeld(tx, ty)) {
      const int hit = wordAt(tx, ty);
      if (hit >= 0 && hit != selected) {
        selected = hit;
        requestUpdate();
      }
      return;
    }
    if (mappedInput.wasScreenTouchReleased()) {
      touchRangeSelecting = false;
      performLookup(selectedQuery());
      return;
    }
  }
  if (mappedInput.wasScreenTapped(tx, ty)) {
    if (selecting) {
      const int hit = wordAt(tx, ty);
      if (hit >= 0) {
        selected = hit;
        rangeSelecting = false;
        performLookup(selectedQuery());
      }
      return;
    }
    if (tx < renderer.getScreenWidth() / 3) {
      changePage(-1);
    } else {
      changePage(1);
    }
    return;
  }

  if (selecting) {
    if (mappedInput.wasPressed(MappedInputManager::Button::ScreenLeft) && selected > 0) {
      --selected;
      requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::ScreenRight) &&
               selected + 1 < static_cast<int>(words.size())) {
      ++selected;
      requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::ScreenUp)) {
      moveVertical(-1);
    } else if (mappedInput.wasPressed(MappedInputManager::Button::ScreenDown)) {
      moveVertical(1);
    }
    return;
  }

  buttonNavigator.onNext([this] { changePage(1); });
  buttonNavigator.onPrevious([this] { changePage(-1); });
}

// Draws the current page: a styled Page when the HTML layout succeeded,
// otherwise the wrapped line spans (copied into a stack buffer for NUL
// termination). Called twice per render: once in font-cache scan mode, once
// for the real paint.
void DictionaryDefinitionActivity::drawBody(const int fontId, const int x, const int startY) const {
  if (!pages.empty()) {
    pages[currentPage]->render(renderer, fontId, x, startY);
    return;
  }
  const int lineHeight = renderer.getLineHeight(fontId);
  char buf[MAX_LINE_BYTES + 1];
  const int firstLine = currentPage * linesPerPage;
  const int lastLine = std::min(firstLine + linesPerPage, static_cast<int>(lines.size()));
  for (int i = firstLine; i < lastLine; i++) {
    if (lines[i].len == 0) continue;
    const size_t len = std::min(static_cast<size_t>(lines[i].len), MAX_LINE_BYTES);
    memcpy(buf, definition.c_str() + lines[i].start, len);
    buf[len] = '\0';
    renderer.drawText(fontId, x, startY + (i - firstLine) * lineHeight, buf);
  }
}

void DictionaryDefinitionActivity::drawSelection(const int fontId) const {
  if (!selecting || words.empty()) return;
  const int first = rangeSelecting ? std::min(selectionAnchor, selected) : selected;
  const int last = rangeSelecting ? std::max(selectionAnchor, selected) : selected;
  const int lineHeight = renderer.getLineHeight(fontId);
  char text[MAX_LINE_BYTES + 1];
  for (int i = first; i <= last; ++i) {
    const auto& word = words[i];
    const int x = std::max(0, static_cast<int>(word.x) - 2);
    const int y = std::max(0, static_cast<int>(word.y) - 2);
    const int right = std::min(renderer.getScreenWidth(), static_cast<int>(word.x) + word.width + 2);
    const int bottom = std::min(renderer.getScreenHeight(), static_cast<int>(word.y) + lineHeight + 2);
    if (right > x && bottom > y) renderer.fillRect(x, y, right - x, bottom - y, true);
    const size_t length = std::min<size_t>(word.length, MAX_LINE_BYTES);
    std::memcpy(text, word.text, length);
    text[length] = '\0';
    renderer.drawText(fontId, word.x, word.y, text, false, word.style);
  }
}

void DictionaryDefinitionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  // Header: matched headword left, page counter right. Reserve the counter's
  // width before truncating the title so a long phrase cannot run underneath
  // it or beyond the safe content band.
  const int headerY = safe.y + metrics.topPadding + 10;
  int counterWidth = 0;
  char counter[16] = {};
  if (totalPages > 1) {
    snprintf(counter, sizeof(counter), "%d/%d", currentPage + 1, totalPages);
    counterWidth = renderer.getTextWidth(UI_10_FONT_ID, counter);
  }
  constexpr int TITLE_COUNTER_GAP = 8;
  const auto header =
      dictionary_ui::definitionHeaderLayout(safe.x, safe.width, SIDE_PADDING, counterWidth, TITLE_COUNTER_GAP);
  if (header.titleWidth > 0) {
    const std::string title =
        renderer.truncatedText(UI_12_FONT_ID, headword.c_str(), header.titleWidth, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, header.titleX, headerY, title.c_str(), true, EpdFontFamily::BOLD);
  }
  if (counterWidth > 0) {
    renderer.drawText(UI_10_FONT_ID, header.counterX, headerY, counter);
  }

  // Body: two-pass draw inside a prewarm scope (same pattern as the reader's
  // renderContents) so SD-card font glyphs load from SD in one batch instead
  // of one on-demand overflow read per character on every page turn.
  const int fontId = dictionary_typography::bodyFontId(SETTINGS.fontPointSize);
  const int bodyStartY = safe.y + metrics.topPadding + metrics.headerHeight;
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  drawBody(fontId, safe.x + SIDE_PADDING, bodyStartY);  // scan pass: records codepoints only
  scope.endScanAndPrewarm();
  drawBody(fontId, safe.x + SIDE_PADDING, bodyStartY);
  drawSelection(fontId);

  const auto labels = selecting ? mappedInput.mapDirectionalLabels(tr(STR_BACK), tr(STR_LOOKUP), tr(STR_DIR_LEFT),
                                                                   tr(STR_DIR_RIGHT), tr(STR_DIR_UP), tr(STR_DIR_DOWN))
                                : mappedInput.mapLabels(tr(STR_BACK), tr(STR_LOOKUP), (currentPage > 0 ? "<" : ""),
                                                        (currentPage + 1 < totalPages ? ">" : ""));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (popup != Popup::None) {
    GUI.drawPopup(renderer, I18N.get(popupMessage));
    return;
  }
  renderer.displayBuffer();
}
