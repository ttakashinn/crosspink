#include "EpubReaderMenuActivity.h"

#include <GfxRenderer.h>
#include <HalFrontlight.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "components/UITheme.h"
#include "util/ButtonNavigator.h"

namespace fui = freeink::ui;

namespace {
constexpr uint16_t AUTO_TURN_SECONDS[] = {0, 5, 10, 15, 30, 45, 60, 90, 120};
constexpr StrId TAB_LABELS[] = {StrId::STR_READER_TAB_READ, StrId::STR_READER_TAB_MARKS, StrId::STR_READER_TAB_MORE};
constexpr StrId WORD_SPACING_LABELS[] = {StrId::STR_SPACING_DEFAULT, StrId::STR_SPACING_MEDIUM,
                                         StrId::STR_SPACING_WIDE};
constexpr StrId RENDER_MODE_LABELS[] = {StrId::STR_RENDER_STANDARD, StrId::STR_RENDER_SIMPLIFIED,
                                        StrId::STR_RENDER_SAFE};
}  // namespace

EpubReaderMenuActivity::EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               const std::string& title, const int currentPage, const int totalPages,
                                               const int bookProgressPercent, const uint8_t currentOrientation,
                                               const bool hasFootnotes, const bool hasBookmarks,
                                               const uint16_t autoPageTurnSeconds, const uint8_t wordSpacing,
                                               const bool repairParagraphIndent, const uint8_t renderMode,
                                               const uint8_t activeRenderMode)
    : UiTabListActivity("EpubReaderMenu", renderer, mappedInput),
      title(title),
      pendingOrientation(currentOrientation),
      selectedAutoTurnSeconds(autoPageTurnSeconds),
      selectedWordSpacing(std::min<uint8_t>(wordSpacing, 2)),
      selectedRepairParagraphIndent(repairParagraphIndent ? 1 : 0),
      selectedRenderMode(std::min<uint8_t>(renderMode, 2)),
      currentPage(currentPage),
      totalPages(totalPages),
      bookProgressPercent(bookProgressPercent) {
  std::vector<MenuItem> items;
  buildMenuItems(items, hasFootnotes, hasBookmarks, activeRenderMode > renderMode);
  for (const auto& item : items) tabItems[static_cast<size_t>(tabForAction(item.action))].push_back(item);
  buildMenuRowItems();
}

// Populates menuRowItems's labels/actionValue from menuItems. Called once
// here since menuItems (and thus which rows exist) never changes after
// construction; buildScreen() only touches the two rows with a live value.
void EpubReaderMenuActivity::buildMenuRowItems() {
  for (size_t tab = 0; tab < TAB_COUNT; ++tab) {
    for (size_t i = 0; i < tabItems[tab].size() && i < MAX_MENU_ITEMS; i++) {
      fui::ListItem item;
      item.label = I18N.get(tabItems[tab][i].labelId);
      item.actionValue = static_cast<int16_t>(i);
      menuRowItems[tab][i] = item;
    }
  }
}

