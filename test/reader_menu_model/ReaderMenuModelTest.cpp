#include <gtest/gtest.h>

#include "ReaderMenuModel.h"

namespace {
using reader_menu::Action;
using reader_menu::Tab;
using reader_menu::tabForAction;
}  // namespace

TEST(ReaderMenuModel, KeepsFrequentReadingControlsInRead) {
  EXPECT_EQ(tabForAction(Action::DICTIONARY), Tab::Read);
  EXPECT_EQ(tabForAction(Action::LOOKUP_HISTORY), Tab::Read);
  EXPECT_EQ(tabForAction(Action::TEXT_SETTINGS), Tab::Read);
  EXPECT_EQ(tabForAction(Action::ROTATE_SCREEN), Tab::Read);
  EXPECT_EQ(tabForAction(Action::SELECT_CHAPTER), Tab::Read);
  EXPECT_EQ(tabForAction(Action::AUTO_PAGE_TURN), Tab::Read);
  EXPECT_EQ(tabForAction(Action::READING_STATS), Tab::Read);
}

TEST(ReaderMenuModel, KeepsMarksScopedToCurrentAndSavedPassages) {
  EXPECT_EQ(tabForAction(Action::TOGGLE_BOOKMARK), Tab::Marks);
  EXPECT_EQ(tabForAction(Action::BOOKMARKS), Tab::Marks);
  EXPECT_EQ(tabForAction(Action::HIGHLIGHT_TEXT), Tab::Marks);
  EXPECT_EQ(tabForAction(Action::MY_CLIPPINGS), Tab::Marks);
  EXPECT_EQ(tabForAction(Action::DISPLAY_QR), Tab::Marks);
}

TEST(ReaderMenuModel, KeepsConfigurationServicesAndMaintenanceInMore) {
  EXPECT_EQ(tabForAction(Action::DICTIONARY_SWITCH), Tab::More);
  EXPECT_EQ(tabForAction(Action::DICTIONARY_BOOK), Tab::More);
  EXPECT_EQ(tabForAction(Action::SYNC), Tab::More);
  EXPECT_EQ(tabForAction(Action::RENDER_MODE), Tab::More);
  EXPECT_EQ(tabForAction(Action::DELETE_CACHE), Tab::More);
}
