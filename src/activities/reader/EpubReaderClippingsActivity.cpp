#include "EpubReaderClippingsActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "clippings/ClippingStore.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "saved_items/SavedItemsCatalog.h"

namespace fui = freeink::ui;

namespace {

size_t utf8LengthAt(const std::string& text, const size_t index) {
  const auto byte = static_cast<uint8_t>(text[index]);
  if ((byte & 0x80U) == 0) return 1;
  if ((byte & 0xE0U) == 0xC0U && index + 1 < text.size()) return 2;
  if ((byte & 0xF0U) == 0xE0U && index + 2 < text.size()) return 3;
  if ((byte & 0xF8U) == 0xF0U && index + 3 < text.size()) return 4;
  return 1;
}

bool utf8SpaceAt(const std::string& text, const size_t index, size_t& length) {
  const auto byte = static_cast<uint8_t>(text[index]);
  if (byte <= 0x20U) {
    length = 1;
    return true;
  }
  if (byte == 0xC2U && index + 1 < text.size() && static_cast<uint8_t>(text[index + 1]) == 0xA0U) {
    length = 2;  // non-breaking space
    return true;
  }
  if (byte == 0xE2U && index + 2 < text.size() && static_cast<uint8_t>(text[index + 1]) == 0x80U) {
    const auto tail = static_cast<uint8_t>(text[index + 2]);
    if (tail >= 0x80U && tail <= 0x8AU) {
      length = 3;  // Unicode en/em/thin spaces
      return true;
    }
  }
  return false;
}

std::string normalizedWhitespace(const std::string& text) {
  std::string normalized;
  normalized.reserve(text.size());
  bool previousSpace = true;
  for (size_t i = 0; i < text.size();) {
    size_t spaceLength = 0;
    if (utf8SpaceAt(text, i, spaceLength)) {
      if (!previousSpace) normalized.push_back(' ');
      previousSpace = true;
      i += spaceLength;
      continue;
    }
    const size_t charLength = utf8LengthAt(text, i);
    normalized.append(text, i, charLength);
    previousSpace = false;
    i += charLength;
  }
  if (!normalized.empty() && normalized.back() == ' ') normalized.pop_back();
  return normalized;
}

void appendLongWord(const GfxRenderer& renderer, const std::string& word, const int width,
                    std::vector<std::string>& lines) {
  std::string line;
  for (size_t i = 0; i < word.size();) {
    const size_t charLength = utf8LengthAt(word, i);
    const std::string candidate = line + word.substr(i, charLength);
    if (!line.empty() && renderer.getTextWidth(UI_10_FONT_ID, candidate.c_str()) > width) {
      lines.push_back(std::move(line));
      line = word.substr(i, charLength);
    } else {
      line = candidate;
    }
    i += charLength;
  }
  if (!line.empty()) lines.push_back(std::move(line));
}

void wrapAllText(const GfxRenderer& renderer, const std::string& text, const int width,
                 std::vector<std::string>& lines) {
  lines.clear();
  std::string line;
  size_t start = 0;
  while (start < text.size()) {
    const size_t end = text.find(' ', start);
    const std::string word = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (renderer.getTextWidth(UI_10_FONT_ID, word.c_str()) > width) {
      if (!line.empty()) {
        lines.push_back(std::move(line));
        line.clear();
      }
      appendLongWord(renderer, word, width, lines);
    } else {
      const std::string candidate = line.empty() ? word : line + " " + word;
      if (!line.empty() && renderer.getTextWidth(UI_10_FONT_ID, candidate.c_str()) > width) {
        lines.push_back(std::move(line));
        line = word;
      } else {
        line = candidate;
      }
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  if (!line.empty()) lines.push_back(std::move(line));
  if (lines.empty()) lines.emplace_back();
}

}  // namespace

EpubReaderClippingsActivity::EpubReaderClippingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                         const std::shared_ptr<Epub>& epub,
                                                         std::vector<ClippingCodec::Record>& clippings, bool& writable)
    : UiListActivity("EpubReaderClippings", renderer, mappedInput, /*wantsTouchLongPress=*/true),
      epub(epub),
      clippings(clippings),
      writable(writable) {}

void EpubReaderClippingsActivity::onEnter() {
  UiListActivity::onEnter();
  if (auto* fonts = renderer.getFontCacheManager()) fonts->clearCache();
  detailText.reserve(ClippingCodec::MAX_TEXT_BYTES);
  detailLines.reserve(32);
}

const char* EpubReaderClippingsActivity::headerTitle() const { return tr(STR_MY_CLIPPINGS); }

void EpubReaderClippingsActivity::refreshWindow(int start) {
  if (clippings.empty()) {
    windowStart = -1;
    windowCount = 0;
    return;
  }
  start = std::clamp(start, 0, static_cast<int>(clippings.size()) - 1);
  if (start == windowStart) return;
  windowStart = start;
  windowCount = std::min(ROW_WINDOW, static_cast<int>(clippings.size()) - windowStart);
  for (int i = 0; i < windowCount; ++i) {
    const auto& clipping = clippings[windowStart + i];
    snippets[i] = normalizedWhitespace(clipping.text);
    const int tocIndex = epub ? epub->getTocIndexForSpineIndex(clipping.spineIndex) : -1;
    const std::string chapter = tocIndex >= 0 ? epub->getTocItem(tocIndex).title : tr(STR_UNNAMED);
    const auto first = ClippingCodec::segmentAt(clipping, 0);
    const auto last = ClippingCodec::segmentAt(clipping, ClippingCodec::segmentCount(clipping) - 1);
    const std::string pageRange = first.pageHint == last.pageHint
                                      ? std::to_string(first.pageHint + 1)
                                      : std::to_string(first.pageHint + 1) + "–" + std::to_string(last.pageHint + 1);
    subtitles[i] = chapter + " · " + tr(STR_CLIPPING_PAGE_PREFIX) + " " + pageRange;
    fui::ListItem item;
    item.label = snippets[i].c_str();
    item.subtitle = subtitles[i].c_str();
    item.icon = listIconFor(UIIcon::Text, 32);
    item.actionValue = static_cast<int16_t>(windowStart + i);
    rowItems[i] = item;
  }
}

void EpubReaderClippingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  if (clippings.empty()) {
    screen.centeredText(tr(STR_NO_CLIPPINGS), screen.theme().bodyText);
    return;
  }
  if (deleteFailed) {
    const fui::Rect warning = screen.takeTop(static_cast<int16_t>(renderer.getLineHeight(SMALL_FONT_ID) + 6));
    screen.target().text(warning, tr(STR_CLIPPING_DELETE_FAILED), screen.theme().smallText);
  }
  if (!mappedInput.hasTouch() && writable) {
    const int helpHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const fui::Rect help = screen.takeBottom(static_cast<int16_t>(helpHeight + metrics.verticalSpacing));
    GUI.drawHelpText(renderer, Rect{help.x, help.y + metrics.verticalSpacing, help.width, helpHeight},
                     tr(STR_HOLD_OPEN_TO_DELETE));
  }

