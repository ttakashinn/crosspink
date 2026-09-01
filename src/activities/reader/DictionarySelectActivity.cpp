#include "DictionarySelectActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "components/UITheme.h"

namespace fui = freeink::ui;

DictionarySelectActivity::DictionarySelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                   const Mode mode, std::string currentName, std::string globalName)
    : UiListActivity("DictionarySelect", renderer, mappedInput),
      mode_(mode),
      currentName_(std::move(currentName)),
      globalName_(std::move(globalName)) {}

void DictionarySelectActivity::onEnter() {
  DictionaryRegistry::discover(dictionaries_);
  rebuildRows();
  UiListActivity::onEnter();

  const int prefixRows = mode_ == Mode::PerBook ? 2 : 0;
  for (int i = 0; i < static_cast<int>(dictionaries_.size()); ++i) {
    if (dictionaries_[i].name == currentName_) {
      nav.reset(prefixRows + i);
      break;
    }
  }
}

int DictionarySelectActivity::listCount() const { return static_cast<int>(rows_.size()); }

const char* DictionarySelectActivity::headerTitle() const {
  return I18N.get(mode_ == Mode::PerBook ? StrId::STR_DICT_BOOK : StrId::STR_DICT_SWITCH);
}

void DictionarySelectActivity::rebuildRows() {
  rows_.clear();
  subtitles_.clear();
  rows_.reserve(dictionaries_.size() + 2);
  subtitles_.reserve(dictionaries_.size() + 2);

  if (mode_ == Mode::PerBook) {
    fui::ListItem global;
    global.label = tr(STR_DICT_USE_GLOBAL);
    subtitles_.push_back(globalName_.empty() ? tr(STR_NONE_OPT) : globalName_);
    global.subtitle = subtitles_.back().c_str();
    global.actionValue = 0;
    rows_.push_back(global);

    fui::ListItem none;
    none.label = tr(STR_NONE_OPT);
    none.actionValue = 1;
    rows_.push_back(none);
  }

  const int prefixRows = mode_ == Mode::PerBook ? 2 : 0;
  for (size_t i = 0; i < dictionaries_.size(); ++i) {
    fui::ListItem item;
    item.label = dictionaries_[i].name.c_str();
    if (dictionaries_[i].name == currentName_) {
      subtitles_.push_back(tr(STR_DICT_CURRENT));
      item.subtitle = subtitles_.back().c_str();
    }
    item.actionValue = static_cast<int16_t>(prefixRows + i);
    rows_.push_back(item);
  }
}

void DictionarySelectActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  if (rows_.empty()) {
    screen.centeredText(tr(STR_DICT_NO_DICTIONARIES), screen.theme().bodyText);
    return;
  }
  fui::ListProps props;
  props.items = rows_.data();
  props.count = static_cast<uint16_t>(rows_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText.maxLines = 2;
  props.subtitleText.maxLines = 1;
  syncListViewport(screen, props, true);
  screen.list(props);
}

void DictionarySelectActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  if (mode_ == Mode::PerBook) {
    if (index == 0) {
      setResult(DictionarySelectionResult{"", true});
      finish();
      return;
    }
    if (index == 1) {
      setResult(DictionarySelectionResult{"", false});
      finish();
      return;
    }
  }
  const int dictionaryIndex = index - (mode_ == Mode::PerBook ? 2 : 0);
  if (dictionaryIndex < 0 || dictionaryIndex >= static_cast<int>(dictionaries_.size())) return;
  setResult(DictionarySelectionResult{dictionaries_[dictionaryIndex].name, false});
  finish();
}
