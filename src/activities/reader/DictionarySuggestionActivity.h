#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

class DictionarySuggestionActivity final : public UiListActivity {
 public:
  DictionarySuggestionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                               std::vector<std::string> suggestions);

 private:
  int listCount() const override { return static_cast<int>(suggestions_.size()); }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;

  std::vector<std::string> suggestions_;
  std::vector<freeink::ui::ListItem> rows_;
};