void EpubReaderMenuActivity::buildMenuItems(std::vector<MenuItem>& items, bool hasFootnotes, bool hasBookmarks,
                                            const bool hasRenderFallback) {
  items.clear();
  items.reserve(MAX_MENU_ITEMS);
  items.push_back({MenuAction::SELECT_CHAPTER, StrId::STR_SELECT_CHAPTER});
  if (hasFootnotes) {
    items.push_back({MenuAction::FOOTNOTES, StrId::STR_FOOTNOTES});
  }
  if (hasBookmarks) {
    items.push_back({MenuAction::BOOKMARKS, StrId::STR_BOOKMARKS});
  }
  items.push_back({MenuAction::TOGGLE_BOOKMARK, StrId::STR_TOGGLE_BOOKMARK});
  items.push_back({MenuAction::TEXT_SETTINGS, StrId::STR_TEXT_SETTINGS});
  items.push_back({MenuAction::WORD_SPACING, StrId::STR_WORD_SPACING});
  items.push_back({MenuAction::REPAIR_PARAGRAPH_INDENT, StrId::STR_REPAIR_PARAGRAPH_INDENT});
  items.push_back({MenuAction::RENDER_MODE, StrId::STR_RENDER_MODE});
  if (hasRenderFallback) {
    items.push_back({MenuAction::TRY_FULL_RENDER_QUALITY, StrId::STR_TRY_FULL_RENDER_QUALITY});
  }
  items.push_back({MenuAction::NIGHT_MODE, StrId::STR_NIGHT_MODE});
  if (Frontlight.present()) {
    items.push_back({MenuAction::FRONTLIGHT, StrId::STR_FRONTLIGHT});
  }
  items.push_back({MenuAction::DICTIONARY, StrId::STR_LOOKUP});
  items.push_back({MenuAction::HIGHLIGHT_TEXT, StrId::STR_HIGHLIGHT_TEXT});
  items.push_back({MenuAction::MY_CLIPPINGS, StrId::STR_MY_CLIPPINGS});
  items.push_back({MenuAction::ROTATE_SCREEN, StrId::STR_ORIENTATION});
  items.push_back({MenuAction::AUTO_PAGE_TURN, StrId::STR_AUTO_TURN_INTERVAL});
  items.push_back({MenuAction::GO_TO_PERCENT, StrId::STR_GO_TO_PERCENT});
  items.push_back({MenuAction::READING_STATS, StrId::STR_READING_STATS});
  items.push_back({MenuAction::SCREENSHOT, StrId::STR_SCREENSHOT_BUTTON});
  items.push_back({MenuAction::DISPLAY_QR, StrId::STR_DISPLAY_QR});
  items.push_back({MenuAction::GO_HOME, StrId::STR_GO_HOME_BUTTON});
  items.push_back({MenuAction::SYNC, StrId::STR_SYNC_PROGRESS});
  items.push_back({MenuAction::DELETE_CACHE, StrId::STR_DELETE_CACHE});
}

EpubReaderMenuActivity::Tab EpubReaderMenuActivity::tabForAction(const MenuAction action) {
  switch (action) {
    case MenuAction::BOOKMARKS:
    case MenuAction::TOGGLE_BOOKMARK:
    case MenuAction::HIGHLIGHT_TEXT:
    case MenuAction::MY_CLIPPINGS:
    case MenuAction::DISPLAY_QR:
    case MenuAction::SYNC:
      return Tab::Marks;
    case MenuAction::ROTATE_SCREEN:
    case MenuAction::READING_STATS:
    case MenuAction::SCREENSHOT:
    case MenuAction::GO_HOME:
    case MenuAction::DELETE_CACHE:
      return Tab::More;
    default:
      return Tab::Read;
  }
}

const char* EpubReaderMenuActivity::tabLabel(const int index) const { return I18N.get(TAB_LABELS[index]); }

void EpubReaderMenuActivity::onTabAction(const int index) {
  if (optionPopup.isActive() || index < 0 || index >= tabCount()) return;
  activeTab_ = static_cast<Tab>(index);
  auto& n = activeNav();
  n.selected = 0;
  n.followOnBuild = true;
  app.clearTapFlash();
  requestUpdate();
}

void EpubReaderMenuActivity::switchTab(const int direction) {
  const int next = direction >= 0 ? ButtonNavigator::nextIndex(activeTab(), tabCount())
                                  : ButtonNavigator::previousIndex(activeTab(), tabCount());
  activeTab_ = static_cast<Tab>(next);
  auto& n = activeNav();
  n.selected = 0;
  n.followOnBuild = true;
  requestUpdate();
}

void EpubReaderMenuActivity::stepTab(const int direction) { switchTab(direction); }

int EpubReaderMenuActivity::autoTurnIndex(const uint16_t seconds) {
  for (size_t i = 0; i < std::size(AUTO_TURN_SECONDS); ++i) {
    if (AUTO_TURN_SECONDS[i] == seconds) return static_cast<int>(i);
  }
  return 0;
}

