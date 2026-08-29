#pragma once

#include <Epub.h>

#include <memory>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "clippings/ClippingCodec.h"
#include "components/OptionPopup.h"
#include "saved_items/SavedItemsCatalog.h"

class SavedItemsActivity final : public UiListActivity {
 public:
  SavedItemsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("SavedItems", renderer, mappedInput, /*wantsTouchLongPress=*/true) {}

  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  std::vector<SavedItemsCatalog::Entry> books;
  std::vector<std::string> subtitles;
  std::vector<freeink::ui::ListItem> rowItems;
  std::shared_ptr<Epub> activeEpub;
  std::vector<ClippingCodec::Record> activeClippings;
  bool activeClippingsWritable = true;
  bool catalogError = false;
  OptionPopup popup;

  int listCount() const override { return static_cast<int>(books.size()); }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;

  void reload();
  void rebuildRows();
  void showOpenMenu(int index);
  void showDeleteMenu(int index);
  void showDeleteConfirmation(int index, bool bookmarks);
  void deleteAll(int index, bool bookmarks);
  void openBookmarks(int index);
  void openClippings(int index);
  std::shared_ptr<Epub> makeBook(const std::string& path);
};