  fui::ListProps props;
  props.count = static_cast<uint16_t>(clippings.size());
  props.action = ACTION_ROW;
  props.inputMask = writable ? fui::InputTouch | fui::InputLongPress : fui::InputTouch;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  refreshWindow(nav.top);
  props.items = rowItems.data();
  props.itemsWindowFirst = static_cast<uint16_t>(windowStart);
  props.itemsWindowCount = static_cast<uint16_t>(windowCount);
  screen.list(props);
}

void EpubReaderClippingsActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount() || confirmPopup.isActive()) return;
  app.clearTapFlash();
  nav.selected = index;
  openSelectedDetail();
}

void EpubReaderClippingsActivity::onRowLongPress(const int index) {
  if (!writable || index < 0 || index >= listCount() || confirmPopup.isActive()) return;
  app.clearTapFlash();
  nav.selected = index;
  showDeleteConfirmation();
}

bool EpubReaderClippingsActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (detailMode) {
      closeDetail();
    } else {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
    }
    return true;
  }
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, DELETE_HOLD_MS)) {
    if (writable) showDeleteConfirmation();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (detailMode) {
      jumpToSelected();
    } else if (!clippings.empty()) {
      openSelectedDetail();
    }
    return true;
  }
  return false;
}

bool EpubReaderClippingsActivity::handleCustomInput() {
  if (confirmPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return true;
  if (confirmingDelete) {
    confirmingDelete = false;
    requestUpdate();
    return true;
  }
  if (!detailMode) return false;

  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, DELETE_HOLD_MS)) {
    if (writable) showDeleteConfirmation();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasBackGesture()) {
    closeDetail();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    jumpToSelected();
    return true;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int buttonHeight = mappedInput.hasTouch() ? std::max(44, metrics.listRowHeight) : 0;
  const Rect openButton{safe.x + metrics.contentSidePadding,
                        safe.y + safe.height - metrics.verticalSpacing - buttonHeight,
                        safe.width - metrics.contentSidePadding * 2, buttonHeight};
  if (mappedInput.hasTouch() &&
      mappedInput.wasTapInRect(openButton.x, openButton.y, openButton.width, openButton.height)) {
    jumpToSelected();
    return true;
  }
  int touchX = 0;
  int touchY = 0;
  if (writable && mappedInput.wasScreenLongPress(touchX, touchY) && touchY < openButton.y) {
    showDeleteConfirmation();
    return true;
  }
  const int pages = detailPageCount();
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up && detailPage + 1 < pages) {
    ++detailPage;
    requestUpdate();
    return true;
  }
  if (swipe == MappedInputManager::SwipeDir::Down && detailPage > 0) {
    --detailPage;
    requestUpdate();
    return true;
  }
  buttonNavigator.onNextRelease([this, pages] {
    if (detailPage + 1 < pages) {
      ++detailPage;
      requestUpdate();
    }
  });
  buttonNavigator.onPreviousRelease([this] {
    if (detailPage > 0) {
      --detailPage;
      requestUpdate();
    }
  });
  return true;
}

