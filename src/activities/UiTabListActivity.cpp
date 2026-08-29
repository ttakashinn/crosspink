#include "UiTabListActivity.h"

#include <GfxRenderer.h>

#include <cassert>

#include "MappedInputManager.h"
#include "components/UIScale.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

UiTabListActivity::UiTabListActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity(name, renderer, mappedInput) {}

void UiTabListActivity::onEnter() {
  // Size the per-tab state before the base resets activeNav() (which indexes
  // into it).
  tabNavs.assign(static_cast<size_t>(tabCount()), fui::ListNav{});
  UiListActivity::onEnter();
  app.on(ACTION_TAB, &UiTabListActivity::tabActionTrampoline, this);
}

fui::ListNav& UiTabListActivity::activeNav() {
  if (tabNavs.empty()) return nav;  // pre-onEnter fallback
  // Invariant: subclasses keep activeTab() inside [0, tabCount()), and
  // tabCount() does not change after onEnter() sized tabNavs.
  assert(activeTab() >= 0 && static_cast<size_t>(activeTab()) < tabNavs.size());
  return tabNavs[static_cast<size_t>(activeTab())];
}

int UiTabListActivity::ringPos() const {
  if (tabNavs.empty()) return 0;
  assert(activeTab() >= 0 && static_cast<size_t>(activeTab()) < tabNavs.size());
  return tabNavs[static_cast<size_t>(activeTab())].selected;
}

void UiTabListActivity::tabActionTrampoline(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<UiTabListActivity*>(user);
  if (event.value < 0 || event.value >= self->tabCount()) return;
  self->onTabAction(event.value);
}

void UiTabListActivity::onRowAction(const fui::ActionEvent& event) {
  activeNav().selected = event.value + 1;  // ring position, not row index
  activateIndex(event.value);
}

void UiTabListActivity::moveRingTo(const int ringIndex) {
  auto& n = activeNav();
  n.selected = ringIndex;
  if (ringIndex == 0) {
    n.top = 0;
  } else {
    // Pull the viewport to the row (ring - 1); ListNav::follow reads
    // n.selected as a row index, so compute directly here.
    const uint16_t rows = n.visibleRows > 0 ? static_cast<uint16_t>(n.visibleRows) : 1;
    n.top = fui::listTopIndexFor(static_cast<int16_t>(ringIndex - 1), static_cast<uint16_t>(n.top < 0 ? 0 : n.top),
                                 rows, static_cast<uint16_t>(listCount()));
  }
  requestUpdate();
}

void UiTabListActivity::navigateButtons() {
  // Buttons walk the tab band (index 0) plus the rows (1..listCount).
  const int ringSize = listCount() + 1;
  buttonNavigator.onNextRelease([this, ringSize] { moveRingTo(ButtonNavigator::nextIndex(ringPos(), ringSize)); });
  buttonNavigator.onPreviousRelease(
      [this, ringSize] { moveRingTo(ButtonNavigator::previousIndex(ringPos(), ringSize)); });
  buttonNavigator.onNextContinuous([this] { stepTab(1); });
  buttonNavigator.onPreviousContinuous([this] { stepTab(-1); });
}

void UiTabListActivity::syncTabListViewport(UiScreen& screen, fui::ListProps& props, const bool hasSubtitle) {
  const int count = listCount();
  auto& n = activeNav();
  int16_t rowHeight = screen.theme().rowHeight;
  if (!mappedInput.hasTouch()) {
    // Non-touch hardware (X3/X4) keeps the original, denser per-theme row
    // height instead of FreeInkUI's touch-target-sized default (see
    // UiListActivity::syncListViewport, the non-tab counterpart of this).
    const auto& metrics = UITheme::getInstance().getMetrics();
    rowHeight = uiScaledListMetric(hasSubtitle ? metrics.listWithSubtitleRowHeight : metrics.listRowHeight);
    // Wrapped (maxLines > 1) labels grow only their own row: list() sizes
    // wrapped items per-row, so the dense height stays for the rest.
    props.rowHeight = rowHeight;
  }
  const uint16_t rows = fui::listVisibleRows(screen.body(), rowHeight, screen.theme().listRowGap);
  n.visibleRows = rows > 0 ? rows : 1;
  if (n.followOnBuild) {
    // Screen entry / tab switch: show the tab's remembered selection, or the
    // top when the tab bar holds the focus.
    n.followOnBuild = false;
    n.top = n.selected > 0 ? static_cast<int>(fui::listTopIndexFor(
                                 static_cast<int16_t>(n.selected - 1), static_cast<uint16_t>(n.top < 0 ? 0 : n.top),
                                 static_cast<uint16_t>(n.visibleRows), static_cast<uint16_t>(count)))
                           : 0;
  }
  n.scrollBy(0, count);  // clamp to range
  // listCount() may shrink between passes (ring: 0 = tab band, 1..count = rows);
  // keep a stale ring selection from indexing past the new row count.
  if (n.selected > count) n.selected = count;
  props.topIndex = static_cast<uint16_t>(n.top);
  props.selectedIndex = static_cast<int16_t>(n.selected - 1);  // -1 = tab band focused
}

