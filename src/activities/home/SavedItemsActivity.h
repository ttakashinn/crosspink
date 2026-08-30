#pragma once

#include <Epub.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "clippings/ClippingCodec.h"
#include "components/OptionPopup.h"
#include "saved_items/SavedItemsCatalog.h"

class SavedItemsActivity final : public UiListActivity {
 public:
  SavedItemsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const bool clippingsOnly = false)
      : UiListActivity("SavedItems", renderer, mappedInput, /*wantsTouchLongPress=*/true),
        clippingsOnly(clippingsOnly) {}

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
  const bool clippingsOnly;
  OptionPopup popup;
  std::atomic<bool> exportRequested{false};

  int listCount() const override { return static_cast<int>(rowItems.size()); }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;

  void reload();
  void rebuildRows();
  bool isExportRow(int index) const;
  int bookIndexForRow(int index) const;
  void exportClippings();
  void showOpenMenu(int index);
  void showDeleteMenu(int index);
  void showDeleteConfirmation(int index, bool bookmarks);
  void deleteAll(int index, bool bookmarks);
  void openBookmarks(int index);
  void openClippings(int index);
  std::shared_ptr<Epub> makeBook(const std::string& path);
};