void EpubReaderClippingsActivity::openSelectedDetail() {
  if (nav.selected < 0 || nav.selected >= listCount()) return;
  detailText = normalizedWhitespace(clippings[nav.selected].text);
  detailMode = true;
  detailPage = 0;
  detailLayoutWidth = 0;
  detailLinesPerPage = 0;
  rebuildDetailLayout();
  requestUpdate();
}

void EpubReaderClippingsActivity::closeDetail() {
  detailMode = false;
  detailPage = 0;
  detailText.clear();
  detailLines.clear();
  requestUpdate();
}

void EpubReaderClippingsActivity::jumpToSelected() {
  if (nav.selected < 0 || nav.selected >= listCount()) return;
  const auto& clipping = clippings[nav.selected];
  ProgressChangeResult result{};
  result.spineIndex = clipping.spineIndex;
  result.page = clipping.pageHint;
  result.hasVisibleTextOffset = true;
  result.visibleTextOffset = clipping.pageVisibleOffset;
  setResult(std::move(result));
  finish();
}

void EpubReaderClippingsActivity::showDeleteConfirmation() {
  if (!writable || nav.selected < 0 || nav.selected >= listCount() || confirmPopup.isActive()) return;
  confirmingDelete = true;
  const char* options[] = {tr(STR_CANCEL), tr(STR_DELETE)};
  confirmPopup.show(tr(STR_CONFIRM_DELETE_CLIPPING), options, 2, 0, [this](const int index) {
    confirmingDelete = false;
    if (index == 1) deleteSelected();
    requestUpdate();
  });
  requestUpdate();
}

