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
  ClippingPageTools::collectWords(*page_, renderer, fontId_, marginLeft_, marginTop_, words_, rowCount_);
  if (records_) {
    resolvedCount_ = ClippingPageTools::resolveClippings(words_, *records_, spineIndex_, pageIndex_, pageVisibleOffset_,
                                                         nextPageVisibleOffset_, resolved_);
  }
  if (!words_.empty()) {
    const int middle = closestInRow(rowCount_ / 2, renderer.getScreenWidth() / 2);
    if (middle >= 0) cursor_ = middle;
  }
  requestUpdate();
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
  ClippingSelectionResult selected;
  selected.startWordIndex = static_cast<uint16_t>(std::min(anchor_, cursor_));
  selected.endWordIndex = static_cast<uint16_t>(std::max(anchor_, cursor_));
  if (!ClippingPageTools::selectionText(words_, selected.startWordIndex, selected.endWordIndex, selected.text)) {
    selectionTooLong_ = true;
    requestUpdate();
    return;
  }
  for (size_t i = 0; i < resolvedCount_; ++i) {
    if (resolved_[i].startWordIndex == selected.startWordIndex && resolved_[i].endWordIndex == selected.endWordIndex) {
      selected.clippingId = resolved_[i].id;
      break;
    }
  }
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
    }
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::ScreenLeft) && cursor_ > 0) {
    --cursor_;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::ScreenRight) &&
             cursor_ + 1 < static_cast<int>(words_.size())) {
    ++cursor_;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::ScreenUp)) {
    moveVertical(-1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::ScreenDown)) {
    moveVertical(1);
  } else {
    return;
  }
  selectionTooLong_ = false;
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
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
