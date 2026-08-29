#pragma once

#include <Epub.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "clippings/ClippingCodec.h"
#include "components/OptionPopup.h"

// Per-book clipping browser. The reader owns the records and remains alive
// below this activity; keeping references here avoids decoding or duplicating
// a worst-case 34 KB clipping file while the rendered page is still resident.
class EpubReaderClippingsActivity final : public UiListActivity {
 public:
  EpubReaderClippingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::shared_ptr<Epub>& epub,
                              std::vector<ClippingCodec::Record>& clippings, bool& writable);

  void onEnter() override;
  void render(RenderLock&& lock) override;

 private:
  static constexpr int ROW_WINDOW = 12;
  static constexpr int DELETE_HOLD_MS = 700;

  std::shared_ptr<Epub> epub;
  std::vector<ClippingCodec::Record>& clippings;
  bool& writable;
  std::array<std::string, ROW_WINDOW> snippets;
  std::array<std::string, ROW_WINDOW> subtitles;
  std::array<freeink::ui::ListItem, ROW_WINDOW> rowItems{};
  int windowStart = -1;
  int windowCount = 0;

  bool detailMode = false;
  bool confirmingDelete = false;
  bool deleteFailed = false;
  int detailPage = 0;
  int detailLayoutWidth = 0;
  int detailLinesPerPage = 0;
  std::string detailText;
  std::vector<std::string> detailLines;
  OptionPopup confirmPopup;

  int listCount() const override { return static_cast<int>(clippings.size()); }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;
  void drawFooter() override;

  void refreshWindow(int start);
  void openSelectedDetail();
  void closeDetail();
  void jumpToSelected();
  void showDeleteConfirmation();
  void deleteSelected();
  void rebuildDetailLayout();
  void renderDetail();
  int detailPageCount() const;
};
