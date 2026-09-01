#pragma once

#include <I18n.h>

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"
#include "util/Dictionary.h"

class DictionaryHistoryActivity final : public UiListActivity {
 public:
  DictionaryHistoryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string dictionaryName)
      : UiListActivity("DictionaryHistory", renderer, mappedInput), dictionaryName_(std::move(dictionaryName)) {}

  void onEnter() override;
  void render(RenderLock&& lock) override;

 private:
  int listCount() const override;
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;

  void refreshEntries();
  void lookupSelected(int index);
  void confirmClear();
  void showError(StrId message);

  std::vector<std::string> entries_;
  const std::string dictionaryName_;
  std::vector<freeink::ui::ListItem> rowItems_;
  Dictionary dictionary_;
  OptionPopup optionPopup_;
  bool busy_ = false;
  bool error_ = false;
  StrId errorMessage_ = StrId::STR_DICT_ERROR;
  unsigned long errorAt_ = 0;
};