std::string EpubReaderMenuActivity::formatAutoTurnInterval(const uint16_t seconds) {
  if (seconds == 0) return I18N.get(StrId::STR_STATE_OFF);
  char value[32];
  if (seconds < 60) {
    snprintf(value, sizeof(value), I18N.get(StrId::STR_STATS_SECONDS_VALUE), static_cast<unsigned>(seconds));
  } else if (seconds % 60 == 0) {
    snprintf(value, sizeof(value), I18N.get(StrId::STR_STATS_MINUTES_VALUE), static_cast<unsigned>(seconds / 60));
  } else {
    snprintf(value, sizeof(value), I18N.get(StrId::STR_STATS_MINUTES_SECONDS_VALUE),
             static_cast<unsigned>(seconds / 60), static_cast<unsigned>(seconds % 60));
  }
  return value;
}

void EpubReaderMenuActivity::closeCancelled() {
  ActivityResult result;
  result.isCancelled = true;
  result.data = MenuResult{-1,
                           pendingOrientation,
                           selectedAutoTurnSeconds,
                           selectedWordSpacing,
                           selectedRepairParagraphIndent,
                           selectedRenderMode};
  setResult(std::move(result));
  finish();
}

bool EpubReaderMenuActivity::handleHomeGesture() {
  closeCancelled();
  return true;
}

void EpubReaderMenuActivity::activateIndex(const int index) {
  if (optionPopup.isActive()) return;
  // The activated row leaves this screen (popup or finish); a lingering flash
  // would gray an unrelated element on the next render.
  app.clearTapFlash();
  activeNav().selected = index + 1;

  const auto selectedAction = activeItems()[index].action;
  if (selectedAction == MenuAction::ROTATE_SCREEN) {
    optionPopup.show(StrId::STR_ORIENTATION, orientationLabels.data(), static_cast<int>(orientationLabels.size()),
                     pendingOrientation, [this](int idx) {
                       pendingOrientation = idx;
                       // Rotate the menu immediately. Only the renderer turns;
                       // SETTINGS.orientation stays unchanged so the reader's
                       // result handler still detects the change and reflows.
                       ReaderUtils::applyOrientation(renderer, pendingOrientation);
                       app.setDevice(uiTarget.deviceContext());  // hit rects follow the new frame
                       requestUpdate(true);
                     });
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::AUTO_PAGE_TURN) {
    std::vector<std::string> labels;
    labels.reserve(std::size(AUTO_TURN_SECONDS));
    for (const uint16_t seconds : AUTO_TURN_SECONDS) labels.push_back(formatAutoTurnInterval(seconds));
    optionPopup.show(StrId::STR_AUTO_TURN_INTERVAL, labels, autoTurnIndex(selectedAutoTurnSeconds), [this](int idx) {
      selectedAutoTurnSeconds = AUTO_TURN_SECONDS[idx];
      requestUpdate();
    });
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::WORD_SPACING) {
    optionPopup.show(StrId::STR_WORD_SPACING, WORD_SPACING_LABELS, static_cast<int>(std::size(WORD_SPACING_LABELS)),
                     selectedWordSpacing, [this](int idx) {
                       selectedWordSpacing = static_cast<uint8_t>(idx);
                       requestUpdate();
                     });
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::REPAIR_PARAGRAPH_INDENT) {
    selectedRepairParagraphIndent = selectedRepairParagraphIndent == 0 ? 1 : 0;
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::RENDER_MODE) {
    optionPopup.show(StrId::STR_RENDER_MODE, RENDER_MODE_LABELS, static_cast<int>(std::size(RENDER_MODE_LABELS)),
                     selectedRenderMode, [this](int idx) {
                       selectedRenderMode = static_cast<uint8_t>(idx);
                       requestUpdate();
                     });
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::NIGHT_MODE) {
    SETTINGS.screenInverted = SETTINGS.screenInverted == 0 ? 1 : 0;
    SETTINGS.saveToFile();
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::FRONTLIGHT) {
    const bool lightOn = !Frontlight.isOn();
    Frontlight.setOn(lightOn);
    SETTINGS.frontlightOn = lightOn ? 1 : 0;
    SETTINGS.saveToFile();
    requestUpdate();
    return;
  }

  setResult(MenuResult{static_cast<int>(selectedAction), pendingOrientation, selectedAutoTurnSeconds,
                       selectedWordSpacing, selectedRepairParagraphIndent, selectedRenderMode});
  finish();
}

