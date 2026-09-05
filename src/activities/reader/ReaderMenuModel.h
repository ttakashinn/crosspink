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
