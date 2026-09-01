#pragma once
#include <Epub.h>
#include <I18n.h>

#include <array>
#include <string>
#include <vector>

#include "activities/UiTabListActivity.h"
#include "components/OptionPopup.h"

class EpubReaderMenuActivity final : public UiTabListActivity {
 public:
  enum class Tab : uint8_t { Read, Marks, More, Count };

  // Menu actions available from the reader menu.
  enum class MenuAction {
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

  struct MenuItem {
    MenuAction action;
    StrId labelId;
  };

  static void buildMenuItems(std::vector<MenuItem>& items, bool hasFootnotes, bool hasBookmarks,
                             bool hasRenderFallback = false);

  explicit EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                                  const int currentPage, const int totalPages, const int bookProgressPercent,
                                  const uint8_t currentOrientation, const bool hasFootnotes, bool hasBookmarks,
                                  uint16_t autoPageTurnSeconds, uint8_t wordSpacing, bool repairParagraphIndent,
                                  uint8_t renderMode, uint8_t activeRenderMode);

  void render(RenderLock&&) override;
  bool handleHomeGesture() override;

 private:
  // Row storage: menuItems is at most MAX_MENU_ITEMS, so a
  // fixed-capacity array avoids any heap allocation for the row list. Labels
  // are set once in the constructor (buildMenuRowItems()); buildScreen()
  // only refreshes rows whose values reflect live state.
  static constexpr size_t MAX_MENU_ITEMS = 24;
  static constexpr size_t TAB_COUNT = static_cast<size_t>(Tab::Count);
  std::array<std::array<freeink::ui::ListItem, MAX_MENU_ITEMS>, TAB_COUNT> menuRowItems{};
  void buildMenuRowItems();

  int listCount() const override { return static_cast<int>(activeItems().size()); }
  int tabCount() const override { return static_cast<int>(Tab::Count); }
  int activeTab() const override { return static_cast<int>(activeTab_); }
  const char* tabLabel(int index) const override;
  void onTabAction(int index) override;
  void stepTab(int direction) override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // Popup input runs before any button or touch handling.
  bool handleCustomInput() override;
  // Back closes on RELEASE and Confirm activates on RELEASE; everything else
  // (row navigation, page jumps) falls through to the base handler.
  bool handleButtons() override;
  // Header via GUI.drawHeader inside the safe area for the battery indicator.
  void drawChrome() override;
  // Confirm advances tabs while the tab band has focus, so name that target
  // instead of leaving the generic "Select" hint on screen.
  void drawFooter() override;

  void closeCancelled();
  void switchTab(int direction);
  const std::vector<MenuItem>& activeItems() const { return tabItems[static_cast<size_t>(activeTab_)]; }
  std::vector<MenuItem>& activeItems() { return tabItems[static_cast<size_t>(activeTab_)]; }
  static Tab tabForAction(MenuAction action);
  static std::string formatAutoTurnInterval(uint16_t seconds);
  static int autoTurnIndex(uint16_t seconds);

  // Fixed menu layout
  std::array<std::vector<MenuItem>, TAB_COUNT> tabItems;
  Tab activeTab_ = Tab::Read;

  OptionPopup optionPopup;
  std::string title = "Reader Menu";
  uint8_t pendingOrientation = 0;
  uint16_t selectedAutoTurnSeconds = 0;
  uint8_t selectedWordSpacing = 0;
  uint8_t selectedRepairParagraphIndent = 0;
  uint8_t selectedRenderMode = 0;
  const std::vector<StrId> orientationLabels = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED,
                                                StrId::STR_LANDSCAPE_CCW};
  int currentPage = 0;
  int totalPages = 0;
  int bookProgressPercent = 0;
};
