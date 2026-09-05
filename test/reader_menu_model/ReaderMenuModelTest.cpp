#include <gtest/gtest.h>

#include "ReaderMenuModel.h"

namespace {
using reader_menu::Action;
using reader_menu::ProgressPosition;
using reader_menu::resolveProgressPosition;
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

TEST(ReaderMenuModel, UsesActiveChapterProgressWhenSectionIsLoaded) {
  const ProgressPosition position = resolveProgressPosition(true, 11, 80, false, 3, 3, 7, 70, true);

  EXPECT_EQ(position.pageIndex, 11);
  EXPECT_EQ(position.displayPage(), 12);
  EXPECT_EQ(position.pageCount, 80);
  EXPECT_FALSE(position.pageCountEstimated);
  EXPECT_FLOAT_EQ(position.visiblePageProgress(), 12.0f / 80.0f);
}

TEST(ReaderMenuModel, RestoresCachedProgressAfterChildScreenReleasesSection) {
  const ProgressPosition position = resolveProgressPosition(false, 0, 0, false, 3, 3, 11, 80, true);

  EXPECT_EQ(position.pageIndex, 11);
  EXPECT_EQ(position.displayPage(), 12);
  EXPECT_EQ(position.pageCount, 80);
  EXPECT_TRUE(position.pageCountEstimated);
  EXPECT_FLOAT_EQ(position.visiblePageProgress(), 12.0f / 80.0f);
}

TEST(ReaderMenuModel, DoesNotUseCachedProgressFromAnotherSpine) {
  const ProgressPosition position = resolveProgressPosition(false, 0, 0, false, 4, 3, 11, 80, true);

  EXPECT_EQ(position.displayPage(), 0);
  EXPECT_EQ(position.pageCount, 0);
}

TEST(ReaderMenuModel, CountsTheVisiblePageAndClampsEndSentinel) {
  const ProgressPosition firstPage{0, 10, false};
  const ProgressPosition lastPage{9, 10, false};
  const ProgressPosition endSentinel{10, 10, false};

  EXPECT_FLOAT_EQ(firstPage.visiblePageProgress(), 0.1f);
  EXPECT_FLOAT_EQ(lastPage.visiblePageProgress(), 1.0f);
  EXPECT_FLOAT_EQ(endSentinel.visiblePageProgress(), 1.0f);
}
