#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "util/DictionaryRegistry.h"

class DictionarySelectActivity final : public UiListActivity {
 public:
  enum class Mode : uint8_t { Temporary, PerBook };

  DictionarySelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Mode mode, std::string currentName,
                           std::string globalName);

  void onEnter() override;

 private:
  int listCount() const override;
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;

  void rebuildRows();

  Mode mode_;
  std::string currentName_;
  std::string globalName_;
  std::vector<DictionaryEntry> dictionaries_;
  std::vector<freeink::ui::ListItem> rows_;
  std::vector<std::string> subtitles_;
};