bool EpubReaderMenuActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

bool EpubReaderMenuActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    closeCancelled();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (ringPos() == 0) {
      switchTab(1);
    } else {
      activateIndex(ringPos() - 1);
    }
    return true;
  }

  return false;
}

void EpubReaderMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band GUI.drawHeader paints.
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                                      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                                      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)),
                                      static_cast<int16_t>(safe.x)});

  // A compact progress summary precedes the tab band.
  std::string progressLine;
  if (totalPages > 0) {
    progressLine = std::string(tr(STR_CHAPTER_PREFIX)) + std::to_string(currentPage) + "/" +
                   std::to_string(totalPages) + std::string(tr(STR_PAGES_SEPARATOR));
  }
  progressLine += std::string(tr(STR_BOOK_PREFIX)) + std::to_string(bookProgressPercent) + "%";
  const int16_t progressHeight = static_cast<int16_t>(screen.target().lineHeight(screen.theme().smallText.font) + 4);
  const fui::Rect band = screen.takeTop(progressHeight);
  const int16_t pad = screen.theme().headerSidePadding;
  screen.target().text(band.inset(fui::Insets{0, pad, 0, pad}), progressLine.c_str(), screen.theme().smallText);
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  buildTabBar(screen);

  // menuRowItems's labels/actionValue were set once in the constructor (see
  // buildMenuRowItems()); only rows with live values need refreshing here.
  auto& rows = menuRowItems[static_cast<size_t>(activeTab_)];
  const auto& items = activeItems();
  std::string autoTurnValue;
  for (size_t i = 0; i < items.size(); i++) {
    const auto action = items[i].action;
    if (action == MenuAction::ROTATE_SCREEN) {
      rows[i].value = I18N.get(orientationLabels[pendingOrientation]);
    } else if (action == MenuAction::AUTO_PAGE_TURN) {
      autoTurnValue = formatAutoTurnInterval(selectedAutoTurnSeconds);
      rows[i].value = autoTurnValue.c_str();
    } else if (action == MenuAction::NIGHT_MODE) {
      rows[i].value = I18N.get(SETTINGS.screenInverted ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
    } else if (action == MenuAction::FRONTLIGHT) {
      rows[i].value = I18N.get(Frontlight.isOn() ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
    } else if (action == MenuAction::WORD_SPACING) {
      rows[i].value = I18N.get(WORD_SPACING_LABELS[selectedWordSpacing]);
    } else if (action == MenuAction::REPAIR_PARAGRAPH_INDENT) {
      rows[i].value = I18N.get(selectedRepairParagraphIndent ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
    } else if (action == MenuAction::RENDER_MODE) {
      rows[i].value = I18N.get(RENDER_MODE_LABELS[selectedRenderMode]);
    } else {
      rows[i].value = nullptr;
    }
  }

  fui::ListProps props;
  props.items = rows.data();
  props.count = static_cast<uint16_t>(items.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the value and the row edge
  // Label at the value's font size: both sides of the row read as one unit.
  // maxLines=2 also marks the style caller-owned (see textStyleUnset).
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncTabListViewport(screen, props);
  screen.list(props);
}

void EpubReaderMenuActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  // Header via GUI.drawHeader (already FreeInkUI-themed) for the battery
  // indicator; the rest of the screen renders through the app.
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 title.c_str());
}

void EpubReaderMenuActivity::drawFooter() {
  const char* confirmLabel = tr(STR_SELECT);
  if (ringPos() == 0) {
    const int nextTab = ButtonNavigator::nextIndex(activeTab(), tabCount());
    confirmLabel = tabLabel(nextTab);
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void EpubReaderMenuActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();
  drawChrome();

  renderUi();

  drawFooter();
  renderer.displayBuffer();
}
