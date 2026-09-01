#include "DictionarySuggestionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "components/UITheme.h"

namespace fui = freeink::ui;

DictionarySuggestionActivity::DictionarySuggestionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                           std::vector<std::string> suggestions)
    : UiListActivity("DictionarySuggestions", renderer, mappedInput), suggestions_(std::move(suggestions)) {
  rows_.reserve(suggestions_.size());
  for (size_t i = 0; i < suggestions_.size(); ++i) {
    fui::ListItem item;
    item.label = suggestions_[i].c_str();
    item.actionValue = static_cast<int16_t>(i);
    rows_.push_back(item);
  }
}

const char* DictionarySuggestionActivity::headerTitle() const { return tr(STR_DICT_DID_YOU_MEAN); }

void DictionarySuggestionActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  fui::ListProps props;
  props.items = rows_.data();
  props.count = static_cast<uint16_t>(rows_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void DictionarySuggestionActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  setResult(LookupQueryResult{suggestions_[index]});
  finish();
}
