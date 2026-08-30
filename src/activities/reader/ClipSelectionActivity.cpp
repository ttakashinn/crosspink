#include "ClipSelectionActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <climits>
#include <cstdlib>

#include "components/UITheme.h"

void ClipSelectionActivity::onEnter() {
  Activity::onEnter();
  lineHeight_ = std::max(1, lineHeight_);
  partialSelection_.text.reserve(ClippingCodec::MAX_TEXT_BYTES);
  ClippingPageTools::collectWords(*page_, renderer, fontId_, marginLeft_, marginTop_, words_, rowCount_,
                                  &wordsTruncated_);
  if (!words_.empty()) {
    const int middle = closestInRow(rowCount_ / 2, renderer.getScreenWidth() / 2);
    if (middle >= 0) cursor_ = middle;
  }
  requestUpdate();
}

bool ClipSelectionActivity::appendCurrentSegment(ClippingSelectionResult& selection) const {
  if (words_.empty() || anchor_ < 0 || selection.segmentCount >= selection.segments.size()) return false;
  const uint16_t start = static_cast<uint16_t>(std::min(anchor_, cursor_));
  const uint16_t end = static_cast<uint16_t>(std::max(anchor_, cursor_));
  std::string pageText;
  if (!ClippingPageTools::selectionText(words_, start, end, pageText)) return false;
  const size_t separatorBytes = selection.text.empty() ? 0 : 1;
  if (pageText.size() + separatorBytes > ClippingCodec::MAX_TEXT_BYTES - selection.text.size()) return false;
  if (separatorBytes != 0) selection.text.push_back(' ');
  const uint16_t textOffset = static_cast<uint16_t>(selection.text.size());
  selection.text += pageText;
  auto& segment = selection.segments[selection.segmentCount++];
  segment.pageHint = pageIndex_;
  segment.pageVisibleOffset = pageVisibleOffset_;
  segment.startWordIndex = start;
  segment.endWordIndex = end;
  segment.textOffset = textOffset;
  segment.textLength = static_cast<uint16_t>(pageText.size());
  if (selection.segmentCount == 1) {
    selection.startWordIndex = start;
    selection.endWordIndex = end;
  }
  return true;
}

bool ClipSelectionActivity::moveToPage(const uint16_t pageIndex, const int restoredAnchor, const int restoredCursor) {
  if (!pageProvider_.loadPage || pageIndex >= pageProvider_.pageCount) return false;
  auto nextPage = pageProvider_.loadPage(pageProvider_.context, pageIndex);
  if (!nextPage) return false;
  std::vector<ClippingPageTools::WordRef> nextWords;
  uint16_t nextRows = 0;
  bool nextTruncated = false;
  ClippingPageTools::collectWords(*nextPage, renderer, fontId_, marginLeft_, marginTop_, nextWords, nextRows,
                                  &nextTruncated);
  if (nextWords.empty()) return false;

  page_ = std::move(nextPage);
  words_ = std::move(nextWords);
  rowCount_ = nextRows;
  wordsTruncated_ = nextTruncated;
  pageIndex_ = pageIndex;
  pageVisibleOffset_ = page_->visibleTextOffset;
  anchor_ = restoredAnchor >= 0 && restoredAnchor < static_cast<int>(words_.size()) ? restoredAnchor : 0;
  cursor_ = std::clamp(restoredCursor, 0, static_cast<int>(words_.size()) - 1);
  selectionTooLong_ = false;
  selectionUnavailable_ = false;
  requestUpdate();
  return true;
}

void ClipSelectionActivity::advancePage() {
  if (words_.empty() || anchor_ < 0 || cursor_ + 1 != static_cast<int>(words_.size())) return;
  if (pageIndex_ == UINT16_MAX || pageIndex_ + 1 >= pageProvider_.pageCount) return;
  if (wordsTruncated_ || partialSelection_.segmentCount + 1 >= partialSelection_.segments.size()) {
    selectionTooLong_ = true;
    requestUpdate();
    return;
  }
  ClippingSelectionResult candidate = partialSelection_;
  if (!appendCurrentSegment(candidate)) {
    selectionTooLong_ = true;
    requestUpdate();
    return;
  }
  if (!moveToPage(static_cast<uint16_t>(pageIndex_ + 1), 0, 0)) {
    selectionUnavailable_ = true;
    requestUpdate();
    return;
  }
  partialSelection_ = std::move(candidate);
}

void ClipSelectionActivity::retreatPage() {
  if (partialSelection_.segmentCount == 0) return;
  const auto previous = partialSelection_.segments[partialSelection_.segmentCount - 1];
  if (!moveToPage(previous.pageHint, previous.startWordIndex, previous.endWordIndex)) {
    selectionUnavailable_ = true;
    requestUpdate();
    return;
  }
  --partialSelection_.segmentCount;
  if (partialSelection_.segmentCount == 0) {
    partialSelection_.text.clear();
  } else {
    const auto& tail = partialSelection_.segments[partialSelection_.segmentCount - 1];
    partialSelection_.text.resize(static_cast<size_t>(tail.textOffset) + tail.textLength);
  }
  partialSelection_.segments[partialSelection_.segmentCount] = {};
}