void EpubReaderClippingsActivity::deleteSelected() {
  if (nav.selected < 0 || nav.selected >= listCount() || !epub) return;
  const int erasedIndex = nav.selected;
  ClippingCodec::Record erased = std::move(clippings[erasedIndex]);
  clippings.erase(clippings.begin() + erasedIndex);
  if (ClippingStore::save(epub->getPath(), epub->getCachePath(), clippings) != ClippingStore::SaveStatus::SAVED) {
    clippings.insert(clippings.begin() + erasedIndex, std::move(erased));
    writable = false;
    deleteFailed = true;
    LOG_ERR("CLIP", "Could not delete clipping: persistent save failed");
    return;
  }
  if (!SavedItemsCatalog::updateClippings(epub->getPath(), epub->getTitle(), epub->getAuthor(), clippings.size())) {
    LOG_ERR("SAVED", "Could not update clipping count after delete");
  }
  // The deleted selected row and confirmation dialog are both dense black
  // regions. Promote the one repaint that clears them to avoid e-paper
  // remnants, especially when the list becomes empty.
  renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
  deleteFailed = false;
  detailMode = false;
  detailText.clear();
  detailLines.clear();
  windowStart = -1;
  if (nav.selected >= listCount() && nav.selected > 0) --nav.selected;
  nav.follow(listCount());
}

void EpubReaderClippingsActivity::rebuildDetailLayout() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int width = std::max(1, safe.width - metrics.contentSidePadding * 2);
  const int footer = mappedInput.hasTouch() ? std::max(44, metrics.listRowHeight) + metrics.verticalSpacing
                                            : metrics.buttonHintsHeight;
  const int textTop = safe.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int available = std::max(1, safe.y + safe.height - footer - metrics.verticalSpacing - textTop - 24);
  const int linesPerPage = std::max(1, available / (renderer.getLineHeight(UI_10_FONT_ID) + 6));
  if (width == detailLayoutWidth && linesPerPage == detailLinesPerPage && !detailLines.empty()) return;
  detailLayoutWidth = width;
  detailLinesPerPage = linesPerPage;
  wrapAllText(renderer, detailText, width, detailLines);
  detailPage = std::min(detailPage, detailPageCount() - 1);
}

int EpubReaderClippingsActivity::detailPageCount() const {
  return detailLinesPerPage <= 0
             ? 1
             : std::max(1, static_cast<int>((detailLines.size() + detailLinesPerPage - 1) / detailLinesPerPage));
}

void EpubReaderClippingsActivity::drawFooter() {
  if (!detailMode) {
    UiListActivity::drawFooter();
    return;
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void EpubReaderClippingsActivity::renderDetail() {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Keep detail chrome on the exact same path as the list view. Apart from
  // visual consistency, this avoids a stale safe-area offset when entering
  // detail immediately after a popup/list refresh.
  drawChrome();
  rebuildDetailLayout();
  const int textX = safe.x + metrics.contentSidePadding;
  int y = safe.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int lineStep = renderer.getLineHeight(UI_10_FONT_ID) + 6;
  const int first = detailPage * detailLinesPerPage;
  const int end = std::min(first + detailLinesPerPage, static_cast<int>(detailLines.size()));
  for (int i = first; i < end; ++i) {
    renderer.drawText(UI_10_FONT_ID, textX, y, detailLines[i].c_str());
    y += lineStep;
  }
  const std::string page = std::to_string(detailPage + 1) + "/" + std::to_string(detailPageCount());
  renderer.drawCenteredText(SMALL_FONT_ID, y + 4, page.c_str());

  if (mappedInput.hasTouch()) {
    const int buttonHeight = std::max(44, metrics.listRowHeight);
    const Rect button{safe.x + metrics.contentSidePadding,
                      safe.y + safe.height - metrics.verticalSpacing - buttonHeight,
                      safe.width - metrics.contentSidePadding * 2, buttonHeight};
    renderer.drawRect(button.x, button.y, button.width, button.height);
    renderer.drawCenteredText(UI_10_FONT_ID, button.y + (button.height - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                              tr(STR_OPEN), true, EpdFontFamily::BOLD);
  } else {
    drawFooter();
  }
  if (confirmPopup.processRender(renderer, mappedInput)) return;
  renderer.displayBuffer();
}

void EpubReaderClippingsActivity::render(RenderLock&& lock) {
  if (detailMode) {
    renderDetail();
    return;
  }
  renderer.clearScreen();
  drawChrome();
  renderUi();
  for (int pass = 0; nav.consumeRebuildNeeded() && pass < 8; ++pass) {
    renderer.clearScreen();
    drawChrome();
    renderUi();
  }
  if (confirmPopup.processRender(renderer, mappedInput)) return;
  drawFooter();
  renderer.displayBuffer();
}
