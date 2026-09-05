#pragma once

#include <cstdint>

namespace reader_menu {

enum class Tab : uint8_t { Read, Marks, More, Count };

enum class Action : uint8_t {
  SELECT_CHAPTER,
  FOOTNOTES,
  TEXT_SETTINGS,
  NIGHT_MODE,
  FRONTLIGHT,
  GO_TO_PERCENT,
  AUTO_PAGE_TURN,
  ROTATE_SCREEN,
  BOOKMARKS,
  TOGGLE_BOOKMARK,
  SCREENSHOT,
  DISPLAY_QR,
  GO_HOME,
  SYNC,
  DELETE_CACHE,
  DICTIONARY,
  LOOKUP_HISTORY,
  DICTIONARY_SWITCH,
  DICTIONARY_BOOK,
  HIGHLIGHT_TEXT,
  MY_CLIPPINGS,
  READING_STATS,
  WORD_SPACING,
  REPAIR_PARAGRAPH_INDENT,
  RENDER_MODE,
  TRY_FULL_RENDER_QUALITY
};

struct ProgressPosition {
  int pageIndex = 0;
  int pageCount = 0;
  bool pageCountEstimated = false;

  constexpr int displayPage() const { return pageCount > 0 ? pageIndex + 1 : 0; }

  constexpr float visiblePageProgress() const {
    if (pageCount <= 0 || pageIndex < 0) return 0.0f;
    const int completedPages = pageIndex < pageCount ? pageIndex + 1 : pageCount;
    return static_cast<float>(completedPages) / static_cast<float>(pageCount);
  }
};

// The classic reader menu is recreated after returning from each child screen.
// Some children release the live Section to leave enough heap for fonts or long
// chapter lists, so use the saved re-pagination position until the Section is
// rebuilt. A cache from another spine must not leak into the menu header.
constexpr ProgressPosition resolveProgressPosition(const bool hasActiveSection, const int activePageIndex,
                                                   const int activePageCount, const bool activePageCountEstimated,
                                                   const int currentSpineIndex, const int cachedSpineIndex,
                                                   const int cachedPageIndex, const int cachedPageCount,
                                                   const bool cachedPageCountEstimated) {
  if (hasActiveSection) {
    return activePageCount > 0 ? ProgressPosition{activePageIndex, activePageCount, activePageCountEstimated}
                               : ProgressPosition{};
  }
  if (currentSpineIndex == cachedSpineIndex && cachedPageCount > 0) {
    return {cachedPageIndex, cachedPageCount, cachedPageCountEstimated};
  }
  return {};
}

// Read is for actions used while progressing through the current book. Marks
// owns saved/current-passage material. More is deliberately limited to global
// display controls, dictionary configuration, external services and
// maintenance. Keeping the taxonomy independent from the UI makes accidental
// reshuffling testable without constructing a hardware-backed activity.
constexpr Tab tabForAction(const Action action) {
  switch (action) {
    case Action::DICTIONARY:
    case Action::LOOKUP_HISTORY:
    case Action::TEXT_SETTINGS:
    case Action::WORD_SPACING:
    case Action::REPAIR_PARAGRAPH_INDENT:
    case Action::ROTATE_SCREEN:
    case Action::SELECT_CHAPTER:
    case Action::FOOTNOTES:
    case Action::GO_TO_PERCENT:
    case Action::AUTO_PAGE_TURN:
    case Action::READING_STATS:
      return Tab::Read;

    case Action::TOGGLE_BOOKMARK:
    case Action::BOOKMARKS:
    case Action::HIGHLIGHT_TEXT:
    case Action::MY_CLIPPINGS:
    case Action::DISPLAY_QR:
      return Tab::Marks;

    case Action::NIGHT_MODE:
    case Action::FRONTLIGHT:
    case Action::DICTIONARY_SWITCH:
    case Action::DICTIONARY_BOOK:
    case Action::SYNC:
    case Action::SCREENSHOT:
    case Action::GO_HOME:
    case Action::RENDER_MODE:
    case Action::TRY_FULL_RENDER_QUALITY:
    case Action::DELETE_CACHE:
      return Tab::More;
  }
  return Tab::More;
}

}  // namespace reader_menu