uint32_t ClipSelectionActivity::matchingClippingId(const ClippingSelectionResult& selection) const {
  if (!records_) return 0;
  for (const auto& record : *records_) {
    if (record.spineIndex != selection.spineIndex || record.text != selection.text ||
        ClippingCodec::segmentCount(record) != selection.segmentCount) {
      continue;
    }
    bool matches = true;
    for (size_t i = 0; i < selection.segmentCount; ++i) {
      const auto saved = ClippingCodec::segmentAt(record, i);
      const auto& selected = selection.segments[i];
      if (saved.pageHint != selected.pageHint || saved.pageVisibleOffset != selected.pageVisibleOffset ||
          saved.startWordIndex != selected.startWordIndex || saved.endWordIndex != selected.endWordIndex ||
          saved.textOffset != selected.textOffset || saved.textLength != selected.textLength) {
        matches = false;
        break;
      }
    }
    if (matches) return record.id;
  }
  return 0;
}

int ClipSelectionActivity::closestInRow(const uint16_t row, const int centerX) const {
  int best = -1;
  int distance = INT_MAX;
  for (int i = 0; i < static_cast<int>(words_.size()); ++i) {
    if (words_[i].row != row) continue;
    const int candidate = std::abs(words_[i].x + words_[i].width / 2 - centerX);
    if (candidate < distance) {
      distance = candidate;
      best = i;
    }
  }
  return best;
}

int ClipSelectionActivity::wordAt(const int x, const int y) const {
  constexpr int SLOP = 4;
  for (int i = 0; i < static_cast<int>(words_.size()); ++i) {
    const auto& word = words_[i];
    if (x >= word.x - SLOP && x <= word.x + word.width + SLOP && y >= word.y - SLOP &&
        y <= word.y + lineHeight_ + SLOP) {
      return i;
    }
  }
  return -1;
}

void ClipSelectionActivity::moveVertical(const int direction) {
  if (words_.empty()) return;
  const auto& current = words_[cursor_];
  const int row = static_cast<int>(current.row) + direction;
  if (row < 0 || row >= rowCount_) return;
  const int next = closestInRow(static_cast<uint16_t>(row), current.x + current.width / 2);
  if (next >= 0) cursor_ = next;
}

void ClipSelectionActivity::confirmSelection() {
  if (words_.empty()) return;
  if (anchor_ < 0) {
    anchor_ = cursor_;
    requestUpdate();
    return;
  }
  ClippingSelectionResult selected = partialSelection_;
  if (!appendCurrentSegment(selected)) {
    selectionTooLong_ = true;
    requestUpdate();
    return;
  }
  selected.clippingId = matchingClippingId(selected);
  setResult(ActivityResult{std::move(selected)});
  finish();
}

void ClipSelectionActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    confirmSelection();
    return;
  }
  if (words_.empty()) return;

  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTapped(x, y)) {
    const int hit = wordAt(x, y);
    if (hit >= 0) {
      cursor_ = hit;
      confirmSelection();
    } else if (anchor_ >= 0 && x >= renderer.getScreenWidth() - std::max(24, renderer.getScreenWidth() / 10)) {
      cursor_ = static_cast<int>(words_.size()) - 1;
      advancePage();
    }
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::ScreenLeft)) {
    if (cursor_ > 0) {
      --cursor_;
    } else if (partialSelection_.segmentCount != 0) {
      retreatPage();
      return;
    } else {
      return;
    }
  } else if (mappedInput.wasPressed(MappedInputManager::Button::ScreenRight)) {
    if (cursor_ + 1 < static_cast<int>(words_.size())) {
      ++cursor_;
    } else {
      advancePage();
      return;
    }
  } else if (mappedInput.wasPressed(MappedInputManager::Button::ScreenUp)) {
    moveVertical(-1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::ScreenDown)) {
    moveVertical(1);
  } else {
    return;
  }
  selectionTooLong_ = false;
  selectionUnavailable_ = false;
  requestUpdate();
}

void ClipSelectionActivity::drawSelection() const {
  if (words_.empty()) return;
  const int first = anchor_ < 0 ? cursor_ : std::min(anchor_, cursor_);
  const int last = anchor_ < 0 ? cursor_ : std::max(anchor_, cursor_);
  for (int i = first; i <= last; ++i) {
    const auto& word = words_[i];
    renderer.drawLine(word.x, word.y + lineHeight_ - 1, word.x + word.width - 1, word.y + lineHeight_ - 1, 2, true);
  }
  const auto& cursor = words_[cursor_];
  renderer.drawRect(cursor.x - 2, cursor.y - 2, cursor.width + 4, lineHeight_ + 4, true);
}

void ClipSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();
  auto scope = renderer.getFontCacheManager()->createPrewarmScope();
  page_->render(renderer, fontId_, marginLeft_, marginTop_);
  scope.endScanAndPrewarm();
  page_->render(renderer, fontId_, marginLeft_, marginTop_);
  drawSelection();
  const auto labels =
      mappedInput.mapDirectionalLabels(tr(STR_BACK), anchor_ < 0 ? tr(STR_SELECT) : tr(STR_DONE), tr(STR_DIR_LEFT),
                                       tr(STR_DIR_RIGHT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (selectionTooLong_) {
    GUI.drawPopup(renderer, tr(STR_CLIPPING_TOO_LONG));
    return;
  }
  if (selectionUnavailable_) {
    GUI.drawPopup(renderer, tr(STR_ERROR_GENERAL_FAILURE));
    return;
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