void UiTabListActivity::buildTabBar(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();

  // Tabs stay on a clean, white navigation band. The active category is
  // distinguished by weight only; touch and physical-button focus keep the
  // same geometry without introducing a second gray selected surface.
  // Stack array, not a heap vector: this runs on every render and the tab
  // count is small and fixed.
  constexpr int MAX_TABS = 8;
  const int count = tabCount() < MAX_TABS ? tabCount() : MAX_TABS;
  fui::TabItem tabs[MAX_TABS];
  for (int i = 0; i < count; i++) {
    tabs[i].label = tabLabel(i);
    tabs[i].value = static_cast<int16_t>(i);
    tabs[i].selected = activeTab() == i;
  }
  fui::TabBarProps tabProps;
  tabProps.tabs = tabs;
  tabProps.count = static_cast<uint16_t>(count);
  tabProps.action = ACTION_TAB;
  tabProps.inputMask = fui::InputTouch;
  tabProps.selectedTextBold = true;
  // Pill shape and label size are theme-driven. Label-hugging (Lyra): small
  // text so the pill wraps a compact label, kept tight horizontally so wide
  // labels (e.g. "Controls") still fit their slot at large UI scales.
  // Full-slot (RoundedRaff): the pill fills its slot like the legacy layout
  // (slot minus a 4px frame, 8px clearance above the divider) with
  // body-size labels; zero horizontal contentInset disables the tabBar's
  // label-width shrink.
  if (metrics.tabPillFullSlot) {
    tabProps.text = screen.theme().bodyText;
    tabProps.tabInset = fui::Insets{4, 4, 7, 4};
    tabProps.contentInset = fui::Insets{2, 0, 2, 0};
  } else {
    tabProps.text = screen.theme().smallText;
    tabProps.layout = metrics.tabSpaceBetween ? fui::TabBarLayout::SpaceBetween : fui::TabBarLayout::ContentWidth;
    tabProps.leadingInset = static_cast<int16_t>(metrics.contentSidePadding);
    tabProps.gap = static_cast<int16_t>(metrics.tabSpacing);
    tabProps.tabInset = fui::Insets{2, 0, 4, 0};
    tabProps.contentInset = fui::Insets{2, static_cast<int16_t>(metrics.tabContentHorizontalPadding), 2,
                                        static_cast<int16_t>(metrics.tabContentHorizontalPadding)};
  }
  // Four translated category names cannot keep generous decoration with a
  // 12 pt UI scale on 480/528 px panels. Keep the readable font and reclaim
  // horizontal whitespace only; this avoids clipped Vietnamese labels without
  // shrinking controls or descriptions.
  if (renderer.getScreenWidth() <= 528 && SETTINGS.uiScale == CrossPointSettings::UI_SCALE_LARGE) {
    tabProps.layout = metrics.tabSpaceBetween ? fui::TabBarLayout::SpaceBetween : fui::TabBarLayout::ContentWidth;
    tabProps.leadingInset = 4;
    tabProps.gap = metrics.tabPillFullSlot ? 0 : static_cast<int16_t>(metrics.tabSpacing);
    const int16_t compactPadding = static_cast<int16_t>(metrics.tabContentHorizontalPadding);
    tabProps.contentInset =
        metrics.tabPillFullSlot ? fui::Insets{2, 0, 2, 0} : fui::Insets{2, compactPadding, 2, compactPadding};
  }
  const int16_t tabLineHeight = screen.target().lineHeight(tabProps.text.font);
  const int16_t tabBand =
      static_cast<int16_t>(metrics.tabBarHeight > tabLineHeight + 10 ? metrics.tabBarHeight : tabLineHeight + 10);
  // Active tabs differ only by bold text. Keep an explicit black foreground
  // for all states so focus/tap resolution cannot fall back to the default
  // selected pill supplied by TabBar.
  tabProps.divider = true;
  if (metrics.tabActiveUnderlineSize > 0) {
    // Merge the active segment into the bottom divider instead of drawing a
    // detached decorative pill. The thicker segment remains crisp on e-ink.
    tabProps.tabInset.bottom = 0;
    tabProps.selectedUnderline = static_cast<int16_t>(metrics.tabActiveUnderlineSize);
  }
  fui::StyleSet tabStyles;
  tabStyles.explicitlySet = true;
  tabStyles.normal.foreground = fui::Paint::solid(fui::Color::Black);
  tabStyles.selected.foreground = fui::Paint::solid(fui::Color::Black);
  tabStyles.focused = tabStyles.selected;
  tabStyles.active = tabStyles.selected;
  tabProps.tabStyles = tabStyles;
  const fui::Rect tabRect = screen.takeTop(tabBand);
  fui::tabBar(screen.frame(), tabRect, tabProps);
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
}
