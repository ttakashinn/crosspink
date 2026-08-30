#include "DictionaryHistoryActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "DictionaryDefinitionActivity.h"
#include "components/UITheme.h"
#include "util/DictionaryHistoryStore.h"

namespace fui = freeink::ui;

namespace {
constexpr unsigned long ERROR_DURATION_MS = 1500;
void indexYield(void*) { vTaskDelay(1); }

StrId lookupErrorMessage(const Dictionary::LookupResult result) {
  switch (result) {
    case Dictionary::LookupResult::NotFound:
      return StrId::STR_DICT_NOT_FOUND;
    case Dictionary::LookupResult::LowMemory:
      return StrId::STR_DICT_LOW_MEMORY;
    case Dictionary::LookupResult::Decompress:
      return StrId::STR_DICT_DECOMPRESS_ERROR;
    case Dictionary::LookupResult::ReadError:
      return StrId::STR_DICT_READ_FAILED;
    case Dictionary::LookupResult::Found:
    default:
      return StrId::STR_DICT_ERROR;
  }
}
}  // namespace

void DictionaryHistoryActivity::onEnter() {
  refreshEntries();
  UiListActivity::onEnter();
}

int DictionaryHistoryActivity::listCount() const {
  return static_cast<int>(entries_.size()) + (!entries_.empty() && DICTIONARY_HISTORY.isWritable() ? 1 : 0);
}

const char* DictionaryHistoryActivity::headerTitle() const { return tr(STR_DICT_HISTORY); }

void DictionaryHistoryActivity::refreshEntries() {
  const auto& source = DICTIONARY_HISTORY.entries();
  entries_.assign(source.begin(), source.end());
  rowItems_.clear();
  rowItems_.reserve(entries_.size() + 1);
  for (size_t i = 0; i < entries_.size(); ++i) {
    fui::ListItem item;
    item.label = entries_[i].c_str();
    item.actionValue = static_cast<int16_t>(i);
    rowItems_.push_back(item);
  }
  if (!entries_.empty() && DICTIONARY_HISTORY.isWritable()) {
    fui::ListItem clear;
    clear.label = tr(STR_DICT_HISTORY_CLEAR);
    clear.actionValue = static_cast<int16_t>(entries_.size());
    rowItems_.push_back(clear);
  }
  nav.selected = std::clamp(nav.selected, 0, std::max(0, listCount() - 1));
  nav.follow(listCount());
}

void DictionaryHistoryActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                                      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                                      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)),
                                      static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  if (rowItems_.empty()) {
    screen.centeredText(DICTIONARY_HISTORY.isWritable() ? tr(STR_DICT_HISTORY_EMPTY) : tr(STR_DICT_HISTORY_UNAVAILABLE),
                        screen.theme().bodyText);
    return;
  }
  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void DictionaryHistoryActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  if (index < static_cast<int>(entries_.size()))
    lookupSelected(index);
  else
    confirmClear();
}

void DictionaryHistoryActivity::showError(const StrId message) {
  busy_ = false;
  errorMessage_ = message;
  error_ = true;
  errorAt_ = millis();
  requestUpdate();
}

void DictionaryHistoryActivity::lookupSelected(const int index) {
  if (index < 0 || index >= static_cast<int>(entries_.size())) return;
  const std::string query = entries_[index];
  busy_ = true;
  requestUpdateAndWait();

  bool ok = dictionary_.isOpen() || dictionary_.open(SETTINGS.dictionaryName);
  Dictionary::IndexResult indexResult = Dictionary::IndexResult::Ok;
  if (ok && dictionary_.needsIndex()) ok = dictionary_.buildIndex(indexYield, nullptr, &indexResult);
  std::string definition;
  std::string headword;
  Dictionary::LookupResult lookupResult = Dictionary::LookupResult::NotFound;
  if (ok) ok = dictionary_.lookup(query.c_str(), definition, headword, &lookupResult);
  if (!ok) {
    if (indexResult == Dictionary::IndexResult::LowMemory)
      showError(StrId::STR_DICT_LOW_MEMORY);
    else if (indexResult == Dictionary::IndexResult::ReadError)
      showError(StrId::STR_DICT_READ_FAILED);
    else if (dictionary_.isOpen())
      showError(lookupErrorMessage(lookupResult));
    else
      showError(StrId::STR_DICT_ERROR);
    return;
  }

  auto activity = makeUniqueNoThrow<DictionaryDefinitionActivity>(
      renderer, mappedInput, std::move(headword), std::move(definition), dictionary_.definitionsAreHtml());
  if (!activity) {
    LOG_ERR("DHIST", "OOM allocating dictionary definition activity");
    showError(StrId::STR_DICT_LOW_MEMORY);
    return;
  }
  busy_ = false;
  DICTIONARY_HISTORY.record(query);
  if (!DICTIONARY_HISTORY.flush()) LOG_ERR("DHIST", "Could not persist dictionary history");
  refreshEntries();
  startActivityForResult(std::move(activity), [this](const ActivityResult&) { requestUpdate(); });
}

void DictionaryHistoryActivity::confirmClear() {
  const char* options[] = {tr(STR_CANCEL), tr(STR_CLEAR_BUTTON)};
  optionPopup_.show(tr(STR_DICT_HISTORY_CLEAR_CONFIRM), options, 2, 0, [this](const int selected) {
    if (selected == 1) {
      if (!DICTIONARY_HISTORY.clear()) {
        showError(StrId::STR_DICT_ERROR);
        return;
      }
      refreshEntries();
    }
    requestUpdate();
  });
  requestUpdate();
}

bool DictionaryHistoryActivity::handleCustomInput() {
  if (optionPopup_.handleInput(mappedInput, [this] { requestUpdate(); })) return true;
  if (busy_) return true;
  if (error_) {
    if (millis() - errorAt_ >= ERROR_DURATION_MS) {
      error_ = false;
      requestUpdate();
    }
    return true;
  }
  return false;
}

void DictionaryHistoryActivity::render(RenderLock&& lock) {
  if (optionPopup_.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
  if (busy_)
    GUI.drawPopup(renderer, tr(STR_DICT_LOOKING_UP));
  else if (error_)
    GUI.drawPopup(renderer, I18N.get(errorMessage_));
}
